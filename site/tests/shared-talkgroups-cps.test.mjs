// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard

import assert from "node:assert/strict";
import { test } from "node:test";
import { empty, encode, decode, newChannel, newContact, remove, validate } from "../static/js/codeplug/model.js";
import { BANK_SIZE, POOL_START, readPool } from "../static/js/codeplug/talkgroup-pool.js";
import { convertOperatingTalkgroups } from "../static/js/codeplug/convert-talkgroups.js";
import { CpsSerial } from "../static/js/codeplug/transport.js";
import { FakeCpsRadio } from "../static/js/codeplug/fakecps.js";

function legacy() {
	const model = empty();
	model.contacts.push(newContact({ number: 20, name: "World 1", id: 91, timeslot: 1 }));
	model.contacts.push(newContact({ number: 21, name: "World 2", id: 91, timeslot: 2 }));
	model.channels.push(newChannel({
		number: 4, name: "Repeater", mode: "dmr",
		talkGroups: [{ id: 91, timeslot: 1 }, { id: 91, timeslot: 2 }],
	}));
	return encode(model);
}

test("converted CPS records keep their identity, order and timeslot through edits", () => {
	const image = convertOperatingTalkgroups(legacy().bytes).image;
	const m = decode(image);
	const channel = m.channels[0];
	const [first, second] = channel.talkgroupRefs;
	assert.equal(first.number, 20);
	assert.equal(second.number, 21);
	assert.deepEqual(encode(m).bytes, image.bytes);
	assert.throws(() => remove(m, "contacts", [first]), /before deleting/);

	first.id = 3110000;
	channel.talkgroupRefs.reverse();
	const next = decode(encode(m));
	assert.deepEqual(next.channels[0].talkgroupRefs.map(contact => [contact.number, contact.id, contact.timeslot]),
		[[21, 91, 2], [20, 3110000, 1]]);
	next.channels[0].talkgroupRefs.length = 0;
	remove(next, "contacts", [next.contacts.find(contact => contact.number === 20)]);
	assert.equal(validate(next).filter(p => p.level === "error").length, 0);
});

test("a CPS transfer commits to the inactive bank and preserves the old codeplug region", async () => {
	const before = legacy();
	const converted = convertOperatingTalkgroups(before.bytes).image;
	const radio = new FakeCpsRadio({ image: before.bytes, features: "FM,DMR,DMR_TALKGROUP_POOL" });
	const client = new CpsSerial(radio.stream());
	await client.identify([{ codeplug: { names: ["DM-1701"], base: 0, end: 0xE00000 } }]);
	let have = await client.read({ iddb: false });
	const first = await client.write(converted, have);
	await client.verify(converted, first.plan);
	assert.equal(readPool(radio.spi).index, 0);
	let active = radio.spi.slice(POOL_START, POOL_START + BANK_SIZE);

	have = await client.read({ iddb: false });
	const model = decode(converted);
	model.channels[0].talkgroupRefs.reverse();
	const next = encode(model);
	const result = await client.write(next, have);
	await client.verify(next, result.plan);
	assert.deepEqual(radio.spi.slice(POOL_START, POOL_START + BANK_SIZE), active);
	assert.deepEqual(readPool(radio.spi).lists.get(4), [21, 20]);
	assert.deepEqual(radio.spi.slice(0xB000, 0x10000), before.bytes.slice(0xB000, 0x10000));
	assert.deepEqual(decode(await client.read({ iddb: false })).channels[0].talkgroupRefs.map(contact => contact.number), [21, 20]);
	await assert.rejects(client.write(before, result.image), { code: "talkgroups_convert_first" });
	await client.close();
});

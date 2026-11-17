// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
import assert from "node:assert/strict";
import { test } from "node:test";

import { empty, encode, decode, newChannel, newContact } from "../static/js/codeplug/model.js";
import { CONTACTS, CONTACT_SIZE, LIMITS } from "../static/js/codeplug/records.js";
import { convertOperatingTalkgroups } from "../static/js/codeplug/convert-talkgroups.js";
import { POOL_START, readPool } from "../static/js/codeplug/talkgroup-pool.js";

function fixture() {
	const model = empty();
	model.contacts.push(newContact({ number: 12, type: "group", id: 91, name: "World", timeslot: 0 }));
	model.contacts.push(newContact({ number: 19, type: "private", id: 91, name: "Private", timeslot: 0 }));
	model.channels.push(newChannel({
		number: 11, name: "Test", mode: "dmr", timeslot: 1, talkGroups: [
			{ id: 91, timeslot: 1 }, { id: 91, timeslot: 2 }, { id: 3100, timeslot: 1 },
			{ id: 3100000, timeslot: 2 }, { id: 120, timeslot: 1 },
		],
	}));
	return encode(model);
}

test("converter preserves legacy order, distinct slots, and unchanged unrelated bytes", () => {
	const original = fixture();
	const before = original.bytes.slice();
	const { image, report } = convertOperatingTalkgroups(original.bytes);
	assert.deepEqual(original.bytes, before);
	assert.equal(report.channels, 1);
	assert.equal(report.assignments, 5);
	assert.equal(report.created.length, 4);
	const refs = readPool(image.bytes).lists.get(11);
	assert.equal(refs[0], 12);
	assert(refs.every(ref => ref >= 1 && ref <= LIMITS.contacts));
	const byNumber = new Map(decode(image).contacts.map(contact => [contact.number, contact]));
	const expected = decode(original).channels[0].talkGroups;
	assert.deepEqual(refs.map(ref => ({ id: byNumber.get(ref).id, timeslot: byNumber.get(ref).timeslot || 1 })), expected);
	assert.equal(byNumber.get(refs[1]).name, "TG 91 TS2");
	assert.equal(byNumber.get(19).type, "private");
	assert.deepEqual(image.bytes.subarray(0xB000, 0x10000), before.subarray(0xB000, 0x10000));
	image.bytes.fill(0xFF, 0xB000, 0x10000);
	assert.deepEqual(readPool(image.bytes).lists.get(11), refs);
});

test("a nonblank candidate bank and an already converted image are refused", () => {
	const original = fixture();
	const changed = original.bytes.slice();
	changed[POOL_START + 1] = 1;
	assert.throws(() => convertOperatingTalkgroups(changed), /unrecognized data/);
	const converted = convertOperatingTalkgroups(original.bytes).image;
	assert.throws(() => convertOperatingTalkgroups(converted.bytes), /already uses/);
});

test("unrecognized old overflow is reported and an occupied contact slot is preserved", () => {
	const original = fixture();
	const noOverflow = decode(original);
	noOverflow.channels[0].talkGroups.length = 4;
	const image = encode(noOverflow);
	image.bytes[0xF000] = 0x17;
	image.bytes[CONTACTS] = 0x01;
	const { image: converted, report } = convertOperatingTalkgroups(image.bytes);
	assert(report.unrecognizedOverflow);
	assert.equal(converted.bytes[0xF000], 0x17);
	assert.deepEqual(converted.bytes.subarray(CONTACTS, CONTACTS + CONTACT_SIZE), image.bytes.subarray(CONTACTS, CONTACTS + CONTACT_SIZE));
	assert(!report.created.some(contact => contact.number === 1));
});

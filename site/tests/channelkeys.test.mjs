// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
import { test } from "node:test";
import assert from "node:assert/strict";
import { empty, newChannel, encode, decode, remove } from "../static/js/codeplug/model.js";
import { CHANNEL_KEYS, channelKeySlot } from "../static/js/codeplug/channelkeys.js";
import { CpsSerial } from "../static/js/codeplug/transport.js";

const channel = (privacySlot) => newChannel({ name: "Test", rxHz: 446000000, txHz: 446000000, mode: "dmr", privacySlot });

test("editing channel encryption preserves TS2 across codeplug saves and reads", () => {
	const m = empty();
	m.channels.push(...[1, 128, 129, 1024].map((number) => newChannel({
		...channel(0), number, timeslot: 2,
	})));
	let back = decode(encode(m));
	for (const privacySlot of [1, 4, 255, 0]) {
		back.channels.forEach((c) => { c.privacySlot = privacySlot; });
		back = decode(encode(back));
		assert.deepEqual(back.channels.map((c) => [c.number, c.timeslot, c.privacySlot]),
			[1, 128, 129, 1024].map((number) => [number, 2, privacySlot]));
	}
});

test("channel bindings survive file round trips, channel renumbering, and deletion without key material", () => {
	const m = empty();
	m.channels.push(channel(1), channel(255), channel(4));
	let image = encode(m), back = decode(image);
	assert.deepEqual(back.channels.map((c) => c.privacySlot), [1, 255, 4]);
	back.channels[0].number = 1000;
	remove(back, "channels", [back.channels[1]]);
	image = encode(back);
	assert.equal(channelKeySlot(image.bytes, 1000), 1);
	assert.equal(channelKeySlot(image.bytes, 1), 0);
	assert.equal(channelKeySlot(image.bytes, 2), 0);
	assert.equal(channelKeySlot(image.bytes, 3), 4);
	assert.deepEqual(encode(decode(image)).bytes, image.bytes);
});

test("ordinary channel edits preserve an unknown extension and AES assignments refuse to overwrite it", () => {
	const m = empty();
	m.base.bytes[CHANNEL_KEYS] = 0x42;
	m.channels.push(channel(0));
	assert.equal(encode(m).bytes[CHANNEL_KEYS], 0x42);
	m.channels[0].privacySlot = 2;
	assert.throws(() => encode(m), /unknown data/);
	assert.equal(m.base.bytes[CHANNEL_KEYS], 0x42);
});

test("damaged and future-format bindings remain invalid rather than becoming clear channels", () => {
	const m = empty(); m.channels.push(channel(2));
	const image = encode(m);
	image.bytes[CHANNEL_KEYS + 9] ^= 1;
	assert.equal(decode(image).channels[0].privacySlot, 254);
	assert.equal(channelKeySlot(encode(decode(image)).bytes, 1), 254);
	image.bytes[CHANNEL_KEYS + 5] = 2;
	assert.equal(decode(image).channels[0].privacySlot, 254);
});

test("older firmware cannot receive a codeplug with explicit encryption assignments", async () => {
	const m = empty(); m.channels.push(channel(1));
	const client = new CpsSerial({});
	let writes = 0;
	client.fastOr = async () => { writes++; };
	await assert.rejects(() => client.write(encode(m)), (e) => e.code === "channel_keys_firmware");
	assert.equal(writes, 0);
	client.probe = { version: { aes_channel_keys: "1" } };
	await client.write(encode(m));
	assert.equal(writes, 1);
});

test("identified firmware capabilities use the VERSION response nested by the real probe", async () => {
	const m = empty(); m.channels.push(channel(2));
	const client = new CpsSerial({});
	const version = { board: "test", features: "DMR", aes_channel_keys: "1" };
	client.link = {
		version: async () => version, chip: async () => null,
		flashSize: async () => null, storageInfo: async () => ({ region: "ours" }),
	};
	let writes = 0;
	client.fastOr = async () => { writes++; };
	const radios = [{ id: "test", codeplug: { names: ["test"], base: 0 } }];
	await client.identify(radios);
	assert.equal(client.probe.version.aes_channel_keys, "1");
	assert.equal(client.probe.aes_channel_keys, undefined);
	await client.write(encode(m));
	assert.equal(writes, 1);
	delete version.aes_channel_keys;
	await client.identify(radios);
	await assert.rejects(() => client.write(encode(m)), (e) => e.code === "channel_keys_firmware");
	assert.equal(writes, 1, "unsupported firmware remains protected from writes");
});

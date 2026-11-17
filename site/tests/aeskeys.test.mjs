// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Garrett Dothard
import { test } from "node:test";
import assert from "node:assert/strict";
import { AesKeys, keyBytes, randomKey } from "../static/js/codeplug/aeskeys.js";

test("random key generation uses all 32 bytes and wipes its temporary buffer", () => {
	let buffer;
	const text = randomKey(bytes => {
		buffer = bytes;
		bytes.forEach((_, index) => { bytes[index] = index; });
	});
	assert.equal(text, Array.from({ length: 32 }, (_, i) => i.toString(16).padStart(2, "0")).join("").toUpperCase());
	assert.ok(buffer.every(byte => byte === 0));
	assert.throws(() => randomKey(bytes => { buffer = bytes; bytes.fill(17); throw new Error("unavailable"); }));
	assert.ok(buffer.every(byte => byte === 0));
});

const noPause = async () => {};
test("key validation preserves every byte and rejects partial or malformed values", () => {
	const hex = Array.from({ length: 32 }, (_, i) => i.toString(16).padStart(2, "0")).join("");
	assert.deepEqual(keyBytes(hex), Uint8Array.from({ length: 32 }, (_, i) => i));
	for (const value of ["", "ab".repeat(31), "ab".repeat(33), "zz".repeat(32)]) assert.throws(() => keyBytes(value));
});
test("program sends the full key with a random nonzero seed, then wipes its mutable packet", async () => {
	let received, packet;
	const client = new AesKeys({ request: async (cmd, data) => {
		assert.equal(cmd, 0x0A); received = data.slice(); packet = data;
		return new Uint8Array();
	} }, { pause: noPause, random: (a) => { a[0] = 0x12345678; } });
	await client.program(2, 7, "ab".repeat(32));
	assert.equal(received.length, 40);
	assert.deepEqual([...received.slice(0, 8)], [36, 1, 2, 7, 0x78, 0x56, 0x34, 0x12]);
	assert.deepEqual([...received.slice(8)], Array(32).fill(0xAB));
	assert.ok(packet.every((v) => v === 0));
});
test("metadata contains slot IDs and active selection, with no secret material", async () => {
	const client = new AesKeys({ request: async () => new Uint8Array([1, 4, 2, 1, 7, 8, 0, 0]) }, { pause: noPause });
	assert.deepEqual(await client.list(), { active: 2, ready: true, slots: [
		{ slot: 1, id: 7 }, { slot: 2, id: 8 }, { slot: 3, id: 0 }, { slot: 4, id: 0 },
	] });
});
test("older firmware gives an actionable upgrade error", async () => {
	const client = new AesKeys({ request: async () => { throw Object.assign(new Error(), { status: 1 }); } }, { pause: noPause });
	await assert.rejects(() => client.list(), /firmware/);
});
test("unavailable keys report the read-only general storage status without claiming or writing", async () => {
	const calls = [];
	const client = new AesKeys({
		request: async (cmd, data) => { calls.push([cmd, ...data]); return new Uint8Array([1, 4, 0, 0, 0, 0, 0, 0]); },
		storageInfo: async () => { calls.push("storageInfo"); return { region: "foreign" }; },
	}, { pause: noPause });
	const state = await client.list();
	assert.equal(state.ready, false);
	assert.equal(state.storage.region, "foreign");
	assert.deepEqual(calls, [[0x0A, 36, 0], "storageInfo"]);
});
test("key bank unavailable with otherwise ready storage is reported separately", async () => {
	const client = new AesKeys({
		request: async () => new Uint8Array([1, 4, 0, 0, 0, 0, 0, 0]),
		storageInfo: async () => ({ region: "ours" }),
	}, { pause: noPause });
	assert.equal((await client.list()).storage.region, "ours");
});
test("slot selection waits for application, including a replacement using the same key ID", async () => {
	let reads = 0;
	const client = new AesKeys({ request: async (cmd) => {
		if (cmd === 0x09) {
			reads++;
			const text = `dmr.aes_serial=${reads > 2 ? 9 : 8}\ndmr.aes_key_id=7\ndmr.privacy=1\n`;
			return new Uint8Array([...Array(5).fill(0), ...new TextEncoder().encode(text)]);
		}
		return new Uint8Array([1, 4, 1, 1, 7, 0, 0, 0]);
	} }, { pause: noPause });
	assert.equal((await client.select(1)).active, 1);
	assert.equal(reads, 3);
});
test("unchanged serial cannot falsely acknowledge a saved selection", async () => {
	const client = new AesKeys({ request: async (cmd) => cmd === 0x09 ?
		new Uint8Array([...Array(5).fill(0), ...new TextEncoder().encode("dmr.aes_serial=8\ndmr.aes_key_id=7\n")]) :
		new Uint8Array([1, 4, 1, 1, 7, 0, 0, 0]) }, { pause: noPause });
	await assert.rejects(() => client.select(1), /failed/);
});

test("saving the radio default respects the current channel's explicit slot", async () => {
	const client = new AesKeys({ request: async (cmd) => cmd === 0x09 ?
		new Uint8Array([...Array(5).fill(0), ...new TextEncoder().encode("dmr.aes_channel_slot=2\ndmr.aes_key_id=9\n")]) :
		new Uint8Array([1, 4, 1, 1, 7, 9, 0, 0]) }, { pause: noPause });
	assert.equal((await client.select(1)).active, 1);
});
test("deleting the active key waits for the RAM session to turn off", async () => {
	let deleted = false;
	const client = new AesKeys({ request: async (cmd, packet) => {
		if (cmd === 0x09) return new Uint8Array([...Array(5).fill(0),
			...new TextEncoder().encode("dmr.aes_key_id=0\ndmr.privacy=0\n")]);
		if (packet[1] === 2) deleted = true;
		return new Uint8Array([1, 4, 1, 1, 7, 0, 0, 0]);
	} }, { pause: noPause });
	await client.remove(1);
	assert.ok(deleted);
});
test("a lost off request is reported when deleting the active key", async () => {
	const client = new AesKeys({ request: async (cmd) => cmd === 0x09 ?
		new Uint8Array([...Array(5).fill(0), ...new TextEncoder().encode("dmr.aes_key_id=7\ndmr.privacy=1\n")]) :
		new Uint8Array([1, 4, 1, 1, 7, 0, 0, 0]) }, { pause: noPause });
	await assert.rejects(() => client.remove(1), /failed/);
});

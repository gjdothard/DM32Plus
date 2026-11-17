// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
import assert from "node:assert/strict";
import { test } from "node:test";
import { encodeTalkGroups } from "../static/js/codeplug/talkgroups.js";
import {
	BANK_SIZE, POOL_START, POOL_END, MAX_ASSIGNMENTS, legacyOffset, legacyReference, migrateLegacyPool, readPool, writePool,
} from "../static/js/codeplug/talkgroup-pool.js";

const erased = () => new Uint8Array(POOL_END).fill(0xFF);

test("an ordered pool supports 63 references and exact legacy assignments", () => {
	const bytes = erased();
	const oldInline = legacyReference(0xB000), oldOverflow = legacyReference(0xF808);
	const lists = new Map([[1, [1024, 2, oldInline, oldOverflow]], [1024, Array.from({ length: 63 }, (_, i) => i + 1)]]);
	assert.equal(writePool(bytes, lists).used, 67);
	assert.deepEqual(readPool(bytes).lists, lists);
	assert.equal(legacyOffset(oldOverflow), 0xF808);
	assert.throws(() => legacyReference(0xF004), /offset/);
	assert.throws(() => writePool(bytes, new Map([[4, Array.from({ length: MAX_ASSIGNMENTS + 1 }, (_, i) => i + 1)]])), /Invalid/);
});

test("truncated or corrupt inactive bank leaves the previous generation readable", () => {
	const bytes = erased();
	const before = new Map([[1, [11, 12]], [2, [13]]]);
	const after = new Map([[1, [10, 12]], [2, [13, 14]]]);
	writePool(bytes, before);
	const incomplete = bytes.slice();
	const changed = writePool(bytes, after);
	assert.equal(changed.index, 1);
	for (const offset of [8, 16, 1024, 2067]) {
		const torn = incomplete.slice();
		const start = POOL_START + BANK_SIZE;
		torn.set(bytes.subarray(start, start + offset), start);
		assert.deepEqual(readPool(torn).lists, before);
	}
	assert.deepEqual(readPool(bytes).lists, after);
	bytes[POOL_START + BANK_SIZE + 2068] ^= 1;
	assert.deepEqual(readPool(bytes).lists, before);
});

test("unrecognized data is preserved and capacity failure does not change the image", () => {
	const bytes = erased();
	bytes[POOL_START] = 0x56;
	const before = bytes.slice();
	assert.throws(() => writePool(bytes, new Map()), /unknown/);
	assert.deepEqual(bytes, before);
	const blank = erased();
	const entries = Array.from({ length: 63 }, (_, i) => i + 1);
	const tooMany = new Map(Array.from({ length: 115 }, (_, i) => [i + 1, entries]));
	assert.throws(() => writePool(blank, tooMany), /full/);
	assert.deepEqual(blank, erased());
});


test("migration resolves exact slot matches and retains missing and dual-slot entries in order", () => {
	const bytes = erased();
	const channels = [{ number: 11, mode: "dmr", timeslot: 2, talkGroups: [
		{ id: 91, timeslot: 2 }, { id: 91, timeslot: 1 }, { id: 3100, timeslot: 1 },
		{ id: 9999000, timeslot: 2 }, { id: 120, timeslot: 1 },
	] }];
	encodeTalkGroups(bytes, channels);
	const lists = migrateLegacyPool(bytes, channels, [
		{ number: 42, id: 91, timeslot: 0, type: "group" },
		{ number: 16, id: 3100, timeslot: 1, type: "group" },
	]);
	const list = lists.get(11);
	assert.equal(list.length, 5);
	assert.equal(list[0], 42);
	assert.equal(legacyOffset(list[1]), 0xB000 + 10 * 16 + 4);
	assert.equal(list[2], 16);
	assert.equal(legacyOffset(list[3]), 0xB000 + 10 * 16 + 12);
	assert.equal(legacyOffset(list[4]), 0xF808);
	writePool(bytes, lists);
	assert.deepEqual(readPool(bytes).lists, lists);
});

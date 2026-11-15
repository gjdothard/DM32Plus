// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const strings = readFileSync(new URL("../i18n/en.json", import.meta.url), "utf8");
globalThis.document = { body: { dataset: {} }, getElementById: () => ({ textContent: strings }) };
const { splitOwnImage } = await import("../static/js/flasher/c7000ui.js");
const { target } = await import("../static/js/flasher/c7000.js");
const c = { app_start: 0xC000, app_end: 0x100000, sector: 4096,
	boards: { test: { slot: [0xE8000, 0x100000], bootloader_id: "BFUTEST" } } };

test("web application import rejects vendor headers before shifting the entry point or codec", async () => {
	for (const header of ["BFUV32-V2", "BFUTEST", "BFU-UNKNOWN"]) {
		const file = new Uint8Array(4096).fill(255);
		file.set(new TextEncoder().encode(header));
		file.set([0x80, 0xC0, 0x20, 0x70], 256);
		await assert.rejects(() => splitOwnImage(c, "test", file), /vendor-updater header/);
		assert.throws(() => target(c, "test", file), /vendor-wrapped/);
	}
});

test("raw application import preserves the actual entry point without dropping bytes", async () => {
	const file = new Uint8Array([0x80, 0xC0, 0x20, 0x70, 0x15, 0x10, 0, 0xC0]);
	const result = await splitOwnImage(c, "test", file);
	assert.deepEqual(result.image, file);
	assert.equal(result.slot, null);
	const [addr, data] = target(c, "test", result.image);
	assert.equal(addr, c.app_start);
	assert.deepEqual(data.subarray(0, file.length), file);
});

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
import assert from "node:assert/strict";
import { test } from "node:test";
import { createHash } from "node:crypto";
import { bundledRelease, loadBundledRelease } from "../static/js/flasher/dm32plus-release.js";

const bytes = Uint8Array.of(72, 3, 26, 1);
const sha256 = createHash("sha256").update(bytes).digest("hex");
const name = "dm32plus-dm32-v26.11b1";
const release = {
	board: "dm32", variant: "full", product: "DM32Plus", version: "v26.11b1",
	image: `firmware/dm32/${name}.bin`, manifest: `firmware/dm32/${name}.json`, sha256,
};
const board = { id: "dm32", name: "DM-32UV", app_base: 0x300c000, flash_end: 0x3100000,
	protected: [[0x3000000, 0x300c000]], sectors: [], codec: { id: "ambe", windows: [[0x30e8000, 0x18000]] } };
const fb = { manifest_format: "rfoundry-release/1", magic: "STOCK", boards: { dm32: board } };
const manifest = {
	format: fb.manifest_format, board: board.id, variant: "full", product: release.product, version: release.version,
	image: { file: name + ".bin", size: bytes.length, sha256 },
	codec: { profile: "ambe", included: false }, ranges: [{ addr: board.app_base, size: bytes.length }],
};

test("selects only the bundled DM32Plus release for the supported board", () => {
	assert.equal(bundledRelease([release, { ...release, product: "RadioFoundry" }], "dm32"), release);
	assert.equal(bundledRelease([release], "dm1701"), null);
});

test("loads only local assets and validates the board, checksum and codec status", async () => {
	const requested = [];
	const fetchFile = async (url) => {
		requested.push(url);
		return new Response(url.endsWith(".json") ? JSON.stringify(manifest) : bytes);
	};
	const fw = await loadBundledRelease(release, "../", fb, fetchFile);
	assert.deepEqual(fw.image, bytes);
	assert.deepEqual(requested, ["../" + release.manifest, "../" + release.image]);
	await assert.rejects(loadBundledRelease({ ...release, image: "https://example.com/image.bin" }, "../", fb, fetchFile), /invalid file paths/);
	await assert.rejects(loadBundledRelease(release, "../", fb, async (url) =>
		new Response(url.endsWith(".json") ? JSON.stringify({ ...manifest, codec: { included: true } }) : bytes)), /does not match/);
	await assert.rejects(loadBundledRelease(release, "../", fb, async (url) =>
		new Response(url.endsWith(".json") ? JSON.stringify(manifest) : Uint8Array.of(72, 3, 26, 2))), /SHA-256/);
});

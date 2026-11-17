// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
import assert from "node:assert/strict";
import { test } from "node:test";
import { createHash } from "node:crypto";
import { bundledRelease, loadBundledRelease, lookupRelease, newestRelease } from "../static/js/flasher/dm32plus-release.js";

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

const published = (version, prerelease, draft = false) => ({
	tag_name: "dm32plus-" + version, prerelease, draft,
	assets: ["bin", "json"].map((ext) => ({
		name: `dm32plus-dm32-${version}.${ext}`, state: "uploaded", digest: "sha256:" + sha256,
	})),
});

test("chooses the newest complete stable or development version in numerical order", () => {
	const incomplete = published("v27.1b1", true);
	incomplete.assets.pop();
	const releases = [published("v26.11b2", true), published("v26.11b10", true), published("v26.12", false),
		published("v27.2b1", true, true), incomplete, published("v26.9", false)];
	assert.equal(newestRelease(releases, "development").version, "v26.11b10");
	assert.equal(newestRelease(releases, "stable").version, "v26.12");
	assert.equal(newestRelease([incomplete], "development"), null);
	assert.equal(newestRelease(releases, "bundled"), null);
});

test("checks all release pages and uses only the fixed local asset endpoint", async () => {
	const calls = [];
	const selected = await lookupRelease("development", async (url) => {
		calls.push(url);
		return Response.json(url.endsWith("page=1") ? Array(100).fill(published("v26.10b1", true)) :
			[published("v26.11b1", true)]);
	});
	assert.equal(calls.length, 2);
	assert.equal(selected.version, "v26.11b1");
	const requested = [];
	const fw = await loadBundledRelease(selected, "../", fb, async (url) => {
		requested.push(url);
		return new Response(url.endsWith(".json") ? JSON.stringify(manifest) : bytes);
	});
	assert.equal(fw.origin, "github");
	assert.deepEqual(requested, [`../_dm32plus_release/${name}.json`, `../_dm32plus_release/${name}.bin`]);
	await assert.rejects(lookupRelease("stable", async () => new Response("", { status: 502 })), /unavailable/);
});

test("Testing selects only RC prereleases and checks the unchanged firmware manifest", async () => {
	const candidate = (version) => ({ ...published(version, true), tag_name: "RC-dm32plus-" + version });
	const releases = [published("v26.12b4", true), candidate("v26.11b1"), candidate("v26.10b9")];
	assert.equal(newestRelease(releases, "development").version, "v26.12b4");
	assert.equal(newestRelease(releases, "stable"), null);
	const selected = await lookupRelease("testing", async () => Response.json(releases));
	assert.equal(selected.version, "v26.11b1");
	const requested = [];
	const fw = await loadBundledRelease(selected, "../", fb, async (url) => {
		requested.push(url);
		return new Response(url.endsWith(".json") ? JSON.stringify(manifest) : bytes);
	});
	assert.equal(fw.source, "RC DM32Plus v26.11b1");
	assert.deepEqual(requested, [`../_dm32plus_release/RC/${name}.json`, `../_dm32plus_release/RC/${name}.bin`]);
});

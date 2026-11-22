// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
import { test } from "node:test";
import assert from "node:assert/strict";
import { Selection } from "../static/js/flasher/select.js";
import { publishedSelection, buildSelectionKey, checkCustomManifest } from "../static/js/flasher/custom-build.js";

const radio = { id: "sample", config: { full: ["LINK", "A", "B"], reduced: ["A"] }, required: ["LINK"], unsupported: [] };
const features = ["LINK", "A", "B"].map(name => ({ name, needs: { all: name === "B" ? ["A"] : [], any: [] } }));
const selection = () => { const s = new Selection(radio, features, []); s.useVariant("full"); return s; };

test("Full uses the published image; a disabled dependency needs a custom build", () => {
	const s = selection(); assert.equal(publishedSelection(s), true);
	s.set("A", false); assert.deepEqual([...s.on], ["LINK"]); assert.equal(publishedSelection(s), false);
	s.set("LINK", false); assert.equal(s.on.has("LINK"), true);
	s.useVariant("reduced"); assert.equal(s.on.has("LINK"), true);
});

test("Changing version or features invalidates an in-flight image", () => {
	const s = selection(), old = buildSelectionKey(s, "v1.0.0");
	assert.notEqual(old, buildSelectionKey(s, "v1.0.1"));
	s.set("B", false); assert.notEqual(old, buildSelectionKey(s, "v1.0.0"));
});

test("A valid hash alone does not authorize the wrong product, version, or selected options", () => {
	const s = selection(), product = { name: "Sample", board: "sample" };
	const m = { product: "Sample", board: "sample", version: "v1.0.0", source_tag: "v1.0.0", variant: "custom",
		codec: { included: false }, selected_features: ["A", "B", "LINK"], features: "A,B,LINK" };
	assert.doesNotThrow(() => checkCustomManifest(m, s, product, "v1.0.0"));
	for (const edit of [{ product: "Other" }, { version: "v1.0.1" }, { source_tag: "wrong" }, { features: "A,LINK" },
		{ selected_features: ["A", "LINK"] }, { codec: { included: true } }])
		assert.throws(() => checkCustomManifest({ ...m, ...edit }, s, product, "v1.0.0"));
});

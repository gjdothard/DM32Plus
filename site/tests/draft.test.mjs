// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
import assert from "node:assert/strict";
import { test } from "node:test";
import { DraftSaver } from "../static/js/editor/draft.js";

test("rapid edits persist the latest snapshot and only report saved after completion", async () => {
	let value = "first";
	const writes = [], states = [];
	const saver = new DraftSaver({
		snapshot: () => value,
		save: async snapshot => { writes.push(snapshot); return true; },
		update: state => states.push(state),
	});
	saver.schedule();
	value = "second";
	saver.schedule();
	assert.equal(states.at(-1), "saving");
	await saver.flush();
	assert.deepEqual(writes, ["second"]);
	assert.equal(states.at(-1), "saved");
});

test("a replacement waits for an older write and cannot be overwritten by it", async () => {
	let value = "old", release;
	const writes = [], states = [];
	const saver = new DraftSaver({
		snapshot: () => value,
		async save(snapshot) {
			if (snapshot === "old") await new Promise(resolve => { release = resolve; });
			writes.push(snapshot);
			return true;
		},
		update: state => states.push(state),
	});
	saver.schedule();
	saver.flush();
	await Promise.resolve();
	value = "replacement";
	saver.schedule();
	const done = saver.flush();
	release();
	await done;
	assert.deepEqual(writes, ["old", "replacement"]);
	assert.deepEqual(states, ["saving", "saving", "saved"]);
});

test("storage rejection and encoding failure report unavailable and later edits can recover", async () => {
	let failSnapshot = false, failStorage = false;
	const states = [];
	const saver = new DraftSaver({
		snapshot() {
			if (failSnapshot) throw new Error("invalid model");
			return "draft";
		},
		async save() {
			if (failStorage) throw new Error("quota exceeded");
			return true;
		},
		update: state => states.push(state),
	});
	failSnapshot = true;
	saver.schedule();
	await saver.flush();
	assert.equal(states.at(-1), "unavailable");
	failSnapshot = false;
	failStorage = true;
	saver.schedule();
	await saver.flush();
	assert.equal(states.at(-1), "unavailable");
	failStorage = false;
	saver.schedule();
	await saver.flush();
	assert.equal(states.at(-1), "saved");
});

test("a disabled draft store reports unavailable without treating it as a completed save", async () => {
	const states = [];
	const saver = new DraftSaver({ snapshot: () => "draft", save: async () => false, update: state => states.push(state) });
	saver.schedule();
	await saver.flush();
	assert.deepEqual(states, ["saving", "unavailable"]);
});

test("restoring a draft cancels pending work and retains its saved status", async () => {
	const writes = [], states = [];
	const saver = new DraftSaver({
		snapshot: () => "blank",
		save: async snapshot => writes.push(snapshot),
		update: state => states.push(state),
	});
	saver.schedule();
	saver.restored();
	await saver.flush();
	assert.deepEqual(writes, []);
	assert.equal(states.at(-1), "saved");
});

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
import assert from "node:assert/strict";
import { test } from "node:test";
import { guardDialog } from "../static/js/editor/unsaved.js";

function setup(t, { dirty = true, accept = false } = {}) {
	const previousWindow = globalThis.window, previousConfirm = globalThis.confirm;
	const window = new EventTarget(), dialog = new EventTarget();
	let closes = 0, prompts = 0;
	dialog.close = () => { closes++; dialog.dispatchEvent(new Event("close")); };
	globalThis.window = window;
	globalThis.confirm = () => { prompts++; return accept; };
	t.after(() => { globalThis.window = previousWindow; globalThis.confirm = previousConfirm; });
	const cancel = guardDialog(dialog, () => dirty, "Discard changes?");
	return { window, dialog, cancel, get closes() { return closes; }, get prompts() { return prompts; } };
}

test("Escape retains modified dialog contents when discard is declined", t => {
	const form = setup(t);
	const escape = new Event("cancel", { cancelable: true });
	form.dialog.dispatchEvent(escape);
	assert.equal(escape.defaultPrevented, true);
	assert.equal(form.prompts, 1);
	assert.equal(form.closes, 0);
});

test("Cancel and Escape both close modified dialogs after confirmation", t => {
	const form = setup(t, { accept: true });
	form.cancel();
	assert.equal(form.prompts, 1);
	assert.equal(form.closes, 1);
});

test("unchanged dialogs close without a discard prompt", t => {
	const form = setup(t, { dirty: false });
	form.dialog.dispatchEvent(new Event("cancel", { cancelable: true }));
	assert.equal(form.closes, 1);
	assert.equal(form.prompts, 0);
});

test("unsaved dialog work blocks navigation until the dialog closes", t => {
	const form = setup(t);
	const leaving = new Event("beforeunload", { cancelable: true });
	form.window.dispatchEvent(leaving);
	assert.equal(leaving.defaultPrevented, true);
	form.dialog.close();
	const after = new Event("beforeunload", { cancelable: true });
	form.window.dispatchEvent(after);
	assert.equal(after.defaultPrevented, false);
});

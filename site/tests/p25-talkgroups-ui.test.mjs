// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
import assert from "node:assert/strict";
import { test } from "node:test";
import { empty, newContact } from "../static/js/codeplug/model.js";
import { channelForm } from "../static/js/editor/views.js";

class Element {
	constructor(tag) { this.tag = tag; this.children = []; this.events = {}; this.attrs = {}; this.value = ""; }
	append(...children) { for (const child of children) { this.children.push(child); if (child instanceof Element) child.parentElement = this; } }
	replaceChildren(...children) { this.children = []; this.append(...children); }
	setAttribute(key, value) { this.attrs[key] = value; if (key === "value") this.value = value; }
	addEventListener(key, fn) { this.events[key] = fn; }
	querySelectorAll(tag) { return this.children.flatMap(child => child instanceof Element ? [...(child.tag === tag ? [child] : []), ...child.querySelectorAll(tag)] : []); }
	get textContent() { return this.children.map(child => child instanceof Element ? child.textContent : child).join(""); }
	set textContent(value) { this.children = [value]; }
	focus() {}
}

test("shared editor filters protocols, hides P25 timeslots and creates P25 groups", () => {
	globalThis.Node = Element;
	globalThis.document = { createElement: tag => new Element(tag) };
	try {
		const model = empty(); model.sharedTalkgroups = true;
		model.contacts.push(newContact({ name: "DMR group", id: 1234 }),
			newContact({ name: "P25 group", id: 1234, protocol: "p25" }),
			newContact({ name: "Too large", id: 70000, protocol: "p25" }));
		const draft = [], channel = { mode: "dmr", kind: 0 };
		const field = channelForm(model, key => key, draft).find(item => item.key === "talkgroupRefs");
		const panel = field.render("groups", [], channel);
		const slot = panel.querySelectorAll("select")[0];
		assert.equal(slot.parentElement.hidden, false);
		assert.match(panel.textContent, /DMR group/);
		assert.doesNotMatch(panel.textContent, /P25 group/);
		channel.kind = 3; panel.syncTalkGroups();
		assert.equal(slot.parentElement.hidden, true);
		assert.match(panel.textContent, /P25 group/);
		assert.doesNotMatch(panel.textContent, /DMR group|Too large/);
		const inputs = panel.querySelectorAll("input");
		inputs.find(input => input.attrs.maxlength).value = "New group";
		const id = inputs.find(input => input.attrs.inputmode === "numeric");
		const create = panel.querySelectorAll("button").find(button => button.textContent === "editor.tg.create_assign");
		id.value = "70000"; create.events.click(); assert.equal(draft.length, 0);
		id.value = "2345"; create.events.click();
		assert.equal(draft[0].protocol, "p25");
		assert.equal(draft[0].timeslot, 0);
		assert.equal(panel.readTalkGroups(channel)[0], draft[0]);
	} finally { delete globalThis.Node; delete globalThis.document; }
});

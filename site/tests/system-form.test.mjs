// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
import assert from "node:assert/strict";
import { test } from "node:test";
import { empty } from "../static/js/codeplug/model.js";
import { newSystem } from "../static/js/codeplug/channelmodes.js";
import { h } from "../static/js/flasher/dom.js";
import { systemForm } from "../static/js/editor/modesview.js";

class Element {
	constructor(tag) { this.tag = tag; this.children = []; this.attrs = {}; this.events = {}; }
	append(...children) { this.children.push(...children); }
	replaceChildren(...children) { this.children = children; }
	setAttribute(key, value) { this.attrs[key] = value; if (key === "value") this.value = value; }
	addEventListener(key, fn) { this.events[key] = fn; }
	all(tag) { return this.children.flatMap(child => child instanceof Element ? [...(child.tag === tag ? [child] : []), ...child.all(tag)] : []); }
}

function setup(run) {
	globalThis.Node = Element;
	globalThis.document = { createElement: tag => new Element(tag) };
	try {
		const model = empty(), system = newSystem(model, 3, "Synthetic system");
		system.entries = [
			{ cls: 0, group: 0, flags: 0, control: true, rxHz: 380075000, txHz: 0, iden: 0, chanNo: 0 },
			{ cls: 1, group: 0, flags: 0, control: false, siteName: "North", rfss: 1, site: 3 },
			{ cls: 99, group: 0, flags: 0, control: false, raw: "preserve" },
		];
		const api = { model, t: key => key, h, button: (text, fn) => h("button", { onclick: fn }, text) };
		run(systemForm(api, system), system);
	} finally {
		delete globalThis.Node;
		delete globalThis.document;
	}
}

test("system form gives every table its own section and keeps identity and transmit fields reachable", () => setup(fields => {
	let section;
	const locations = new Map();
	for (const field of fields) {
		if (field.tab) section = field.label;
		if (field.key) locations.set(field.key, section);
	}
	assert.equal(locations.get("name"), "editor.modes.sec.identity");
	assert.equal(locations.get("transmit"), "editor.modes.sec.radio");
	assert.equal(locations.get("sys.tbl0"), "editor.modes.class.control");
	assert.equal(locations.get("sys.tbl1"), "editor.modes.class.sites");
	assert.equal(locations.get("sys.tbl2"), "editor.modes.class.plan");
}));

test("entry edits across tabs are staged, validated independently, and preserve unknown classes", () => setup((fields, system) => {
	const tables = fields.filter(field => field.type === "custom");
	const control = tables.find(field => field.key === "sys.tbl0");
	const sites = tables.find(field => field.key === "sys.tbl1");
	const frequency = control.render().all("input").find(input => input.attrs["aria-label"] === "editor.col.rx");
	frequency.value = "381.25";
	frequency.events.input();
	const siteName = sites.render().all("input").find(input => input.attrs["aria-label"] === "editor.p25.site_name");
	siteName.value = "North Tower";
	siteName.events.input();
	assert.equal(system.entries[0].rxHz, 380075000);
	assert.equal(system.entries[1].siteName, "North");
	const work = { ...system };
	for (const field of tables) assert.deepEqual(field.parse(work), []);
	assert.equal(work.entries.find(entry => entry.cls === 0).rxHz, 381250000);
	assert.equal(work.entries.find(entry => entry.cls === 1).siteName, "North Tower");
	assert.equal(work.entries.find(entry => entry.cls === 99), system.entries[2]);
	frequency.value = "invalid";
	frequency.events.input();
	assert.match(control.parse(work)[0], /editor.col.rx/);
	assert.deepEqual(sites.parse(work), []);
}));

test("adding and removing rows on one tab does not discard another tab's entries", () => setup((fields, system) => {
	const tables = fields.filter(field => field.type === "custom");
	const control = tables.find(field => field.key === "sys.tbl0");
	const table = control.render();
	table.all("button").find(button => button.children[0] === "+ editor.modes.add_control").events.click();
	assert.equal(table.all("tr").length, 3);
	const remove = table.all("button").filter(button => button.attrs["aria-label"] === "editor.modes.remove_row");
	remove[1].events.click();
	const work = { ...system };
	for (const field of tables) assert.deepEqual(field.parse(work), []);
	assert.equal(work.entries.length, 3);
	assert.equal(work.entries.find(entry => entry.cls === 1).siteName, "North");
	assert.equal(system.entries.length, 3);
}));

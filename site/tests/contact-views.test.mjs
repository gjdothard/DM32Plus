// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
import assert from "node:assert/strict";
import { test } from "node:test";
import { empty, newContact, newChannel, encode, decode, remove } from "../static/js/codeplug/model.js";
import { CONTACT_VIEWS, contactView, contactRows, contactViewFor, contactColumns, contactEditError } from "../static/js/editor/contact-views.js";
import { columns, recordForm, channelForm } from "../static/js/editor/views.js";
import { Grid, stageEdits } from "../static/js/editor/grid.js";
import { History } from "../static/js/editor/undo.js";
import { setKind } from "../static/js/codeplug/channelmodes.js";
import { addContacts } from "../static/js/codeplug/sources/output.js";

const t = key => key;
function fixture() {
	const model = empty();
	model.sharedTalkgroups = true;
	model.contacts = [
		newContact({ number: 4, name: "DMR", id: 91 }),
		newContact({ number: 8, name: "P25", protocol: "p25", id: 1201 }),
		newContact({ number: 12, name: "Shared", protocol: "both", id: 3100 }),
		newContact({ number: 15, name: "Private", type: "private", id: 1234567 }),
		newContact({ number: 19, name: "All call", type: "all", id: 0xFFFFFF }),
	];
	return model;
}
const column = (model, key) => columns("contacts", model, t).find(c => c.key === key);

test("contact pages partition groups and private calls without copying or renumbering records", () => {
	const model = fixture();
	delete model.contacts[0].protocol;
	assert.deepEqual(CONTACT_VIEWS.map(view => view.key), ["all", "dmr", "p25", "private", "shared"]);
	for (const [key, index] of [["dmr", 0], ["p25", 1], ["shared", 2], ["private", 3]]) {
		assert.deepEqual(contactRows(model, key), [model.contacts[index]]);
		assert.equal(contactRows(model, key)[0], model.contacts[index]);
		assert.equal(contactViewFor(model.contacts[index]), key);
	}
	assert.equal(contactRows(model, "all").length, 5);
	assert.equal(contactViewFor(model.contacts[4]), "all");
	assert.equal(contactView("unknown").key, "all");
	assert.deepEqual(model.contacts.map(c => c.number), [4, 8, 12, 15, 19]);
});

test("creation defaults, protocol labels and columns match each contact page", () => {
	const model = fixture(), all = columns("contacts", model, t);
	for (const key of ["dmr", "p25", "private", "shared"]) {
		const created = newContact({ id: 9, ...contactView(key).defaults });
		assert.equal(contactViewFor(created), key);
	}
	assert.ok(contactColumns(all, "dmr", t).some(c => c.key === "timeslot"));
	assert.ok(!contactColumns(all, "p25", t).some(c => c.key === "timeslot"));
	assert.ok(!contactColumns(all, "private", t).some(c => c.key === "timeslot"));
	assert.equal(contactColumns(all, "private", t).find(c => c.key === "id").label, "editor.contacts.unit_id");
	assert.equal(column(model, "protocol").options().find(([value]) => value === "both")[1], "editor.protocol.both");
	const timeslot = recordForm("contacts", model, t).find(field => field.key === "timeslot");
	assert.equal(timeslot.show(model.contacts[1]), false);
	assert.equal(timeslot.show(model.contacts[0]), true);
});

test("contact edits reject incompatible channel, VFO and RX-list assignments before mutation", () => {
	const model = fixture(), group = model.contacts[0];
	const channel = newChannel({ number: 1, name: "Assigned", talkgroupRefs: [group] });
	model.channels.push(channel);
	assert.equal(column(model, "protocol").set(group, "p25"), "editor.contacts.assigned");
	assert.equal(column(model, "type").set(group, "private"), "editor.contacts.assigned");
	assert.equal(group.protocol, "dmr");
	assert.equal(group.type, "group");
	assert.throws(() => remove(model, "contacts", contactRows(model, "dmr")), /assignments/);
	channel.talkgroupRefs = [];
	model.vfo = [newChannel({ name: "VFO", contact: group })];
	assert.equal(column(model, "protocol").set(group, "p25"), "editor.contacts.assigned");
	model.vfo = [];
	const list = { number: 1, name: "Listen", contacts: [group] };
	model.rxLists.push(list);
	assert.equal(column(model, "type").set(group, "private"), "editor.contacts.rx_list");
	channel.rxList = list;
	assert.equal(column(model, "protocol").set(group, "p25"), "editor.contacts.assigned");
});

test("P25 and shared group IDs stay 16-bit while private IDs remain 24-bit", () => {
	const model = fixture();
	for (const group of model.contacts.slice(1, 3)) {
		const old = group.id;
		assert.equal(column(model, "id").set(group, "65536"), "editor.err.range");
		assert.equal(group.id, old);
		assert.equal(column(model, "id").set(group, "65535"), undefined);
	}
	assert.equal(column(model, "id").set(model.contacts[3], "16777215"), undefined);
	assert.equal(column(model, "protocol").set(model.contacts[3], "p25"), undefined);
	assert.equal(column(model, "type").set(model.contacts[3], "group"), "editor.err.range");
	const work = { ...model.contacts[0], protocol: "p25", id: 70000 };
	assert.equal(contactEditError(model, model.contacts[0], work, t), "editor.err.range");
});

test("bulk edits are atomic when a later contact refuses reclassification", () => {
	const model = fixture();
	model.contacts[1].protocol = "dmr";
	model.channels.push(newChannel({ name: "Busy", talkgroupRefs: [model.contacts[1]] }));
	const rows = contactRows(model, "dmr");
	const result = stageEdits(rows, column(model, "protocol"), "p25");
	assert.equal(result.error, "editor.contacts.assigned");
	assert.equal(result.copies, undefined);
	assert.ok(rows.every(row => row.protocol === "dmr"));
});

test("filtering excludes hidden selections and refresh removes reclassified rows", () => {
	const model = fixture(), selected = new Set(model.contacts);
	const grid = Object.assign(Object.create(Grid.prototype), {
		rows: contactRows(model, "all"), columns: columns("contacts", model, t), selected, filter: "P25", sort: null,
	});
	assert.deepEqual(grid.selection(), [model.contacts[1]]);
	grid.filter = "";
	grid.rows = contactRows(model, "dmr");
	assert.deepEqual(grid.selection(), [model.contacts[0]]);
	column(model, "protocol").set(model.contacts[0], "p25");
	grid.render = () => {};
	grid.setRows(contactRows(model, "dmr"));
	assert.deepEqual(grid.selection(), []);
	assert.equal(grid.selected.has(model.contacts[0]), false);
});

test("undo, redo and binary save/reopen preserve contact identity, references and all-call records", () => {
	const model = fixture(), group = model.contacts[1], history = new History();
	const channel = newChannel({ number: 1, name: "P25 channel", rxOnly: true });
	model.channels.push(channel);
	setKind(model, channel, 2);
	channel.talkgroupRefs = [group];
	model.rxLists.push({ number: 1, name: "Listen", contacts: [group] });
	history.record(model);
	assert.equal(column(model, "protocol").set(group, "both"), undefined);
	assert.equal(contactRows(model, "shared").includes(group), true);
	history.undo(model);
	assert.equal(contactRows(model, "p25")[0], group);
	assert.equal(channel.talkgroupRefs[0], group);
	history.redo(model);
	assert.equal(contactRows(model, "shared").includes(group), true);
	const saved = decode(encode(model));
	const restored = saved.contacts.find(c => c.number === 8);
	assert.equal(saved.channels[0].talkgroupRefs[0], restored);
	assert.equal(saved.rxLists[0].contacts[0], restored);
	assert.equal(restored.protocol, "both");
	assert.deepEqual(saved.contacts.map(c => c.number), [4, 8, 12, 15, 19]);
	assert.equal(saved.contacts.find(c => c.number === 19).type, "all");
	const picker = channelForm(saved, t).find(field => field.key === "talkgroupRefs");
	assert.ok(picker.contacts.includes(restored));
});

test("RadioID imports remain private contacts in the shared table", () => {
	const model = fixture();
	const before = model.contacts.length;
	const result = addContacts(model, [{ id: 7654321, call: "TEST", name: "Test User", last: "User", city: "Town", state: "", country: "" }], { template: "{call}", upper: false });
	assert.equal(result.added, 1);
	assert.equal(model.contacts.length, before + 1);
	assert.ok(contactRows(model, "private").some(contact => contact.id === 7654321));
});

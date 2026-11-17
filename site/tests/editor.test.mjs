// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Garrett Dothard
// The Codeplug Editor's record forms (static/js/editor/views.js): what a field shows and what typing into it stores.
// Run with site/tests/run.sh.

import { test } from "node:test";
import assert from "node:assert/strict";

import { bulkColumns, channelForm, columns, recordForm, visibleChannelColumns, DEFAULT_CHANNEL_COLUMNS } from "../static/js/editor/views.js";
import { nextTab } from "../static/js/editor/navigation.js";
import { empty } from "../static/js/codeplug/model.js";
import { Grid } from "../static/js/editor/grid.js";

const t = (k) => (k === "editor.tot.default" ? "Radio Default" : k);
const field = (key) => channelForm(empty(), t).find((f) => f.key === key);

test("operating talkgroups use saved group contacts and preserve per-entry timeslots", () => {
	const model = empty();
	model.contacts.push(
		{ id: 3100, name: "Nationwide", type: "group", timeslot: 2 },
		{ id: 91, name: "Worldwide", type: "group", timeslot: 0 },
		{ id: 3100, name: "Duplicate alias", type: "group" },
		{ id: 1234567, name: "Private", type: "private" },
		{ id: 0xFFFFFF, name: "All call", type: "all" },
	);
	const f = channelForm(model, t).find((item) => item.key === "talkGroups");
	assert.deepEqual(f.contacts, model.contacts.slice(0, 3));
	const channel = { timeslot: 1 };
	const entries = f.resolve(f.contacts.slice(0, 2), channel);
	assert.equal(f.parse(channel, f.key, entries), undefined);
	assert.deepEqual(channel.talkGroups, [{ id: 3100, timeslot: 2 }, { id: 91, timeslot: 1 }]);
	assert.equal(f.parse(channel, f.key, [entries[0], entries[0]]), "editor.err.talkgroups");
});

test("channel TX timeout: 0 shows as the radio default, the rest in the 15 s steps the record stores", () => {
	const f = field("tot");
	assert.equal(f.format(0), "Radio Default");
	assert.equal(f.format(180), "180");
	const typed = (s) => {
		const r = { tot: 99 };
		return f.parse(r, "tot", s) ?? r.tot;
	};
	for (const s of ["", " ", "0", "Radio Default", "radio default", "default"]) assert.equal(typed(s), 0, JSON.stringify(s));
	assert.deepEqual(["1", "15", "16", "180", "3825"].map(typed), [15, 15, 30, 180, 3825]);
	for (const s of ["3826", "-5", "1.5", "3 min", "off"]) assert.equal(typed(s), "editor.err.tot", s);
});

test("an RX frequency in the air band or the broadcast band makes the channel RX only and names the band", () => {
	const freqs = channelForm(empty(), t).find((f) => f.key === "freqs").fields;
	const rx = freqs.find((f) => f.key === "rxHz");
	const typed = (s) => {
		const r = { rxHz: 446006250, txHz: 446006250, rxOnly: false };
		rx.parse(r, "rxHz", s);
		return r.rxOnly;
	};
	assert.deepEqual(["118.1", "98.5", "136.975", "145.5", "107.9"].map(typed), [true, true, true, false, true]);
	// TX goes to RX there, so no stray TX frequency stays behind; elsewhere TX is left as it was
	const tx = (s) => {
		const r = { rxHz: 446006250, txHz: 446006250, rxOnly: false };
		rx.parse(r, "rxHz", s);
		return r.txHz;
	};
	assert.deepEqual(["118.1", "145.5"].map(tx), [118100000, 446006250]);
	// the form's TX box and the RX/TX buttons are locked to RX while RX is in such a band
	const txField = freqs.find((f) => f.key === "txHz");
	const pair = channelForm(empty(), t).find((f) => f.key === "freqs");
	assert.equal(txField.follows, "rxHz");
	assert.deepEqual(["121.5", "462.5625"].map((v) => [txField.disabled(null, () => v), pair.disabled(null, () => v)]), [[true, true], [false, false]]);
	const note = channelForm(empty(), t).find((f) => f.key === "band");
	assert.deepEqual(["121.5", "88.1", "462.5625", "x"].map((v) => note.text(() => v)), ["editor.band.air", "editor.band.bcast", "", ""]);
	// the form's RX Only box follows the RX frequency as it is typed
	const rxOnly = field("rxOnly");
	assert.deepEqual(["121.5", "88.1", "462.5625", "x"].map((v) => rxOnly.implied(() => v)), [true, true, false, false]);
});

test("the channel table shows the mode the radio uses in a receive-only band", () => {
	const mode = columns("channels", empty(), t).find((c) => c.key === "mode");
	assert.deepEqual([[118100000, "fm"], [98500000, "fm"], [146520000, "fm"], [118100000, "dmr"]].map(([rxHz, m]) => mode.format(m, { rxHz, mode: m })),
		["editor.mode.air", "editor.mode.bcast", "FM", "DMR"]);
});

test("Bulk Edit sets no names or numbers, and has nothing to set on zones and RX lists", () => {
	const keys = (table) => bulkColumns(table, empty(), t).map((c) => c.key);
	assert.deepEqual([keys("zones"), keys("rxLists")], [[], []]);
	assert.ok(keys("channels").includes("power"));
	assert.ok(!keys("channels").includes("name") && !keys("contacts").includes("number"));
	assert.ok(keys("contacts").includes("type"));
});

test("the table filter and sort take every column's text, numbers too", () => {
	// a bare Grid: shown() filters and sorts without the DOM
	const m = empty();
	m.contacts.push({ number: 12, name: "Local", id: 9, type: "group", timeslot: 0 }, { number: 3, name: "World", id: 91, type: "group", timeslot: 0 });
	const g = Object.assign(Object.create(Grid.prototype), { columns: columns("contacts", m, t), rows: m.contacts, filter: "", sort: null });
	for (const c of g.columns) for (const r of g.rows) assert.equal(typeof g.text(c, r), "string", c.key);
	g.filter = "12";
	assert.deepEqual(g.shown().map((r) => r.name), ["Local"]);
	g.filter = "";
	g.sort = { key: "number", dir: 1 };
	assert.deepEqual(g.shown().map((r) => r.number), [3, 12]);
});

test("channel column preferences retain the pinned name and ignore unknown fields", () => {
	const all = columns("channels", empty(), t);
	assert.deepEqual(visibleChannelColumns(all).map(column => column.key), DEFAULT_CHANNEL_COLUMNS);
	const chosen = visibleChannelColumns(all, ["power", "unknown"]);
	assert.deepEqual(chosen.map(column => column.key), ["name", "power"]);
	assert.equal(chosen[0].pinned, true);
	assert.deepEqual(visibleChannelColumns(all, null).map(column => column.key), DEFAULT_CHANNEL_COLUMNS);
});

test("channel filtering still finds values in hidden columns", () => {
	const all = columns("channels", empty(), t);
	const g = Object.assign(Object.create(Grid.prototype), {
		columns: all.filter(column => column.key === "name"), searchColumns: all,
		rows: [{ name: "Simplex", mode: "fm", txTone: "88.5" }], filter: "88.5", sort: null,
	});
	assert.equal(g.shown().length, 1);
});

test("record forms keep channel-default APRS frequencies and existing all-call IDs readable", () => {
	const aprs = recordForm("aprs", empty(), t).find(field => field.key === "txHz");
	assert.equal(aprs.format(0), "");
	const profile = { txHz: 145000000 };
	assert.equal(aprs.parse(profile, "txHz", ""), undefined);
	assert.equal(profile.txHz, 0);
	const id = recordForm("contacts", empty(), t).find(field => field.key === "id");
	const all = { type: "all", id: 0 };
	assert.equal(id.disabled(all), true);
	assert.equal(id.parse(all, "id", "0"), undefined);
	assert.equal(all.id, 0);
	const group = { type: "group", id: 9 };
	assert.ok(id.parse(group, "id", "bad"));
	assert.equal(group.id, 9);
});

test("tab keyboard navigation wraps and supports Home and End", () => {
	assert.equal(nextTab("ArrowRight", 3, 4), 0);
	assert.equal(nextTab("ArrowLeft", 0, 4), 3);
	assert.equal(nextTab("Home", 2, 4), 0);
	assert.equal(nextTab("End", 0, 4), 3);
	assert.equal(nextTab("Tab", 1, 4), null);
	assert.equal(nextTab("ArrowRight", 0, 0), null);
});

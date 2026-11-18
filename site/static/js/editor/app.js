// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Garrett Dothard
// The Codeplug Editor page: a codeplug from a file or a radio, edited in tables and forms, saved to a file or
// written back to the radio. Everything runs in the page; the codeplug never leaves it unless the user puts it in a
// bug report (report.js). The working codeplug is kept in this browser (IndexedDB) so a reload doesn't lose edits.

import { h, t, ROOT, HOME, button, notice, download, progressBar, num as fmtNum, date as fmtDate } from "../flasher/dom.js";
import { equal } from "../flasher/bytes.js";
import { SerialStream, serialSupported, RFOUNDRY_USB, browserErrorKey, knownRadioPort } from "../flasher/link.js";
import { CodeplugImage, IDDB_HEADER, REGIONS } from "../codeplug/image.js";
import { channelsUsingTalkgroups, decode, encode, empty, remove, validate, newChannel, newContact, numberAll, assignNumbers } from "../codeplug/model.js";
import { LIMITS, decodeGeneral } from "../codeplug/records.js";
import { CpsSerial, TransportError, estimate, keepIdentity, IDENTITY, iddbRanges } from "../codeplug/transport.js";
import { FastError } from "../codeplug/fastlink.js";
import { convert as convertDb, readHeader as dbHeader } from "../codeplug/iddb.js";
import { convertIddb, iddbEnd } from "../codeplug/sources/output.js";
import { convertOperatingTalkgroups } from "../codeplug/convert-talkgroups.js";
import { FakeCpsRadio } from "../codeplug/fakecps.js";
import product from "../product.js";
import { Grid } from "./grid.js";
import { History } from "./undo.js";
import { DraftSaver, isUnusedDraft } from "./draft.js";
import { keyboardTabs } from "./navigation.js";
import { bulkColumns, columns, channelForm, recordForm, visibleChannelColumns, DEFAULT_CHANNEL_COLUMNS, formDialog, membersDialog, refLabel } from "./views.js";
import { openFile, saveBytes, tftpBytes, FileError, imageFrom, saveDraft, loadDraft } from "./files.js";
import { extensions } from "./extensions.js";
import "./parts.js";
import * as diag from "../diag.js";
import { initReporting, reportedLine } from "../report.js";
import { updateFor } from "../flasher/version.js";
import { encryptionTab } from "./encryption.js";

const CABLES = [0x0403, 0x1A86, 0x10C4, 0x067B].map((usbVendorId) => ({ usbVendorId }));
const TABS = ["radio", "encryption", "channels", "zones", "contacts", "rxLists", "dtmfContacts", "aprs", "iddb", "problems"];
const TABLE_MAX = { channels: LIMITS.channels, zones: LIMITS.zones, contacts: LIMITS.contacts, rxLists: LIMITS.rxLists, dtmfContacts: LIMITS.dtmfContacts, aprs: LIMITS.aprs };

const S = {
	model: null,
	started: false,
	history: new History(),
	fileName: "codeplug",
	dirty: false,
	tab: TABS.includes(new URLSearchParams(location.search).get("tab")) ? new URLSearchParams(location.search).get("tab") : "channels",
	grid: null,
	radios: [],
	conn: null,		// {transport, radio, image: the radio's bytes as last read or written, cable}
	lastRadioImage: null,	// the last connection's image, for a bug report after it closed
	iddbChanged: false,
	sim: location.hash.startsWith("#sim"),		// #sim-foreign: a radio still holding stock data in its settings area
	filter: {},
};

const root = document.getElementById("editor");
const toolbar = h("div", { class: "ed-toolbar" });
const status = h("div", { class: "ed-status small muted", "aria-live": "polite" });
const tabs = h("nav", { class: "ed-tabs", role: "tablist", "aria-label": t("editor.tabs") });
const view = h("div", { class: "ed-view", id: "editor-panel", role: "tabpanel" });
const connectionBar = h("div", { class: "ed-connection", "aria-live": "polite" });
const draftStatus = h("span", { class: "ed-draft small", "aria-live": "polite" });
const editorHead = h("div", { class: "ed-head" }, toolbar, h("div", { class: "ed-save-status" }, status, draftStatus));
const fileInput = h("input", { type: "file", accept: ".bin,.zip,.yaml,.yml,.csv,.txt", hidden: true, onchange: async () => {
	const file = fileInput.files?.[0];
	fileInput.value = "";
	if (file) await openPicked(file.name, new Uint8Array(await file.arrayBuffer()));
} });
const welcome = h("section", { class: "ed-start", hidden: true, "aria-labelledby": "ed-start-title" },
	h("h2", { id: "ed-start-title" }, t("editor.start.title")),
	h("div", { class: "ed-start-actions" }, ...[
		{ key: "read", label: t("editor.read"), action: readRadio, primary: true },
		{ key: "open", label: t("editor.open"), action: () => fileInput.click() },
		{ key: "create", label: t("editor.start.create"), action: newCodeplug },
	].map(({ key, label, action, primary }) =>
		h("button", { type: "button", class: "btn ed-start-action" + (primary ? " btn-primary" : ""),
			"data-action": "start-" + key, "aria-label": label, "aria-describedby": "ed-start-" + key, onclick: action },
			h("strong", {}, label), h("span", { id: "ed-start-" + key }, t("editor.start." + key + "_help"))))));
let draftState = "saving", connecting = false, connectionLost = false;
let encryptionView, encryptionConnection, radioView, radioModel;
let channelColumns = DEFAULT_CHANNEL_COLUMNS;
try {
	const saved = JSON.parse(localStorage.getItem("dm32plus.channelColumns"));
	if (Array.isArray(saved)) channelColumns = saved;
} catch { /* Column preferences are optional. */ }
keyboardTabs(tabs, control => show(control.dataset.tab));
const messages = h("div", { class: "ed-messages", "aria-live": "polite" });

// --- Model changes ---

function load(model, name, { fromRadio = false, dirty = false, restored = false, started = true } = {}) {
	S.model = model;
	S.started = started;
	S.fileName = name;
	S.history.reset();
	S.dirty = dirty;
	S.iddbChanged = false;
	S.iddbSkipped = null;
	if (!fromRadio && S.conn) S.conn.dbKnown = false;
	refresh();
	if (!started) return;
	if (restored) drafts.restored();
	else draftSoon();
}

// Run fn as one undoable change
function change(label, fn) {
	S.history.record(S.model, label);
	const r = fn();
	if (r === false) {
		S.history.cancel();
		return false;
	}
	S.dirty = true;
	S.started = true;
	refresh();
	draftSoon();
	return true;
}

const drafts = new DraftSaver({
	snapshot: () => ({
		bytes: saveBytes(encode(S.model)), name: S.fileName, dirty: S.dirty, started: S.started, at: new Date().toISOString(),
	}),
	save: saveDraft,
	update(state) {
		draftState = state;
		draftStatus.textContent = t("editor.draft." + state);
		draftStatus.classList.toggle("ed-draft-unavailable", state === "unavailable");
		draftStatus.title = state === "unavailable" ? t("editor.draft.help") : "";
	},
});
function draftSoon() {
	drafts.schedule();
}
document.addEventListener("visibilitychange", () => {
	if (document.visibilityState === "hidden") drafts.flush();
});
window.addEventListener("pagehide", () => drafts.flush());
window.addEventListener("beforeunload", event => {
	if (!S.started || draftState === "saved") return;
	event.preventDefault();
	event.returnValue = "";
});
let openingFile = false;

// report: a failure worth a bug report gets the way to make one
// report: the record of the error shown (diag.error's return): under the message, that it was sent automatically, or
// how to report it
function say(text, kind = "info", { report = null } = {}) {
	if (kind === "bad") diag.note(`shown: ${text}`);
	const n = h("div", { class: `notice ${kind}` }, h("p", {}, text));
	const line = report ? reportedLine(report) : null;
	if (line) n.append(line);
	const close = button("×", () => n.remove(), { quiet: true });
	close.setAttribute("aria-label", t("editor.dismiss"));
	n.prepend(close);
	close.classList.add("ed-dismiss");
	messages.replaceChildren(n);
}

// --- Rendering ---

function problems() {
	return validate(S.model, { bands: S.conn?.radio?.bands_hz || null });
}

function refresh() {
	editorHead.hidden = tabs.hidden = view.hidden = !S.started;
	welcome.hidden = S.started;
	if (!S.started) {
		renderConnection();
		return;
	}
	const p = problems();
	const errors = p.filter((x) => x.level === "error").length;
	renderToolbar(errors);
	const counts = {
		channels: S.model.channels.length, zones: S.model.zones.length, contacts: S.model.contacts.length,
		rxLists: S.model.rxLists.length, dtmfContacts: S.model.dtmfContacts.length, aprs: S.model.aprs.length,
		iddb: S.model.iddb?.count || 0, problems: p.length,
	};
	for (const key of TABS) {
		let tab = tabs.querySelector('[data-tab="' + key + '"]');
		if (!tab) {
			tab = h("button", { type: "button", role: "tab", id: "editor-tab-" + key, "data-tab": key,
				"aria-controls": view.id, onclick: () => show(key) });
			tabs.append(tab);
		}
		tab.setAttribute("aria-selected", String(S.tab === key));
		tab.tabIndex = S.tab === key ? 0 : -1;
		tab.classList.toggle("has-errors", key === "problems" && errors > 0);
		const children = [t("editor.tab." + key)];
		if (key in counts) children.push(h("span", { class: "ed-count" }, String(counts[key])));
		tab.replaceChildren(...children);
	}
	view.setAttribute("aria-labelledby", "editor-tab-" + S.tab);
	status.textContent = [S.fileName, S.dirty ? t("editor.status.unsaved") : t("editor.status.saved")].join(" · ");
	renderConnection();
	renderView(p);
}

function renderConnection() {
	const connected = S.conn && !S.conn.transport.lost;
	const version = S.conn?.probe?.version?.release || S.conn?.probe?.release || S.conn?.probe?.git || "?";
	const text = connecting ? t("editor.connection.connecting") : connected ?
		t("editor.connection.connected", { radio: S.conn.radio.name, version }) :
		t(connectionLost ? "editor.connection.lost" : "editor.connection.disconnected");
	const toggle = button(t(connected ? "editor.connection.disconnect" : "editor.connection.connect"), () => exclusive(async () => {
		try {
			if (connected) await dropConnection();
			else {
				await connect();
				refresh();
			}
		} catch (error) { await fail("connect radio", error); }
	}), { disabled: connecting || radioBusy });
	connectionBar.replaceChildren(h("span", {}, text), toggle);
}

function show(tab) {
	S.tab = tab;
	S.grid = null;
	refresh();
}

function renderToolbar(errors) {
	const save = h("details", { class: "ed-menu" }, h("summary", { class: "btn" }, t("editor.save")),
		h("div", { class: "ed-menu-list" },
			button(t("editor.save.bin"), () => saveFile("bin")),
			button(t("editor.save.tftp"), () => saveFile("tftp")),
			...extensions.saveItems(api()).map(([label, fn]) => button(label, fn))));
	save.addEventListener("click", (e) => {
		if (e.target.closest(".ed-menu-list button")) save.open = false;
	});

	toolbar.replaceChildren(
		h("div", { class: "ed-group" },
			button(t("editor.new"), newCodeplug),
			button(t("editor.open"), () => fileInput.click()),
			save),
		h("div", { class: "ed-group" },
			button(t("editor.read"), readRadio),
			button(t("editor.write"), writeRadio, { primary: true })),
		h("div", { class: "ed-group" },
			button(t("editor.undo"), undo, { disabled: !S.history.canUndo, quiet: true }),
			button(t("editor.redo"), redo, { disabled: !S.history.canRedo, quiet: true })),
	);
}

function renderView(p) {
	const tab = S.tab;
	if (tab !== "encryption" && view.firstChild === encryptionView) encryptionView.deactivate();
	if (tab === "radio") {
		if (!radioView || radioModel !== S.model) {
			radioModel = S.model;
			radioView = radioTab();
		}
		radioView.update();
		if (view.firstChild !== radioView) view.replaceChildren(radioView);
		return;
	}
	if (tab === "encryption") {
		if (!encryptionView || encryptionConnection !== S.conn) {
			encryptionView?.deactivate();
			encryptionConnection = S.conn;
			encryptionView = encryptionTab({ connection: S.conn, exclusive, isBusy: () => radioBusy });
		}
		if (view.firstChild !== encryptionView) {
			view.replaceChildren(encryptionView);
			encryptionView.activate();
		}
		return;
	}
	if (tab === "iddb") return view.replaceChildren(extensions.iddbTab(api()));
	if (tab === "problems") return view.replaceChildren(problemsTab(p));
	// a table: keep the grid (focus, sort, selection) when only its rows changed
	if (S.grid?.table === tab && S.grid.sharedTalkgroups === S.model.sharedTalkgroups) {
		S.grid.g.setRows(S.model[tab]);
		S.grid.update();
		return;
	}
	const allColumns = columns(tab, S.model, t);
	const g = new Grid({
		columns: tab === "channels" ? visibleChannelColumns(allColumns, channelColumns) : allColumns,
		searchColumns: allColumns, rows: S.model[tab], t, empty: t(`editor.empty.${tab}`),
		onChange: (row, col, value) => {
			let err = null;
			change(t("editor.undo.edit"), () => {
				err = col.set(row, value);
				return err ? false : undefined;
			});
			return err;
		},
		onSelect: () => S.grid?.update(),
		onOpen: record => openRecord(tab, record),
	});
	g.filter = S.filter[tab] || "";
	g.render();
	const filter = h("input", { type: "search", class: "ed-filter", placeholder: t("editor.filter"), "aria-label": t("editor.filter"), value: g.filter,
		oninput: (e) => {
			S.filter[tab] = e.target.value;
			g.setFilter(e.target.value);
		} });
	const actions = h("div", { class: "ed-actions" });
	const update = () => actions.replaceChildren(...tableActions(tab, g));
	S.grid = { table: tab, g, filter, update, sharedTalkgroups: S.model.sharedTalkgroups };
	update();
	const picker = tab === "channels" ? columnPicker(g, allColumns) : null;
	view.replaceChildren(h("div", { class: "ed-bar" }, actions, h("div", { class: "ed-table-tools" }, picker, filter)), g.el);
}

function columnPicker(grid, all) {
	const picker = h("details", { class: "ed-columns" }, h("summary", { class: "btn" }, t("editor.columns")));
	const choices = h("div", { class: "ed-column-choices" });
	for (const column of all) {
		const input = h("input", { type: "checkbox", checked: column.key === "name" || channelColumns.includes(column.key),
			disabled: column.key === "name", onchange: () => {
				const selected = new Set(channelColumns);
				if (input.checked) selected.add(column.key);
				else selected.delete(column.key);
				channelColumns = [...selected];
				try { localStorage.setItem("dm32plus.channelColumns", JSON.stringify(channelColumns)); }
				catch { /* This view remains usable when preferences cannot be stored. */ }
				grid.setColumns(visibleChannelColumns(all, channelColumns));
			} });
		choices.append(h("label", {}, input, column.label));
	}
	picker.append(choices);
	return picker;
}

function convertTalkgroups() {
	const model = S.model;
	let converted, report;
	try {
		const result = convertOperatingTalkgroups(encode(model).bytes);
		report = result.report;
		converted = decode(result.image);
	} catch (error) {
		diag.note("talkgroup conversion failed", { detail: error.message });
		say(t("editor.tg.convert_failed"), "bad");
		return;
	}
	const body = [h("p", {}, t("editor.tg.convert_summary", {
		assignments: report.assignments, contacts: report.created.length,
	}))];
	if (report.unrecognizedOverflow) body.push(notice(t("editor.tg.convert_overflow"), "warn"));
	dialog(t("editor.tg.convert"), body, t("editor.tg.convert"), (close) => {
		close();
		change(t("editor.tg.convert"), () => Object.assign(model, converted));
	});
}

function tableActions(tab, g) {
	const sel = g.selection();
	const full = S.model[tab].length >= TABLE_MAX[tab];
	const numbered = tab !== "zones";
	const add = button(t("editor.add"), () => addRecord(tab), { disabled: full });
	add.dataset.action = "add";
	const out = [
		// first, so it sits above the row checkboxes it acts on; not on a table with nothing it can set (zones, RX lists)
		bulkColumns(tab, S.model, t).length ? button(sel.length ? t("editor.bulk_edit_n", { n: sel.length }) : t("editor.bulk_edit"), () => bulkSet(tab, g), { disabled: sel.length < 2 }) : null,
		add,
		button(t("editor.duplicate"), () => duplicate(tab, g), { disabled: full || !g.targets().length }),
		button(t("editor.delete"), () => del(tab, g), { disabled: !g.targets().length, danger: true }),
		button(t("editor.move_up"), () => move(tab, g, -1), { disabled: !g.targets().length, quiet: true }),
		button(t("editor.move_down"), () => move(tab, g, 1), { disabled: !g.targets().length, quiet: true }),
	].filter(Boolean);
	if (numbered) out.push(button(t("editor.renumber"), () => renumber(tab), { quiet: true }));
	if (tab === "channels" && !S.model.sharedTalkgroups && S.model.channels.length)
		out.push(button(t("editor.tg.convert"), convertTalkgroups));
	out.push(...extensions.tableActions(tab, api()));
	return out;
}

// --- Table actions ---

function blank(tab) {
	const n = (base) => {
		const names = new Set(S.model[tab].map((x) => x.name));
		let i = S.model[tab].length + 1;
		const max = tab === "aprs" ? LIMITS.aprsName : LIMITS.name;
		const value = () => base.slice(0, Math.max(0, max - String(i).length - 1)) + " " + i;
		while (names.has(value())) i++;
		return value();
	};
	switch (tab) {
	case "channels": return newChannel({ name: n(t("editor.new.channel")) });
	case "contacts": return newContact({ name: n(t("editor.new.contact")), id: 9 });
	case "zones": return { name: n(t("editor.new.zone")), channels: [] };
	case "rxLists": return { number: null, name: n(t("editor.new.rx_list")), contacts: [] };
	case "dtmfContacts": return { number: null, name: n(t("editor.new.dtmf")), code: "0" };
	case "aprs": return { number: null, name: n("APRS"), ssid: 7, lat: 0, lon: 0, path1: "WIDE1-1", path2: "", symTable: "/", symCode: ">", comment: "", txHz: 0, baud300: false, useFixed: false, qsy: false, silent: false, ambiguity: 0 };
	}
}

function addRecord(tab) {
	openRecord(tab, blank(tab), { creating: true });
}

function duplicate(tab, g) {
	const src = g.targets();
	const room = TABLE_MAX[tab] - S.model[tab].length;
	change(t("editor.undo.duplicate"), () => {
		for (const r of src.slice(0, room)) {
			const c = { ...r, number: null, name: (r.name + " +").slice(0, tab === "aprs" ? LIMITS.aprsName : LIMITS.name) };
			for (const k of Object.keys(c)) if (Array.isArray(c[k])) c[k] = c[k].slice();
			S.model[tab].push(c);
		}
		if (tab !== "zones") assignNumbers(S.model[tab], TABLE_MAX[tab]);
	});
}

function del(tab, g) {
	const items = g.targets();
	if (!items.length) return;
	if (tab === "contacts") {
		const channels = channelsUsingTalkgroups(S.model, items);
		if (channels.length) {
			say(t("editor.tg.in_use", { channels: channels.map(channel => channel.name).join(", ") }), "bad");
			return;
		}
	}
	const go = () => change(t("editor.undo.delete"), () => remove(S.model, tab, items));
	if (items.length === 1) return go();
	confirmDialog(t("editor.delete_confirm", { n: items.length }), t("editor.delete"), go);
}

function move(tab, g, d) {
	const items = g.targets();
	change(t("editor.undo.move"), () => {
		if (tab === "zones") {
			const list = S.model.zones;
			const idx = items.map((x) => list.indexOf(x)).sort((a, b) => (d < 0 ? a - b : b - a));
			for (const i of idx) {
				const j = i + d;
				if (j < 0 || j >= list.length || items.includes(list[j])) continue;
				[list[i], list[j]] = [list[j], list[i]];
			}
			return;
		}
		// numbered records swap numbers with their neighbor in number order
		const list = [...S.model[tab]].sort((a, b) => a.number - b.number);
		const idx = items.map((x) => list.indexOf(x)).sort((a, b) => (d < 0 ? a - b : b - a));
		for (const i of idx) {
			const j = i + d;
			if (j < 0 || j >= list.length || items.includes(list[j])) continue;
			[list[i].number, list[j].number] = [list[j].number, list[i].number];
			[list[i], list[j]] = [list[j], list[i]];
		}
		S.model[tab].sort((a, b) => a.number - b.number);
	});
	g.sort = null;
	g.render();
}

function renumber(tab) {
	change(t("editor.undo.renumber"), () => {
		const list = [...S.model[tab]].sort((a, b) => a.number - b.number);
		list.forEach((r, i) => (r.number = i + 1));
		S.model[tab] = list;
	});
}

function bulkSet(tab, g) {
	const rows = g.selection();
	const cols = bulkColumns(tab, S.model, t);
	if (!cols.length) return;
	const pickCol = h("select", {}, ...cols.map((c, i) => h("option", { value: String(i) }, c.label)));
	const slot = h("div");
	const errs = h("p", { class: "notice bad", hidden: true });
	let input;
	const fill = () => {
		const c = cols[pickCol.selectedIndex];
		if (c.type === "select") {
			const opts = c.options(rows[0]);
			input = h("select", {}, ...opts.map(([, label], i) => h("option", { value: String(i) }, label)));
			input._opts = opts;
		} else if (c.type === "bool") {
			input = h("select", {}, h("option", { value: "1" }, t("editor.yes")), h("option", { value: "0" }, t("editor.no")));
		} else input = h("input", { type: "text" });
		slot.replaceChildren(h("label", {}, t("editor.value"), input));
	};
	pickCol.addEventListener("change", fill);
	fill();
	dialog(t("editor.bulk_edit_n", { n: rows.length }), [h("label", {}, t("editor.field"), pickCol), slot, errs], t("editor.apply"), (close) => {
		const c = cols[pickCol.selectedIndex];
		const v = c.type === "select" ? input._opts[input.selectedIndex][0] : c.type === "bool" ? input.value === "1" : input.value;
		let err = null;
		change(t("editor.undo.edit"), () => {
			for (const r of rows) {
				err = c.set(r, v);
				if (err) return false;
			}
		});
		if (err) {
			errs.hidden = false;
			errs.textContent = err;
			return;
		}
		close();
	});
}

function problemText(problem) {
	return t("editor.problem." + problem.code, {
		...problem.params, field: problem.field ? t("editor.field." + problem.field) : "",
	});
}

function openRecord(tab, record, { creating = false, problem = null } = {}) {
	const model = S.model;
	const title = creating ? t("editor.record.create", { name: record.name }) : t("editor.record.edit", { name: record.name });
	const options = {
		field: problem?.field, error: problem ? problemText(problem) : null,
		saveLabel: t(tab === "channels" ? "editor.form.save_channel" : "editor.save_record"),
		onClose() {
			if (model[tab].includes(record)) S.grid?.g.focusRow(record);
			else view.querySelector('[data-action="add"]')?.focus();
		},
	};
	const commit = (work, contacts = []) => {
		change(t(creating ? "editor.undo.add" : "editor.undo.edit"), () => {
			Object.assign(record, work);
			if (creating) model[tab].push(record);
			model.contacts.push(...contacts);
			if (tab !== "zones") assignNumbers(model[tab], TABLE_MAX[tab]);
			if (contacts.length) assignNumbers(model.contacts, LIMITS.contacts);
			if (creating && S.grid?.table === tab) {
				S.filter[tab] = "";
				S.grid.filter.value = "";
				S.grid.g.filter = "";
			}
		});
	};
	if (tab === "channels") {
		const draftContacts = [];
		formDialog(title, channelForm(model, t, draftContacts), record, t, work => {
			const created = work.mode !== "dmr" ? [] : draftContacts.filter(contact => model.sharedTalkgroups ?
				work.talkgroupRefs.includes(contact) : work.talkGroups.some(entry => entry.id === contact.id));
			commit(work, created);
		}, options);
	} else if (tab === "zones" || tab === "rxLists") {
		const zone = tab === "zones", key = zone ? "channels" : "contacts";
		const pool = zone ? [...model.channels].sort((a, b) => a.number - b.number) : model.contacts.filter(contact => contact.type === "group");
		membersDialog(title, record[key], pool, zone ? LIMITS.zoneChannels : LIMITS.rxListContacts, t,
			item => zone ? `${item.number} · ${item.name}` : `${item.name} (${item.id})`,
			(list, name) => commit({ ...record, name, [key]: list }), { ...options, name: record.name });
	} else {
		formDialog(title, recordForm(tab, model, t), record, t, work => commit(work), options);
	}
}

// --- Radio tab: the radio-wide settings ---

function radioTab() {
	const g = S.model.general, fields = new Map();
	const field = (key, label, max, help, parse) => {
		const input = h("input", { type: "text", value: g[key] ?? "", maxlength: max, id: "g-" + key, spellcheck: "false" });
		const err = h("span", { id: "g-" + key + "-error", class: "small ed-err" });
		input.setAttribute("aria-describedby", err.id);
		fields.set(key, { input, err });
		input.addEventListener("change", () => {
			const res = parse(input.value);
			if (res.error) {
				err.textContent = res.error;
				input.setAttribute("aria-invalid", "true");
				return;
			}
			err.textContent = "";
			input.removeAttribute("aria-invalid");
			change(t("editor.undo.edit"), () => (S.model.general = { ...S.model.general, [key]: res.value }));
		});
		return h("div", { class: "form-row" }, h("label", { for: "g-" + key }, label), input, help ? h("span", { class: "small muted" }, help) : null, err);
	};
	const text = (max) => (v) => (v.length > max ? { error: t("editor.err.too_long", { max }) } : { value: v });
	const num = (lo, hi) => (v) => {
		const n = Number(v.trim() || 0);
		return Number.isInteger(n) && n >= lo && n <= hi ? { value: n } : { error: t("editor.err.range", { min: lo, max: hi }) };
	};
	const parts = [
		h("div", { class: "form-grid ed-radio-form" },
			field("dmrId", t("editor.general.dmr_id"), 8, t("editor.general.dmr_id_help"), num(0, 0xFFFFFF)),
			field("radioName", t("editor.general.radio_name"), LIMITS.radioName, t("editor.general.radio_name_help"), text(LIMITS.radioName)),
			field("bootLine1", t("editor.general.boot1"), LIMITS.bootText, null, text(LIMITS.bootText)),
			field("bootLine2", t("editor.general.boot2"), LIMITS.bootText, null, text(LIMITS.bootText)),
			field("groupHang", t("editor.general.group_hang"), 3, t("editor.general.hang_help"), num(0, 255)),
			field("privateHang", t("editor.general.private_hang"), 3, t("editor.general.hang_help"), num(0, 255))),
	];
	const connected = h("div");
	const panel = h("div", {}, ...parts, connected);
	panel.update = () => {
		for (const [key, field] of fields) {
			if (field.input !== document.activeElement && !field.err.textContent) field.input.value = S.model.general[key] ?? "";
		}
		connected.replaceChildren();
		if (S.conn) connected.append(h("h3", {}, t("editor.connected_radio")), h("dl", { class: "specs" },
			h("dt", {}, t("editor.radio_model")), h("dd", {}, S.conn.radio.name),
			h("dt", {}, t("editor.firmware")), h("dd", {}, S.conn.probe?.version?.release || S.conn.probe?.git || "?")));
	};
	panel.showProblem = (key, message) => {
		const field = fields.get(key);
		if (!field) return;
		field.err.textContent = message;
		field.input.setAttribute("aria-invalid", "true");
		field.input.focus();
	};
	return panel;
}

// --- Problems tab ---

function problemsTab(p) {
	if (!p.length) return notice(t("editor.problems.none"), "ok");
	const rows = p.map((x) => {
		const what = x.item ? (x.item.number ? `${t(`editor.tab.${x.table}`)} ${x.item.number} · ${x.item.name}` : `${t(`editor.tab.${x.table}`)} · ${x.item.name}`) : t(`editor.tab.${x.table === "general" ? "radio" : x.table}`);
		const go = button(t("editor.problems.show"), () => {
			const tab = x.table === "general" ? "radio" : x.table;
			show(tab);
			if (x.item) openRecord(tab, x.item, { problem: x });
			else if (tab === "radio") radioView.showProblem(x.field, problemText(x));
			else say(problemText(x), x.level === "error" ? "bad" : "warn");
		}, { quiet: true });
		return h("tr", {}, h("td", {}, h("span", { class: `badge ${x.level === "error" ? "badge-bad" : "badge-testing"}` }, t(`editor.level.${x.level}`))),
			h("td", {}, what), h("td", {}, problemText(x)), h("td", {}, go));
	});
	return h("div", { class: "table-wrap" }, h("table", { class: "data" }, h("tbody", {}, ...rows)));
}

// --- Files ---

async function openPicked(name, bytes) {
	if (openingFile) return;
	openingFile = true;
	try {
		const f = await openFile(name, bytes);
		if (f.kind !== "foreign" && S.dirty && !confirm(t("editor.open_confirm", { name }))) return;
		if (f.kind === "image") {
			load(decode(f.image), name.replace(/\.[^.]+$/, ""));
			say(t("editor.opened", { name }), "ok");
		} else if (f.kind === "converted") {
			// another firmware's codeplug (a stock radio's backup, say): converted, never shown as raw bytes
			load(decode(f.image), name.replace(/\.[^.]+$/, ""));
			const c = f.counts;
			diag.note("codeplug converted", { from: f.from, ...c, notes: f.notes });
			say(t("editor.converted", { name, format: t(`editor.format.${f.from}`), channels: c.channels, zones: c.zones, contacts: c.contacts, rxLists: c.rxLists }),
				f.notes.length ? "warn" : "ok");
			if (f.notes.length) {
				dialog(t("editor.converted_left_out"), [h("ul", {}, ...f.notes.map((x) => h("li", {}, t(`editor.qdmr.note.${x.code}`, { n: x.n }))))],
					null, null, () => {}, { cancel: t("editor.close") });
			}
		} else if (f.kind === "foreign") {
			diag.note("codeplug not readable", { format: f.format, size: bytes.length });
			say(f.format === "tyt-uv380-rdt" ? t("editor.err.foreign_rdt") : t("editor.err.foreign", { format: t(`editor.format.${f.format}`) }), "bad");
		} else {
			const handled = await extensions.openText(f, name, api());
			if (!handled) say(t("editor.err.unknown_file", { name }), "bad");
		}
	} catch (e) {
		const rec = diag.error("open file", e, { name, size: bytes?.length });
		say(e instanceof FileError ? t(`editor.err.${e.code}`, { name }) : t("editor.err.open_failed", { error: e.message }), "bad", { report: rec });
	} finally {
		openingFile = false;
	}
}

function saveFile(kind) {
	const errors = problems().filter((p) => p.level === "error");
	if (errors.length) return say(t("editor.err.fix_first", { n: errors.length }), "bad");
	const img = encode(S.model);
	if (kind === "tftp") {
		download("codeplug.bin", tftpBytes(img, iddbEnd(S.conn?.radio)));
		say(t("editor.saved_tftp"), "info");
		return;
	}
	download(`${S.fileName || "codeplug"}.bin`, saveBytes(img));
	S.dirty = false;
	refresh();
	draftSoon();
}

function newCodeplug() {
	const go = () => {
		load(empty(), "codeplug");
	};
	if (S.dirty) confirmDialog(t("editor.new_confirm"), t("editor.new"), go);
	else go();
}

// --- Radio ---

async function connect() {
	// a connection whose port went away (a radio unplugged, or restarted after a write) is made again: kept, every
	// later read or write went to nothing and failed with no answer
	if (S.conn?.transport.lost) {
		diag.note("radio connection lost: connecting again");
		await dropConnection();
	}
	if (S.conn) return S.conn;
	connecting = true;
	renderConnection();
	try {
		let stream, cable = false;
		if (S.sim) {
			// #sim-compact: firmware that reads a format C ID database; &release=vYY.M: a radio that runs that release;
			// &git_time=N: a build of a commit from that time (Unix seconds)
			const hash = location.hash;
			const radio = S.radios.find((r) => r.id === product.board) || S.radios[0];
			S.fake = S.fake || new FakeCpsRadio({ board: radio.codeplug.names[0], base: radio.codeplug.base,
				end: radio.codeplug.end, image: CodeplugImage.blank().bytes, foreign: hash === "#sim-foreign",
				features: hash.startsWith("#sim-compact") ? "FM,DMR,IDDB_COMPACT" : "FM,DMR", release: hash.match(/[#&]release=([\w.-]+)/)?.[1],
				gitTime: Number(hash.match(/[#&]git_time=(\d+)/)?.[1]) || 0 });
			stream = S.fake.stream();
		} else {
			if (!serialSupported()) throw new TransportError("no_serial");
			// one chooser for both links: the radio's own USB port, or a programming cable, told apart by the port's USB ID.
			// A port granted before where RadioFoundry answers needs no chooser
			const known = await knownRadioPort([RFOUNDRY_USB, ...CABLES]);
			let port = known?.port;
			if (!port) {
				try {
					port = await navigator.serial.requestPort({ filters: [RFOUNDRY_USB, ...CABLES] });
				} catch (e) {
					if (e?.name !== "NotFoundError") throw e;
					say(t("editor.connect.none"), "warn");
					return null;
				}
			}
			const info = port.getInfo?.() || {};
			cable = !(info.usbVendorId === RFOUNDRY_USB.usbVendorId && info.usbProductId === RFOUNDRY_USB.usbProductId);
			diag.note(known ? "port known" : "port chosen", { cable, ...info });
			stream = known?.stream || await SerialStream.open(port);
		}
		const tr = new CpsSerial(stream, { cable });
		try {
			const { radio, probe } = await tr.identify(S.radios);
			diag.note("radio found", { radio: radio.id, board: probe.board, git: probe.git, features: probe.features });
			// dbKnown: the radio's ID database is the model's (read or written since the model was loaded)
			S.conn = { transport: tr, radio: sizedRadio(tr), probe, image: null, uid: null, cable, dbKnown: false };
			connectionLost = false;
			return S.conn;
		} catch (e) {
			await tr.close();
			throw e;
		}
	} finally {
		connecting = false;
		renderConnection();
	}
}

// The radio as radios.json has it, its codeplug's room cut to what this one says it has (transport.js end), so the ID
// database is sized for it
function sizedRadio(tr) {
	const r = tr.radio, cp = r.codeplug;
	return tr.end < cp.end - cp.base ? { ...r, codeplug: { ...cp, end: cp.base + tr.end } } : r;
}

function radioError(e) {
	if (e instanceof TransportError) {
		const p = { ...e.params };
		if (typeof p.addr === "number") p.addr = `0x${p.addr.toString(16).toUpperCase()}`;
		return t(`editor.radio_err.${e.code}`, p);
	}
	if (e instanceof FastError && e.status === "no answer") return t("editor.radio_err.no_answer");
	if (e?.name === "NotFoundError" && !/disconnect/i.test(e.message || "")) return null;	// the user closed the port chooser
	// the browser's own serial errors (a port another program holds, a radio unplugged): what to do, then its words
	const key = browserErrorKey(e);
	if (key) return `${t(key)} (${e.message})`;
	return t("editor.radio_err.other", { error: e?.message || String(e) });
}

// A failure with a radio: into the log and the console with what was being done, then shown with the way to report it
async function fail(stage, e) {
	const rec = diag.error(stage, e);
	await dropConnection();
	const msg = radioError(e);
	if (msg) say(msg, "bad", { report: rec });
}

// A radio fresh from its original firmware may still hold that firmware's data where RadioFoundry keeps its settings: it then
// refuses every write (settings-store.md 5). Offer to prepare it; true when the radio can take the write
async function ensureStorage(c) {
	if (c.storage?.region === "ours") return true;
	c.storage = await c.transport.storage();
	diag.note("storage region", c.storage);
	if (c.storage?.region !== "foreign") return true;
	const go = await new Promise((resolve) => {
		let ok = false;
		dialog(t("editor.storage.title"), [h("p", {}, t("editor.storage.text", { radio: c.radio.name })), h("p", {}, t("editor.storage.text2"))],
			t("editor.storage.prepare"), (close) => { ok = true; close(); }, () => resolve(ok));
	});
	if (!go) return false;
	const done = await withProgress(t("editor.storage.preparing", { radio: c.radio.name }), async (bar) => {
		bar.set(0, 1);
		return c.transport.claimStorage();
	});
	diag.note("storage claim", { done });
	if (!done) throw new TransportError("storage_claim_failed");
	c.storage = { region: "ours" };
	say(t("editor.storage.done", { radio: c.radio.name }), "ok");
	c.prepared = true;	// said again with the write's outcome, which replaces this message
	return true;
}

async function withProgress(title, fn) {
	const bar = progressBar();
	const ctrl = new AbortController();
	const d = dialog(title, [bar.el], null, null, () => ctrl.abort(), { cancel: t("editor.stop") });
	try {
		return await fn(bar, ctrl.signal);
	} finally {
		d.close();
	}
}

// One radio operation at a time. Connecting shows no dialog while it probes (up to 2 s), so a second click on Read
// started a second connection to the same port: it failed with "The port is already open", and its failure dropped
// the first connection too
let radioBusy = false;
async function exclusive(fn) {
	if (radioBusy) return diag.note("radio busy: click ignored");
	radioBusy = true;
	renderConnection();
	try {
		return await fn();
	} finally {
		radioBusy = false;
		renderConnection();
	}
}

// The radio on the line now. A write sends only the bytes that differ from c.image, so c.image must be this radio's:
// a programming cable stays open while one radio is swapped for the next, and the bytes the last one had are not the
// new one's. A different chip UID, or none (firmware that doesn't say), drops c.image and the write reads first
async function onLine(c) {
	const uid = await c.transport.who(S.radios);
	c.radio = sizedRadio(c.transport);
	if (c.image && (!uid || uid !== c.uid)) {
		diag.note("radio changed: read before write", { was: c.uid, now: uid });
		c.image = null;
		c.dbKnown = false;
	}
	c.uid = uid;
}

// The offer to convert a format N database to C, after a read: at most once in 12 hours for each radio model,
// whatever the answer (a converted database isn't offered again anyway)
const CONVERT_QUIET = 12 * 3600 * 1000;

function convertDue(radio) {
	const key = `rfoundry-editor-iddb-convert:${radio}`;
	try {
		if (Date.now() - Number(localStorage.getItem(key) || 0) < CONVERT_QUIET) return false;
		localStorage.setItem(key, String(Date.now()));
	} catch { /* without storage the offer comes after every read */ }
	return true;
}

// A read of the ID database expected to take this long or longer (seconds) is offered, not made
const IDDB_ASK = 20;

function askIddb(c, db, bytes, seconds) {
	return new Promise((done) => {
		let yes = false;
		dialog(t("editor.iddb_read.title"), [h("p", {}, t("editor.iddb_read.text", { radio: c.radio.name, n: db.count,
			size: kb(bytes), time: duration(seconds) })), h("p", {}, t("editor.iddb_read.skip_help"))],
		t("editor.iddb_read.read"), (close) => {
			yes = true;
			close();
		}, () => done(yes), { cancel: t("editor.iddb_read.skip") });
	});
}

const kb = (n) => n >= 1e6 ? `${(n / 1048576).toFixed(1)} MB` : `${Math.ceil(n / 1024)} KB`;

function duration(s) {
	return s < 90 ? t("editor.about_seconds", { n: Math.ceil(s / 5) * 5 }) : t("editor.about_minutes", { n: Math.round(s / 60) });
}

// The ID database a read skipped, read now into the codeplug (the DMR ID Database tab's button)
// The radio on the line holds the codeplug last read from it (img): its regions read again and compared
async function sameCodeplug(c, img) {
	const now = await withProgress(t("editor.reading", { radio: c.radio.name }), (bar, signal) =>
		c.transport.read({ iddb: false, signal, cache: img, progress: (n, total) => bar.set(n, total) }));
	const at = (x, a, b) => x.bytes.subarray(a, Math.min(b, x.bytes.length));
	return REGIONS.every(([a, b]) => equal(at(now, a, b), at(img, a, b)));
}

function readSkippedIddb() {
	return exclusive(async () => {
		const c = S.conn;
		try {
			if (!c?.image || !S.iddbSkipped || S.iddbChanged) return;
			const [was, known] = [c.image, c.dbKnown];
			await onLine(c);
			// firmware that doesn't give its chip UID: the codeplug read again (with the fast protocol only the blocks that
			// changed move) and unchanged is the same radio
			if (!c.image && !c.uid && await sameCodeplug(c, was)) {
				c.image = was;
				c.dbKnown = known;
			}
			if (!c.image) return say(t("editor.iddb_read.swapped"), "warn");
			// the header from the radio, not from c.image: a write since the read left the model's erased header there
			const img = await withProgress(t("editor.reading_iddb", { radio: c.radio.name }), async (bar, signal) =>
				c.transport.readIddb(await c.transport.readIddbHeader({ signal }), { signal, cache: c.image,
					progress: (n, total) => bar.set(n, total) }));
			// the database's bytes into the codeplug being edited (its other edits kept) and into the radio's as last read
			if (dbHeader(img.bytes)) {
				for (const [a, b] of iddbRanges(img)) {
					for (const dst of [S.model.base, c.image]) {
						dst.ensure(b);
						dst.bytes.set(img.bytes.subarray(a, b), a);
					}
				}
			}
			S.model.iddb = dbHeader(S.model.base.bytes);
			c.dbKnown = true;
			S.iddbSkipped = null;
			diag.note("iddb read", { bytes: img.bytes.length });
			refresh();
			draftSoon();
		} catch (e) {
			await fail("read ID database", e);
		}
	});
}

async function readRadio() {
	const go = () => exclusive(async () => {
		try {
			const c = await connect();
			if (!c) return;
			await onLine(c);
			// the ID database can be megabytes: its header first, and a long read of it offered, not made. Then one read
			// (one progress bar) of the codeplug and, when wanted, the database
			const cached = !!c.image && c.dbKnown;
			const head = await c.transport.readIddbHeader();
			const db = dbHeader(head.bytes);
			const dbBytes = c.transport.iddbBytes(head);
			const rate = c.rate || (c.cable ? 9000 : 30000);
			const seconds = cached ? 0 : Math.ceil(dbBytes / rate);
			const readDb = dbBytes > 0 && (seconds < IDDB_ASK || await askIddb(c, db, dbBytes, seconds));
			const t0 = performance.now(), moved = c.transport.moved;
			// the header just read serves as the cache for its own block on a first read
			const img = await withProgress(t("editor.reading", { radio: c.radio.name }), (bar, signal) =>
				c.transport.read({ iddb: readDb, signal, cache: c.image || head, progress: (n, total) => bar.set(n, total) }));
			// how fast this radio and cable read, for the next estimate (a read from the cache says nothing): by the bytes
			// that crossed the link, not the progress, which also counts blank blocks the fast protocol skips. A read of a
			// few blocks is mostly round trips: too few to tell
			const took = c.transport.moved - moved;
			if (!c.image && took >= 65536) c.rate = took / Math.max(0.25, (performance.now() - t0) / 1000);
			diag.note("read done", { bytes: img.bytes.length, iddb: readDb, dbBytes, seconds });
			c.image = img.clone();
			if (db && !readDb) {
				// the model without the database (its header erased): a write leaves the radio's as it is
				img.bytes.fill(0xFF, IDDB_HEADER[0], IDDB_HEADER[1]);
			}
			load(decode(img), `${c.radio.id}-codeplug`, { fromRadio: true });
			c.dbKnown = !db || readDb;
			S.iddbSkipped = db && !readDb ? { count: db.count, bytes: dbBytes, seconds } : null;
			if (S.iddbSkipped) refresh();
			say(t("editor.read_done", { radio: c.radio.name, channels: S.model.channels.length, contacts: S.model.contacts.length }), "ok");
			// the database's format against what this radio's firmware reads (iddb-compact.md 6)
			const fmt = S.model.iddb?.format;
			if (fmt === "C" && !c.transport.compact) {
				S.iddbChanged = true;
				say(t("editor.iddb.old_radio_reads_none"), "warn");
			} else if (fmt === "N" && c.transport.compact && convertDue(c.radio.id)) {
				confirmDialog(t("editor.iddb.upgrade", { radio: c.radio.name }), t("editor.iddb.convert"), upgradeIddb,
					{ onClose: () => offerUpdate(c) });
				return;
			}
			offerUpdate(c);
		} catch (e) {
			await fail("read codeplug", e);
		}
	});
	if (S.dirty) confirmDialog(t("editor.read_confirm"), t("editor.read"), go);
	else go();
}

// Features a newer release may add that change what the editor can do with the radio, with what each gains
const UPDATE_GAINS = ["CPS_FAST", "IDDB_COMPACT"];
let releaseList = null;

// After a read: a newer release for this radio (releases.json, updateFor), offered once per tab for each radio, running
// build and release
async function offerUpdate(c) {
	const v = c.transport.probe?.version || {};
	const gitTime = Number(v.git_time) || 0;
	if (!v.release && !gitTime) return;
	if (!releaseList) {
		try {
			releaseList = (await (await fetch(`${ROOT}data/releases.json`)).json()).releases || [];
		} catch {
			return;
		}
	}
	const rel = updateFor(releaseList, c.radio.id, v.release, gitTime);
	if (!rel) return;
	// a release by its name, any other build by its commit and that commit's date
	const running = v.release || t("editor.update.build", { git: v.git, date: fmtDate(new Date(gitTime * 1000), { dateStyle: "medium" }) });
	const key = `rfoundry-editor-update:${c.radio.id}:${v.release || v.git}:${rel.version}`;
	try {
		if (sessionStorage.getItem(key)) return;
		sessionStorage.setItem(key, "1");
	} catch { /* without storage the offer comes after every read */ }
	const has = String(c.transport.probe.features || "").split(",");
	const gains = UPDATE_GAINS.filter((f) => String(rel.features || "").split(",").includes(f) && !has.includes(f));
	diag.note("update offered", { running: v.release || v.git, release: rel.version, gains });
	const body = [h("p", {}, t("editor.update.text", { radio: c.radio.name, version: rel.version, running }))];
	if (gains.length) body.push(h("p", {}, t("editor.update.gains")), h("ul", {}, ...gains.map((f) => h("li", {}, t(`editor.update.gain.${f}`)))));
	dialog(t("editor.update.title"), body, t("editor.update.go"), async (close) => {
		// the flasher's tab needs the port: this one lets go of it (opened first, in the click, so no popup blocker stops it)
		window.open(`${HOME}flash/index.html#update`, "_blank", "noopener");
		close();
		if (S.conn === c) await dropConnection();
	}, () => {}, { cancel: t("editor.update.later") });
}

function writeRadio() {
	return exclusive(planWrite);
}

async function planWrite() {
	const errors = problems().filter((p) => p.level === "error");
	if (errors.length) {
		say(t("editor.err.fix_first", { n: errors.length }), "bad");
		return show("problems");
	}
	let stage = "connect";
	try {
		const c = await connect();
		if (!c) return;
		stage = "prepare radio";
		if (!await ensureStorage(c)) return;
		stage = "encode codeplug";
		const file = encode(S.model);
		stage = "check radio";
		await onLine(c);
		if (!c.image) {
			stage = "read before write";
			c.image = await withProgress(t("editor.comparing", { radio: c.radio.name }), (bar, signal) =>
				c.transport.read({ iddb: false, signal, progress: (n, total) => bar.set(n, total) }));
		}
		// the ID database goes when it was edited, or when this radio's may differ: a file or draft opened since, or
		// another radio on the cable. The fast protocol then sends only its blocks that differ
		const iddb = S.iddbChanged || (!!dbHeader(file.bytes) && !c.dbKnown);
		// a compact database to a radio that reads only format N goes as format N (iddb-compact.md 6)
		let older = null;
		if (iddb && dbHeader(file.bytes)?.format === "C" && !c.transport.compact) older = convertDb(file, "N", { end: c.transport.end });
		stage = "plan write";
		// the same codeplug with this radio's own name, ID and boot text kept, and what each would send
		// (compared byte for byte: two plans can send the same number of bytes. The second is planned only when there
		// is something to keep: each plan is a pass over every block's CRC)
		const kept = keepIdentity(file, c.image);
		const differs = IDENTITY.some(([a, b]) => kept.bytes.subarray(a, b).some((x, i) => x !== file.bytes[a + i]));
		const plans = { file: await c.transport.plan(file, c.image, { iddb }) };
		plans.kept = differs ? await c.transport.plan(kept, c.image, { iddb }) : plans.file;
		let keep = keepPref() && differs;
		const choice = () => (keep ? { want: kept, ...plans.kept } : { want: file, ...plans.file });
		diag.note("write plan", { bytes: choice().bytes, sectors: choice().plan.length, iddb, keep });
		const prepared = c.prepared ? `${t("editor.storage.done", { radio: c.radio.name })} ` : "";
		c.prepared = false;
		// by the plan, not its bytes: a block written blank counts no bytes
		if (!choice().plan.length) return say(prepared + t("editor.write_nothing", { radio: c.radio.name }), "ok");
		const summary = h("p", {});
		const summarize = () => {
			const { bytes } = choice();
			const text = [t("editor.write_summary", { radio: c.radio.name, bytes: fmtNum(bytes), seconds: estimate(bytes, c.cable, choice().blocks) })];
			if (iddb) text.push(t(c.transport.fast ? "editor.write_iddb_fast" : "editor.write_iddb", { n: dbHeader(file.bytes)?.count ?? 0 }));
			if (older) text.push(t("editor.write_iddb_older"));
			if (older?.dropped) text.push(t("editor.write_iddb_cut", { dropped: fmtNum(older.dropped) }));
			summary.textContent = text.join(" ");
		};
		summarize();
		const body = [summary];
		// offered only when the radio's identity differs from the file's: otherwise there's nothing to keep
		if (differs) {
			const g = decodeGeneral(c.image.bytes), none = t("editor.keep_identity_none");
			const box = h("input", { type: "checkbox", checked: keep, onchange: () => {
				keep = box.checked;
				keepPref(keep);
				summarize();
			} });
			body.push(h("label", {}, box, " ", t("editor.keep_identity")),
				h("p", { class: "small muted" }, t("editor.keep_identity_help", { name: g.radioName || none, id: g.dmrId || none })));
		}
		dialog(t("editor.write"), body, t("editor.write"), (close) => {
			close();
			writeChosen(c, choice(), iddb, prepared);
		});
	} catch (e) {
		await fail(stage, e);
	}
}

// The ID database rewritten in format C for a radio that reads it, as one undoable change; Write to Radio sends it
function upgradeIddb() {
	let r = null;
	change(t("editor.iddb.convert"), () => {
		r = convertIddb(S.model, "C", iddbEnd(S.conn?.radio));
		return r ? undefined : false;
	});
	if (!r) return;
	S.iddbChanged = true;
	say(t("editor.iddb.converted", { n: r.count }), "ok");
}

// Whether the last write kept the radio's own identity: remembered in this browser for the next radio
function keepPref(v) {
	const k = "rfoundry-editor-keep-identity";
	try {
		if (v === undefined) return localStorage.getItem(k) === "1";
		localStorage.setItem(k, v ? "1" : "0");
	} catch {
		// private mode: not remembered
	}
	return !!v;
}

function writeChosen(c, { want }, iddb, prepared) {
	return exclusive(async () => {
		let step = "write codeplug";
		try {
			const res = await withProgress(t("editor.writing", { radio: c.radio.name }), async (bar, signal) => {
				// the CPS rewrites the whole codeplug with a database; the fast protocol checks every block's CRC itself
				const r = await c.transport.write(want, iddb && !c.transport.fast ? null : c.image, { iddb, signal, progress: (n, total) => bar.set(n, total, t("editor.progress.write")) });
				step = "verify codeplug";
				await c.transport.verify(want, r.plan, { progress: (n, total) => bar.set(n, total, t("editor.progress.verify")) });
				step = "finish write";
				await c.transport.finish();
				return r;
			});
			diag.note("write done", { bytes: res.bytes, sectors: res.sectors });
			c.image = (res.image ?? want).clone();
			if (iddb) c.dbKnown = true;
			S.iddbChanged = false;
			S.model.base = res.image ?? want;
			S.model.iddb = dbHeader(want.bytes);	// format N when it went to a radio that reads only that
			S.dirty = false;
			refresh();
			draftSoon();
			say(prepared + t("editor.write_done", { radio: c.radio.name, bytes: fmtNum(res.bytes) }), "ok");
		} catch (e) {
			await fail(step, e);
		}
	});
}

async function dropConnection({ lost = false } = {}) {
	const connection = S.conn;
	if (!connection) return;
	S.lastRadioImage = connection.image || S.lastRadioImage;
	encryptionView?.deactivate();
	S.conn = null;
	connectionLost = lost;
	refresh();
	await connection.transport.close();
}

// Observe transport loss without polling the radio or taking its request lock.
setInterval(() => {
	if (S.conn?.transport.lost && !radioBusy)
		dropConnection({ lost: true }).catch(error => diag.error("disconnect radio", error));
}, 1000);

// --- Undo ---

function undo() {
	if (S.history.undo(S.model) !== null) {
		S.dirty = true;
		refresh();
		draftSoon();
	}
}

function redo() {
	if (S.history.redo(S.model) !== null) {
		S.dirty = true;
		refresh();
		draftSoon();
	}
}

document.addEventListener("keydown", (e) => {
	if (!(e.ctrlKey || e.metaKey) || e.target.closest("input, select, textarea, dialog")) return;
	const k = e.key.toLowerCase();
	if (k === "z" && !e.shiftKey) {
		e.preventDefault();
		undo();
	} else if ((k === "z" && e.shiftKey) || k === "y") {
		e.preventDefault();
		redo();
	}
});

// --- Dialogs ---

function dialog(title, body, okLabel, onOk, onClose = () => {}, { cancel = t("editor.cancel"), primary = true } = {}) {
	const d = h("dialog", { class: "confirm editor-dialog" });
	const close = () => d.close();
	const acts = h("div", { class: "confirm-actions" }, button(cancel, close));
	if (okLabel) acts.append(button(okLabel, () => onOk(close), { primary }));
	d.append(h("h2", {}, title), ...body, acts);
	d.addEventListener("close", () => {
		d.remove();
		onClose();
	});
	document.body.append(d);
	d.showModal();
	return d;
}

function confirmDialog(text, okLabel, onOk, { onClose = () => {}, ...opts } = {}) {
	return dialog(okLabel, [h("p", {}, text)], okLabel, (close) => {
		close();
		onOk();
	}, onClose, opts);
}

// --- What other modules (CSV, YAML, contacts) get from the page ---

function api() {
	return {
		get model() { return S.model; },
		t, h, button, notice, say, change, load, refresh, download, dialog, confirmDialog,
		get fileName() { return S.fileName; },
		get tab() { return S.tab; },
		show,
		get radio() { return S.conn?.radio || null; },
		// whether the connected radio reads a format C ID database; null without a radio
		get compact() { return S.conn ? S.conn.transport.compact : null; },
		upgradeIddb,
		get radios() { return S.radios; },
		root: ROOT,
		markIddb() {
			S.iddbChanged = true;
			S.iddbSkipped = null;
		},
		// the radio's ID database when a read skipped it ({count, bytes, seconds}), and reading it now
		get iddbSkipped() { return S.iddbSkipped; },
		readSkippedIddb,
		encode: () => encode(S.model),
		decode, numberAll,
	};
}

// --- Start ---

// What a bug report says about the editor: no names, callsigns or IDs (automatic error reports carry it too)
function reportContext() {
	const c = S.conn, m = S.model;
	return {
		sim: S.sim,
		radio: c ? { id: c.radio?.id, board: c.probe?.board, git: c.probe?.git, features: c.probe?.features, codec: c.probe?.codec, cable: c.cable, storage: c.storage?.region } : null,
		codeplug: m ? { channels: m.channels.length, zones: m.zones.length, contacts: m.contacts.length, iddb: m.iddb?.count || 0,
			dirty: S.dirty, iddbChanged: S.iddbChanged, errors: problems().filter((p) => p.level === "error").length } : null,
		tab: S.tab,
	};
}

// What the user may add to a report: the codeplug being edited and the one last read from the radio
function reportFiles() {
	const out = [];
	if (S.model) out.push({ name: "codeplug-edited.bin", label: t("report.attach_codeplug"), data: () => saveBytes(encode(S.model)) });
	const radio = S.conn?.image || S.lastRadioImage;
	if (radio) out.push({ name: "codeplug-radio.bin", label: t("report.attach_radio"), data: () => saveBytes(radio) });
	return out;
}

async function start() {
	initReporting({ tool: "editor", context: reportContext, attachments: reportFiles });
	const loading = h("p", { class: "muted", role: "status" }, t("editor.loading"));
	root.replaceChildren(loading, connectionBar, editorHead, messages, welcome, tabs, view, fileInput);
	try {
		const r = await fetch(`${ROOT}data/radios.json`);
		S.radios = (await r.json()).radios || [];
	} catch {
		S.radios = [];
	}
	const draft = await loadDraft();
	const direct = TABS.includes(new URLSearchParams(location.search).get("tab")) || S.sim;
	const fresh = !direct && (!draft?.bytes || isUnusedDraft(draft, saveBytes(encode(empty()))));
	const restored = !!draft?.bytes && !fresh;
	loading.remove();
	load(restored ? decode(imageFrom(draft.bytes)) : empty(), restored ? draft.name || "codeplug" : "codeplug",
		{ dirty: restored && !!draft.dirty, restored, started: !fresh });
	if (restored)
		say(t("editor.draft_restored", { time: fmtDate(new Date(draft.at), { dateStyle: "medium", timeStyle: "short" }) }), "info");
	if (!serialSupported() && !S.sim) say(t("editor.no_serial"), "info");
	if (S.sim) say(t("editor.sim"), "info");
}

start();

export { S, refLabel };

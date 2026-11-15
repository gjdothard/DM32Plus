// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
import { h, t, button, notice } from "../flasher/dom.js";
import { AesKeys } from "../codeplug/aeskeys.js";

export function encryptionTab({ connect, exclusive }) {
	const panel = h("div", { class: "ed-aes" }, h("h2", {}, t("editor.aes.title")), h("p", {}, t("editor.aes.lead")));
	const status = h("div", { "aria-live": "polite" });
	const rows = h("div", {});
	let client, state;
	const report = (e) => status.replaceChildren(notice(t(`editor.aes.error.${e?.message in errors ? e.message : "failed"}`), "bad"));
	const errors = { key: 1, id: 1, busy: 1, firmware: 1, failed: 1 };
	const run = (action) => exclusive(async () => {
		panel.inert = true;
		try { await action(); }
		catch (e) { report(e); }
		finally { panel.inert = false; }
	});
	const refresh = async () => { state = await client.list(); render(); };
	function render() {
		rows.replaceChildren();
		if (!state.ready) {
			const region = state.storage?.region;
			const detail = region === "foreign" ? "foreign" : region === "ours" ? "blocked" :
				region === "none" || region === "nomap" ? "missing" : "unknown";
			return rows.append(notice(t(`editor.aes.storage.${detail}`), "warn"));
		}
		rows.append(h("p", {}, state.active ? t("editor.aes.active", { n: state.active }) : t("editor.aes.off")));
		for (const entry of state.slots) {
			const id = h("input", { type: "number", min: 1, max: 255, value: entry.id || entry.slot, "aria-label": t("editor.aes.id") });
			const key = h("input", { type: "password", maxlength: 64, autocomplete: "off", spellcheck: "false", "aria-label": t("editor.aes.key") });
			const show = h("input", { type: "checkbox", onchange: () => { key.type = show.checked ? "text" : "password"; } });
			rows.append(h("fieldset", {}, h("legend", {}, t("editor.aes.slot", { n: entry.slot })),
				h("p", { class: "muted" }, entry.id ? t("editor.aes.programmed", { id: entry.id }) : t("editor.aes.empty")),
				h("label", {}, t("editor.aes.id"), id),
				h("label", {}, t("editor.aes.key"), key), h("label", {}, show, t("editor.aes.show")),
				button(t("editor.aes.program"), () => run(async () => {
					const text = key.value;
					key.value = "";
					await client.program(entry.slot, Number(id.value), text);
					await refresh();
					status.replaceChildren(notice(t("editor.aes.saved"), "ok"));
				})),
				button(t("editor.aes.use"), () => run(async () => {
					await client.select(entry.slot);
					await refresh();
					status.replaceChildren(notice(t("editor.aes.selected"), "ok"));
				}), { disabled: !entry.id }),
				button(t("editor.aes.delete"), () => run(async () => {
					if (!confirm(t("editor.aes.delete_confirm", { n: entry.slot }))) return;
					await client.remove(entry.slot);
					await refresh();
					status.replaceChildren(notice(t("editor.aes.deleted"), "ok"));
				}), { disabled: !entry.id })));
		}
		rows.append(button(t("editor.aes.disable"), () => run(async () => {
			await client.select(0);
			await refresh();
			status.replaceChildren(notice(t("editor.aes.off"), "ok"));
		})));
	}
	panel.append(notice(t("editor.aes.local")), button(t("editor.aes.connect"), () => run(async () => {
		const connection = await connect();
		if (!connection) return;
		client = new AesKeys(connection.transport.link);
		await refresh();
		status.replaceChildren();
	})), status, rows);
	return panel;
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Garrett Dothard
import { h, t, button, notice } from "../flasher/dom.js";
import { AesKeys, randomKey } from "../codeplug/aeskeys.js";

export function encryptionTab({ connect, exclusive }) {
	const panel = h("div", { class: "ed-aes ed-key-manager" });
	const status = h("div", { "aria-live": "polite" }), content = h("div");
	let client, state, selected = 1, editing = false, keyInput;
	const wipe = () => {
		if (keyInput) { keyInput.value = ""; keyInput.type = "password"; }
	};
	const observer = new MutationObserver(() => {
		if (!panel.isConnected) { wipe(); observer.disconnect(); }
	});
	observer.observe(document.body, { childList: true, subtree: true });
	const discard = () => !keyInput?.value || confirm(t("editor.aes.discard_confirm"));
	const run = action => exclusive(async () => {
		panel.inert = true;
		try { await action(); }
		catch (error) {
			wipe();
			const known = ["key", "id", "busy", "firmware"];
			status.replaceChildren(notice(t("editor.aes.error." + (known.includes(error?.message) ? error.message : "failed")), "bad"));
		} finally { panel.inert = false; }
	});
	const refresh = async () => {
		wipe();
		state = await client.list();
		editing = false;
		if (panel.isConnected) render();
	};
	function render() {
		wipe();
		content.replaceChildren();
		if (!state.ready) {
			const region = state.storage?.region;
			const detail = region === "foreign" ? "foreign" : region === "ours" ? "blocked" :
				region === "none" || region === "nomap" ? "missing" : "unknown";
			content.append(notice(t("editor.aes.storage." + detail), "warn"));
			return;
		}
		const defaults = h("select", { "aria-label": t("editor.aes.default") },
			h("option", { value: 0 }, t("editor.aes.disable")),
			...state.slots.filter(slot => slot.id).map(slot =>
				h("option", { value: slot.slot }, t("editor.aes.slot", { n: slot.slot }) + " · " + t("editor.aes.programmed", { id: slot.id }))));
		defaults.value = String(state.active);
		defaults.addEventListener("change", () => {
			const slot = Number(defaults.value);
			defaults.value = String(state.active);
			if (!discard()) return;
			run(async () => {
				await client.select(slot);
				await refresh();
				status.replaceChildren(notice(t(slot ? "editor.aes.selected" : "editor.aes.off"), "ok"));
			});
		});
		const nav = h("nav", { class: "ed-key-slots", "aria-label": t("editor.aes.title") },
			...state.slots.map(slot => {
				const control = button([
					h("strong", {}, t("editor.aes.slot", { n: slot.slot })),
					h("span", {}, t(slot.id ? "editor.aes.programmed" : "editor.aes.empty", { id: slot.id })),
					slot.slot === state.active ? h("small", {}, t("editor.aes.default")) : null,
				], () => {
					if (slot.slot === selected || !discard()) return;
					selected = slot.slot;
					editing = false;
					render();
				});
				control.setAttribute("aria-pressed", String(slot.slot === selected));
				return control;
			}));
		const entry = state.slots.find(slot => slot.slot === selected);
		const editor = h("section", { class: "ed-key-editor", "aria-label": t("editor.aes.slot", { n: selected }) },
			h("h3", {}, t("editor.aes.slot", { n: selected })));
		if (entry.id && !editing) {
			editor.append(h("div", { class: "ed-key-programmed" },
				h("span", { "aria-hidden": "true", class: "ed-key-lock" }, "🔒"),
				h("div", {}, h("strong", {}, t("editor.aes.key_programmed")),
					h("p", { class: "small muted" }, t("editor.aes.programmed", { id: entry.id }) + " · AES-256"))),
				button(t("editor.aes.change"), () => {
					if (!confirm(t("editor.aes.change_confirm", { n: selected }))) return;
					editing = true;
					render();
					keyInput.focus();
				}));
		} else {
			const id = h("input", { type: "number", min: 1, max: 255, value: entry.id || selected });
			const key = h("input", { type: "password", maxlength: 64, autocomplete: "off", spellcheck: "false" });
			keyInput = key;
			const count = h("span", { class: "small muted" });
			const show = h("input", { type: "checkbox", onchange: () => { key.type = show.checked ? "text" : "password"; } });
			const copy = button(t("editor.aes.copy"), async () => {
				try {
					await navigator.clipboard.writeText(key.value);
					if (panel.isConnected) status.replaceChildren(notice(t("editor.aes.copied"), "ok"));
				} catch {
					status.replaceChildren(notice(t("editor.aes.copy_failed"), "warn"));
				}
			});
			const valid = () => /^[a-f0-9]{64}$/i.test(key.value) && Number.isInteger(Number(id.value)) &&
				Number(id.value) >= 1 && Number(id.value) <= 255;
			const save = button(t("editor.aes.program"), () => {
				if (!valid()) return;
				run(async () => {
					await client.program(selected, Number(id.value), key.value);
					wipe();
					await refresh();
					status.replaceChildren(notice(t("editor.aes.saved"), "ok"));
				});
			}, { primary: true });
			const sync = () => {
				count.textContent = t("editor.aes.characters", { n: key.value.length });
				copy.disabled = !/^[a-f0-9]{64}$/i.test(key.value);
				save.disabled = !valid();
			};
			key.addEventListener("input", sync);
			id.addEventListener("input", sync);
			editor.append(h("label", { class: "ed-key-id" }, t("editor.aes.id"), id),
				h("label", {}, t("editor.aes.key"), key), count,
				h("div", { class: "ed-key-actions" }, h("label", {}, show, t("editor.aes.show")), copy),
				button(t("editor.aes.generate"), () => {
					if (!discard()) return;
					try {
						key.value = randomKey();
						show.checked = false;
						key.type = "password";
						sync();
						status.replaceChildren(notice(t("editor.aes.generated"), "info"));
					} catch {
						status.replaceChildren(notice(t("editor.aes.random_failed"), "bad"));
					}
				}),
				h("div", { class: "ed-key-actions" }, save,
					button(t("editor.aes.clear_entry"), () => { wipe(); show.checked = false; sync(); }),
					entry.id ? button(t("editor.cancel"), () => {
						wipe(); editing = false; render();
					}) : null));
			sync();
		}
		if (entry.id) editor.append(h("div", { class: "ed-key-existing" },
			button(t("editor.aes.delete"), () => {
				if (!confirm(t("editor.aes.delete_confirm", { n: selected }))) return;
				run(async () => {
					wipe();
					await client.remove(selected);
					await refresh();
					status.replaceChildren(notice(t("editor.aes.deleted"), "ok"));
				});
			}, { danger: true })));
		content.append(h("label", { class: "ed-key-default" }, t("editor.aes.default"), defaults),
			h("p", { class: "small muted" }, t(state.active ? "editor.aes.active" : "editor.aes.off", { n: state.active })),
			h("div", { class: "ed-key-workspace" }, nav, editor));
	}
	panel.append(h("h2", {}, t("editor.aes.title")), h("p", {}, t("editor.aes.lead")),
		button(t("editor.aes.connect"), () => {
			if (!discard()) return;
			run(async () => {
				const connection = await connect();
				if (!connection || !panel.isConnected) return;
				client = new AesKeys(connection.transport.link);
				await refresh();
				status.replaceChildren();
			});
		}), status, content, h("p", { class: "small muted" }, t("editor.aes.local")));
	return panel;
}

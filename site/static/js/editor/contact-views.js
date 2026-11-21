// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Contact pages share records and capacity; only presentation and creation defaults differ.
import { contactAllowed } from "../codeplug/contact-protocol.js";

export const CONTACT_VIEWS = [
	{ key: "all", defaults: {}, matches: () => true },
	{ key: "dmr", defaults: { protocol: "dmr", type: "group" }, matches: c => c.type === "group" && (c.protocol ?? "dmr") === "dmr" },
	{ key: "p25", defaults: { protocol: "p25", type: "group" }, matches: c => c.type === "group" && c.protocol === "p25" },
	{ key: "private", defaults: { protocol: "dmr", type: "private" }, matches: c => c.type === "private" },
	{ key: "shared", defaults: { protocol: "both", type: "group" }, matches: c => c.type === "group" && c.protocol === "both" },
];
export const contactView = key => CONTACT_VIEWS.find(view => view.key === key) ?? CONTACT_VIEWS[0];
export const contactViewFor = record => CONTACT_VIEWS.slice(1).find(view => view.matches(record))?.key ?? "all";
export const contactRows = (model, key) => model.contacts.filter(contactView(key).matches);
export function contactColumns(all, key, t) {
	return all.filter(column => column.key !== "timeslot" || !["p25", "private"].includes(key))
		.map(column => key === "private" && column.key === "id" ? { ...column, label: t("editor.contacts.unit_id") } : column);
}

export function contactEditError(model, record, work, t) {
	if (["p25", "both"].includes(work.protocol) && work.type === "group" && work.id > 0xFFFF)
		return t("editor.err.range", { min: 1, max: 65535 });
	const saved = model.contacts.includes(record) ? record : model.contacts.find(c => c.number && c.number === record.number);
	if (!saved) return;
	if ((record.protocol ?? "dmr") === (work.protocol ?? "dmr") && record.type === work.type) return;
	const channels = [...model.channels, ...model.vfo].filter(Boolean).filter(channel => {
		const group = (channel.talkgroupRefs ?? []).includes(saved) || (channel.rxList?.contacts ?? []).includes(saved);
		const used = group || channel.contact === saved;
		return used && (!contactAllowed(work, channel) || (group && work.type !== "group"));
	});
	if (channels.length) return t("editor.contacts.assigned", { channels: channels.map(c => c.name).join(", ") });
	if (work.type !== "group" && model.rxLists.some(list => list.contacts.includes(saved)))
		return t("editor.contacts.rx_list");
}

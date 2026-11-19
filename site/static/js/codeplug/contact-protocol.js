// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Protocol-scoped contact names; channel assignments remain the radio's selections.
import { decodeArea, getValue, setValue, deleteValue, putArea } from "./kv.js";
import { nameCheck } from "./modebytes.js";

export const CONTACT_PROTOCOL_KEY = 0x2000;
const scopes = ["", "dmr", "p25", "both"];
export const isP25 = channel => channel.kind === 2 || channel.kind === 3;
export const contactAllowed = (contact, channel) => {
	const scope = contact.protocol ?? "dmr";
	return scope === "both" || scope === (isP25(channel) ? "p25" : "dmr");
};

function binding(contact) {
	const check = nameCheck(contact.name);
	return [check & 255, check >> 8, contact.id & 255, (contact.id >> 8) & 255, (contact.id >> 16) & 255];
}

export function decodeContactProtocols(model, bytes) {
	const entries = decodeArea(bytes).entries;
	const usage = new Map();
	for (const channel of model.channels) {
		const bit = isP25(channel) ? 2 : channel.mode === "dmr" && !channel.kind ? 1 : 0;
		if (!bit) continue;
		for (const contact of [channel.contact, ...(channel.talkgroupRefs ?? []), ...(channel.rxList?.contacts ?? [])])
			if (contact && typeof contact === "object") usage.set(contact, (usage.get(contact) ?? 0) | bit);
	}
	for (const contact of model.contacts) {
		const raw = getValue(entries, CONTACT_PROTOCOL_KEY + contact.number - 1);
		const valid = raw?.length === 7 && raw[0] === 1 && scopes[raw[1]] &&
			binding(contact).every((value, i) => value === raw[i + 2]);
		contact.protocol = valid ? scopes[raw[1]] : scopes[usage.get(contact) ?? 1];
	}
}

export function contactProtocolEntries(model, original) {
	let entries = original;
	const contacts = new Map(model.contacts.map(contact => [contact.number, contact]));
	for (let number = 1; number <= 1024; number++) {
		const key = CONTACT_PROTOCOL_KEY + number - 1;
		const old = getValue(entries, key), contact = contacts.get(number);
		if (old && (old.length !== 7 || old[0] !== 1)) {
			continue;
		}
		const protocol = contact?.protocol ?? "dmr";
		if (!scopes.includes(protocol) || !protocol) continue;
		if (!contact || protocol === "dmr") {
			if (old) entries = deleteValue(entries, key);
		} else {
			const value = Uint8Array.from([1, scopes.indexOf(protocol), ...binding(contact)]);
			if (!old || old.some((byte, i) => byte !== value[i])) entries = setValue(entries, key, value);
		}
	}
	return entries;
}

export function encodeContactProtocols(model, bytes) {
	const area = decodeArea(bytes);
	if (area.state === "newer") return;
	const entries = contactProtocolEntries(model, area.entries);
	if (entries !== area.entries) putArea(bytes, entries);
}

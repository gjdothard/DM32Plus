// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Reserve mode metadata without losing the older inline DMR operating assignments.
import { decodeArea, KV_ADDR, KV_SIZE } from "./kv.js";
import { POOL_START, POOL_END, POOL_CAPACITY, readPool } from "./talkgroup-pool.js";
import { contactAllowed, isP25 } from "./contact-protocol.js";

export const hasModes = m => m.channels.some(c => c.kind) || (m.systems?.length ?? 0) > 0 ||
	Object.values(m.modeSettings ?? {}).some(s => s.unitId) || m.contacts.some(contact => contact.protocol === "p25" || contact.protocol === "both");
export const modeMagic = bytes => [82, 70, 75, 86].every((v, i) => bytes[KV_ADDR + i] === v);
export const containsModes = m => hasModes(m) || (m.base && modeMagic(m.base.bytes));
export class ModeStorageError extends Error {
	constructor(code) { super(code); this.code = code; }
}

function legacyTail(bytes) {
	for (let at = KV_ADDR; at < KV_ADDR + KV_SIZE; at += 16) {
		const record = bytes.subarray(at, at + 16);
		if (record.every(b => b === 0 || b === 255)) continue;
		for (let k = 0; k < 16; k += 4) {
			if (record.subarray(k).every(b => b === 255)) break;
			const id = record[k] | record[k + 1] << 8 | record[k + 2] << 16;
			if (![0xA0, 0xA1].includes(record[k + 3]) || id < 1 || id > 0xFFFFFE) return false;
		}
	}
	return true;
}

export function prepareModeStorage(m, dryRun = false) {
	if (!hasModes(m)) return;
	const bytes = m.base.bytes, area = decodeArea(bytes);
	if (area.state === "newer" || (area.state === "bad" && (modeMagic(bytes) || !legacyTail(bytes))))
		throw new ModeStorageError("mode_storage_unrecognized");
	if (m.sharedTalkgroups) return;
	if (!readPool(bytes) && bytes.subarray(POOL_START, POOL_END).some(b => b !== 255))
		throw new ModeStorageError("mode_pool_unrecognized");
	const contacts = m.contacts.slice(), created = [], assignments = new Map();
	let count = 0;
	for (const c of m.channels) {
		const refs = [];
		for (const e of c.talkGroups ?? []) {
			let contact = contacts.find(x => x.type === "group" && x.id === e.id && contactAllowed(x, c) && (x.timeslot || c.timeslot) === e.timeslot);
			if (!contact) {
				contact = { number: null, type: "group", id: e.id, timeslot: e.timeslot,
					protocol: isP25(c) ? "p25" : "dmr", name: isP25(c) ? `TG ${e.id}` : `TG ${e.id} TS${e.timeslot}` };
				contacts.push(contact); created.push(contact);
			}
			refs.push(contact);
		}
		count += refs.length;
		assignments.set(c, refs);
	}
	if (contacts.length > 1024 || count > POOL_CAPACITY) throw new ModeStorageError("mode_migration_full");
	if (dryRun) return;
	m.contacts.push(...created);
	for (const [c, refs] of assignments) c.talkgroupRefs = refs;
	m.sharedTalkgroups = true;
}

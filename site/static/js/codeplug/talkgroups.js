// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Ordered DMR operating entries in the RadioFoundry EEPROM extension.
export const TALK_GROUP_START = 0xB000;
const INLINE_MAX = 4;
export const TALK_GROUP_MAX = 16;
const OVERFLOW = 0xF000;
export const TALK_GROUP_END = 0x10000;
const MAGIC = [82, 70, 84, 71, 2, 0, 0, 0];
const POOL = OVERFLOW + MAGIC.length + 2048;
const POOL_MAX = (TALK_GROUP_END - POOL) / 4;
const TAG = 0xA0;

export function parseTalkGroups(value) {
	const input = String(value).trim();
	const entries = input ? input.split(",").map((entry) => {
		const match = /^\s*([12])\s*:\s*(\d{1,8})\s*$/.exec(entry);
		return match ? { id: Number(match[2]), timeslot: Number(match[1]) } : {};
	}) : [];
	return validTalkGroups(entries) ? entries : null;
}

export function formatTalkGroups(entries = []) {
	return entries.map(({ id, timeslot }) => `${timeslot}:${id}`).join(", ");
}

export function validTalkGroups(entries) {
	const seen = new Set();
	return Array.isArray(entries) && entries.length <= TALK_GROUP_MAX && entries.every(({ id, timeslot }) => {
		const key = timeslot * 0x1000000 + id;
		if (!Number.isInteger(id) || id < 1 || id > 0xFFFFFE || (timeslot !== 1 && timeslot !== 2) || seen.has(key)) {
			return false;
		}
		seen.add(key);
		return true;
	});
}

function record(bytes, number) {
	const offset = TALK_GROUP_START + (number - 1) * INLINE_MAX * 4;
	return bytes.subarray(offset, offset + INLINE_MAX * 4);
}

function decodeEntries(data) {
	const entries = [];
	for (let i = 0; i < data.length; i += 4) {
		const tag = data[i + 3];
		if (tag === 0xFF && data.subarray(i).every((byte) => byte === 0xFF)) break;
		if (tag !== TAG && tag !== TAG + 1) return [];
		entries.push({ id: data[i] | (data[i + 1] << 8) | (data[i + 2] << 16), timeslot: tag - TAG + 1 });
	}
	return validTalkGroups(entries) ? entries : [];
}

const hasOverflow = (bytes) => MAGIC.every((byte, i) => bytes[OVERFLOW + i] === byte);
const blank = (bytes) => bytes.every((byte) => byte === 0xFF || byte === 0);

export function decodeTalkGroups(bytes, number) {
	const entries = decodeEntries(record(bytes, number));
	if (entries.length !== INLINE_MAX || !hasOverflow(bytes)) return entries;
	const address = OVERFLOW + MAGIC.length + (number - 1) * 2;
	const descriptor = bytes[address] | (bytes[address + 1] << 8);
	if (descriptor === 0xFFFF) return entries;
	const start = descriptor & 0x1FF, count = descriptor >> 9;
	if (!count || count > TALK_GROUP_MAX - INLINE_MAX || start + count > POOL_MAX) return entries;
	const extra = decodeEntries(bytes.subarray(POOL + 4 * start, POOL + 4 * (start + count)));
	const combined = [...entries, ...extra];
	return extra.length === count && validTalkGroups(combined) ? combined : entries;
}

function putEntry(data, offset, { id, timeslot }) {
	data.set([id & 0xFF, (id >> 8) & 0xFF, id >> 16, TAG + timeslot - 1], offset);
}

export function encodeTalkGroups(bytes, channels) {
	const byNumber = new Map(channels.filter((channel) => channel.mode === "dmr").map((channel) => [channel.number, channel]));
	const extraCount = [...byNumber.values()].reduce((sum, channel) => sum + Math.max(0, (channel.talkGroups?.length ?? 0) - INLINE_MAX), 0);
	const extraRegion = bytes.subarray(OVERFLOW, TALK_GROUP_END);
	if (extraCount > POOL_MAX) throw new Error("Operating talkgroup storage is full");
	if (extraCount && !hasOverflow(bytes) && !blank(extraRegion))
		throw new Error("Operating talkgroups overlap an unknown EEPROM extension");
	if (hasOverflow(bytes) || extraCount) extraRegion.fill(0xFF);
	if (extraCount) bytes.set(MAGIC, OVERFLOW);
	let tail = 0;
	for (let n = 1; n <= 1024; n++) {
		const data = record(bytes, n), entries = byNumber.get(n)?.talkGroups ?? [];
		const known = blank(data) || decodeEntries(data).length > 0;
		if (!known) {
			if (entries.length) throw new Error("Channel talkgroup data overlaps an unknown EEPROM record");
			continue;
		}
		if (!validTalkGroups(entries)) throw new Error("Invalid channel talkgroups");
		if (!entries.length && !decodeEntries(data).length) continue;
		data.fill(0xFF);
		entries.slice(0, INLINE_MAX).forEach((entry, i) => putEntry(data, i * 4, entry));
		const extra = entries.slice(INLINE_MAX);
		if (extra.length) {
			const descriptor = tail | (extra.length << 9);
			const address = OVERFLOW + MAGIC.length + (n - 1) * 2;
			bytes[address] = descriptor & 0xFF;
			bytes[address + 1] = descriptor >> 8;
			for (const entry of extra) putEntry(bytes, POOL + 4 * tail++, entry);
		}
	}
}

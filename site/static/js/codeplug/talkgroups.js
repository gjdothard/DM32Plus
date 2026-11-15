// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Ordered DMR operating entries in the RadioFoundry EEPROM extension.
export const TALK_GROUP_START = 0xB000;
export const TALK_GROUP_MAX = 4;
export const TALK_GROUP_END = TALK_GROUP_START + 1024 * TALK_GROUP_MAX * 4;
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
	const offset = TALK_GROUP_START + (number - 1) * TALK_GROUP_MAX * 4;
	return bytes.subarray(offset, offset + TALK_GROUP_MAX * 4);
}

export function decodeTalkGroups(bytes, number) {
	const data = record(bytes, number), entries = [];
	if (data.length !== TALK_GROUP_MAX * 4) return entries;
	for (let i = 0; i < data.length; i += 4) {
		const tag = data[i + 3];
		if (tag === 0xFF && data.subarray(i).every((byte) => byte === 0xFF)) break;
		if (tag !== TAG && tag !== TAG + 1) return [];
		entries.push({ id: data[i] | (data[i + 1] << 8) | (data[i + 2] << 16), timeslot: tag - TAG + 1 });
	}
	return validTalkGroups(entries) ? entries : [];
}

export function encodeTalkGroups(bytes, channels) {
	const byNumber = new Map(channels.filter((channel) => channel.mode === "dmr").map((channel) => [channel.number, channel]));
	for (let n = 1; n <= 1024; n++) {
		const data = record(bytes, n), entries = byNumber.get(n)?.talkGroups ?? [];
		const known = data.every((byte) => byte === 0xFF || byte === 0) || decodeTalkGroups(bytes, n).length > 0;
		if (!known) {
			if (entries.length) throw new Error("Channel talkgroup data overlaps an unknown EEPROM record");
			continue;
		}
		if (!validTalkGroups(entries)) throw new Error("Invalid channel talkgroups");
		if (!entries.length && !decodeTalkGroups(bytes, n).length) continue;
		data.fill(0xFF);
		entries.forEach(({ id, timeslot }, index) => {
			data.set([id & 0xFF, (id >> 8) & 0xFF, id >> 16, TAG + timeslot - 1], index * 4);
		});
	}
}

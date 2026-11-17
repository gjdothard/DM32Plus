// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard

import { CodeplugImage } from "./image.js";
import { decode } from "./model.js";
import { CONTACTS, CONTACT_SIZE, LIMITS, encodeContact } from "./records.js";
import { POOL_END, POOL_START, readPool, writePool } from "./talkgroup-pool.js";

function contactRecord(bytes, number) {
	const offset = CONTACTS + (number - 1) * CONTACT_SIZE;
	return bytes.subarray(offset, offset + CONTACT_SIZE);
}

function erased(record) {
	return record.every(byte => byte === 0xFF);
}

export function convertOperatingTalkgroups(input) {
	if (input.length < CONTACTS + LIMITS.contacts * CONTACT_SIZE)
		throw new Error("Input must be a binary codeplug backup with the complete contact table");
	if (readPool(input)) throw new Error("This codeplug already uses the shared talkgroup pool");
	if (!erased(input.subarray(POOL_START, POOL_END)))
		throw new Error("The new pool banks contain unrecognized data");

	const image = CodeplugImage.from(input);
	const model = decode(image);
	const overflow = input.subarray(0xF000, 0x10000);
	const legacyHeader = [0x52, 0x46, 0x54, 0x47, 2, 0, 0, 0];
	const unrecognizedOverflow = !legacyHeader.every((byte, i) => byte === overflow[i]) &&
		overflow.some(byte => byte !== 0 && byte !== 0xFF);
	const groups = new Map();
	for (const contact of model.contacts) {
		if (contact.type !== "group") continue;
		if (!groups.has(contact.id)) groups.set(contact.id, []);
		groups.get(contact.id).push(contact);
	}

	const free = [];
	for (let n = 1; n <= LIMITS.contacts; n++) {
		if (erased(contactRecord(image.bytes, n))) free.push(n);
	}

	const lists = new Map(), created = [], ambiguous = [];
	let used = 0, nextFree = 0;
	for (const channel of model.channels) {
		if (channel.mode !== "dmr" || !channel.talkGroups.length) continue;
		const refs = [];
		for (const { id, timeslot } of channel.talkGroups) {
			const matches = (groups.get(id) ?? []).filter(record => (record.timeslot || channel.timeslot) === timeslot);
			// When possible prefer an exact override, then the lowest saved slot.
			matches.sort((a, b) => b.timeslot - a.timeslot || a.number - b.number);
			let contact = matches[0];
			if (matches.length > 1)
				ambiguous.push({ channel: channel.number, id, timeslot, selected: contact.number,
					matches: matches.map(record => record.number) });
			if (!contact) {
				const number = free[nextFree++];
				if (!number) throw new Error(`No erased contact slots left for TG ${id} TS${timeslot}`);
				contact = { number, id, type: "group", timeslot, name: `TG ${id} TS${timeslot}` };
				encodeContact(contact, contactRecord(image.bytes, number));
				if (!groups.has(id)) groups.set(id, []);
				groups.get(id).push(contact);
				created.push(contact);
			}
			refs.push(contact.number);
		}
		lists.set(channel.number, refs);
		used += refs.length;
	}

	if (!model.channels.length) throw new Error("No channels were found in the input codeplug");
	writePool(image.bytes, lists);
	return { image, report: { channels: lists.size, assignments: used, created, ambiguous, unrecognizedOverflow } };
}

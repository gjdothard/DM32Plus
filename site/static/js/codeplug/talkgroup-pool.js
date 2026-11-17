// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard

import { crc32 } from "../flasher/bytes.js";

export const POOL_START = 0x34000;
export const BANK_SIZE = 0x4000;
export const POOL_END = POOL_START + 2 * BANK_SIZE;
export const MAX_ASSIGNMENTS = 63;
const CHANNELS = 1024;
const CONTACTS = 1024;
const HEADER = 16;
const ENTRY_BASE = HEADER + (CHANNELS + 1) * 2;
export const POOL_CAPACITY = (BANK_SIZE - ENTRY_BASE) / 2;
const MAGIC = Uint8Array.of(0x52, 0x46, 0x54, 0x50, 1, 0, 0, 0);

function wordView(bytes) {
	return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
}

function validReference(ref) {
	return Number.isInteger(ref) && ref >= 1 && ref <= CONTACTS;
}

function bank(bytes, index) {
	const start = POOL_START + index * BANK_SIZE;
	return bytes.subarray(start, start + BANK_SIZE);
}

function owned(bytes) {
	return bytes.length === BANK_SIZE && MAGIC.every((byte, i) => byte === bytes[i]);
}

function decodeBank(bytes) {
	if (!owned(bytes)) return null;
	const view = wordView(bytes);
	const generation = view.getUint32(8, true);
	if (view.getUint32(12, true) !== crc32(bytes.subarray(HEADER), crc32(bytes.subarray(8, 12)))) return null;
	const indices = new Uint16Array(CHANNELS + 1);
	for (let i = 0; i <= CHANNELS; i++) indices[i] = view.getUint16(HEADER + i * 2, true);
	if (indices[0] !== 0 || indices[CHANNELS] > POOL_CAPACITY) return null;
	const entries = [];
	for (let i = 0; i < indices[CHANNELS]; i++) {
		const ref = view.getUint16(ENTRY_BASE + i * 2, true);
		if (!validReference(ref)) return null;
		entries.push(ref);
	}
	const lists = new Map();
	for (let i = 0; i < CHANNELS; i++) {
		const count = indices[i + 1] - indices[i];
		if (count < 0 || count > MAX_ASSIGNMENTS) return null;
		if (!count) continue;
		const refs = entries.slice(indices[i], indices[i + 1]);
		if (new Set(refs).size !== refs.length) return null;
		lists.set(i + 1, refs);
	}
	return { generation, lists };
}

export function readPool(bytes) {
	const first = decodeBank(bank(bytes, 0)), second = decodeBank(bank(bytes, 1));
	if (second && (!first || ((second.generation - first.generation) | 0) > 0))
		return { ...second, index: 1 };
	return first ? { ...first, index: 0 } : null;
}

export function writePool(bytes, lists) {
	const current = readPool(bytes);
	const index = current ? 1 - current.index : 0;
	const target = bank(bytes, index);
	if (target.length !== BANK_SIZE || (!owned(target) && !target.every(byte => byte === 0xFF)))
		throw new Error("Operating talkgroup pool overlaps unknown codeplug data");

	for (const [channel, refs] of lists) {
		if (!Number.isInteger(channel) || channel < 1 || channel > CHANNELS ||
		    !Array.isArray(refs) || refs.length > MAX_ASSIGNMENTS || !refs.every(validReference) ||
		    new Set(refs).size !== refs.length) {
			throw new RangeError("Invalid operating talkgroup assignments");
		}
	}

	// Build off-image. A validation failure never changes either bank.
	const next = new Uint8Array(BANK_SIZE).fill(0xFF);
	const view = wordView(next);
	next.set(MAGIC);
	view.setUint32(8, (current?.generation ?? 0) + 1, true);
	let used = 0;
	for (let channel = 1; channel <= CHANNELS; channel++) {
		view.setUint16(HEADER + (channel - 1) * 2, used, true);
		for (const ref of lists.get(channel) ?? []) {
			if (used >= POOL_CAPACITY) throw new RangeError("Operating talkgroup pool is full");
			view.setUint16(ENTRY_BASE + used++ * 2, ref, true);
		}
	}
	view.setUint16(HEADER + CHANNELS * 2, used, true);
	view.setUint32(12, crc32(next.subarray(HEADER), crc32(next.subarray(8, 12))), true);
	target.set(next);
	return { index, used };
}

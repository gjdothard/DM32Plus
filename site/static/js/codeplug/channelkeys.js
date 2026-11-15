// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Slot references only; the private key bank is never part of a codeplug image.
export const CHANNEL_KEYS = 0x5400;
const HEADER = new Uint8Array([82, 70, 65, 69, 83, 1, 0, 0]);
const END = CHANNEL_KEYS + 8 + 2048;
const matches = (b, n = HEADER.length) => HEADER.subarray(0, n).every((v, i) => b[CHANNEL_KEYS + i] === v);

export function channelKeySlot(b, number) {
	if (!matches(b, 5)) return 0;
	if (!matches(b)) return 254;
	const a = CHANNEL_KEYS + 8 + 2 * (number - 1), s = b[a], inv = b[a + 1];
	if ((s === 255 && inv === 255) || (s === 0 && inv === 0)) return 0;
	return (s ^ inv) === 255 && (s <= 4 || s === 255) ? s : 254;
}

export function encodeChannelKeys(b, channels) {
	const slots = new Map(channels.map((c) => [c.number, c.privacySlot ?? 0]));
	const owned = matches(b);
	if (!owned && ![...slots.values()].some((s) => s !== 0)) return;
	if (!owned) {
		if (matches(b, 5) || b.subarray(CHANNEL_KEYS, END).some((v) => v !== 0 && v !== 255))
			throw new Error("channel key table contains unknown data");
		b.set(HEADER, CHANNEL_KEYS);
	}
	for (let n = 1; n <= 1024; n++) {
		const slot = slots.get(n) ?? 0;
		// Keep an invalid binding intact until the user explicitly replaces it.
		if (slot === 254 && channelKeySlot(b, n) === 254) continue;
		if (!Number.isInteger(slot) || (slot > 4 && slot !== 255) || slot < 0)
			throw new Error("invalid channel key slot");
		if (channelKeySlot(b, n) === slot) continue;
		const a = CHANNEL_KEYS + 8 + 2 * (n - 1);
		b[a] = slot;
		b[a + 1] = slot ^ 255;
	}
}

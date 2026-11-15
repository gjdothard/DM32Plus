// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Key programming is separate from codeplug files and browser drafts. No key bytes are returned by the radio.
import { concat, le32, sleep } from "../flasher/bytes.js";

export class AesKeyError extends Error {}

export function keyBytes(text) {
	if (!/^[0-9a-f]{64}$/i.test(text.trim())) throw new AesKeyError("key");
	return Uint8Array.from(text.trim().match(/../g), (pair) => parseInt(pair, 16));
}

export class AesKeys {
	constructor(link, { random = (a) => crypto.getRandomValues(a), pause = sleep } = {}) {
		this.link = link;
		this.random = random;
		this.pause = pause;
	}
	async request(payload) {
		await this.pause(30);
		const data = concat(new Uint8Array([36]), payload);
		try {
			return await this.link.request(0x0A, data);
		} catch (e) {
			throw new AesKeyError(e.status === 3 ? "busy" : [1, 2, 4].includes(e.status) ? "firmware" : "failed");
		} finally {
			data.fill(0);
		}
	}
	async list() {
		const r = await this.request(new Uint8Array([0]));
		if (r.length !== 8 || r[0] !== 1 || r[1] !== 4 || r[2] > 4) throw new AesKeyError("firmware");
		const state = { active: r[2], ready: !!r[3], slots: Array.from(r.subarray(4), (id, index) => ({ slot: index + 1, id })) };
		if (!state.ready) {
			// Distinguish the general foreign-region guard from a problem opening the private key bank.
			// STORAGE INFO is read only; preparation always needs a separate, explicit action.
			state.storage = await this.link.storageInfo?.().catch(() => null) ?? null;
		}
		return state;
	}
	async program(slot, id, text) {
		if (!Number.isInteger(slot) || slot < 1 || slot > 4 || !Number.isInteger(id) || id < 1 || id > 255)
			throw new AesKeyError("id");
		const key = keyBytes(text);
		const random = new Uint32Array(1);
		do { this.random(random); } while (!random[0]);
		const packet = concat(new Uint8Array([1, slot, id]), le32(random[0]), key);
		try { await this.request(packet); }
		finally { key.fill(0); packet.fill(0); random.fill(0); }
	}
	async remove(slot) {
		if (!Number.isInteger(slot) || slot < 1 || slot > 4) throw new AesKeyError("id");
		const before = await this.list();
		await this.request(new Uint8Array([2, slot]));
		if (before.active === slot) {
			for (let n = 0; n < 20; n++) {
				const v = await this.current();
				if (Number(v["dmr.aes_channel_slot"])) return;
				if (v["dmr.aes_key_id"] === "0" && v["dmr.privacy"] === "0") return;
				await this.pause(100);
			}
			throw new AesKeyError("failed");
		}
	}
	async select(slot) {
		if (!Number.isInteger(slot) || slot < 0 || slot > 4) throw new AesKeyError("id");
		const before = await this.current();
		await this.request(new Uint8Array([3, slot]));
		const state = await this.list();
		if (state.active !== slot) throw new AesKeyError("failed");
		if (Number(before["dmr.aes_channel_slot"])) return state;
		for (let n = 0; n < 20; n++) {
			const v = await this.current();
			if (!slot && v["dmr.aes_key_id"] === "0" && v["dmr.privacy"] === "0") return state;
			if (slot && v["dmr.aes_serial"] !== before["dmr.aes_serial"] &&
				v["dmr.aes_key_id"] === String(state.slots[slot - 1].id)) return state;
			await this.pause(100);
		}
		throw new AesKeyError("failed");
	}
	async current() {
		const r = await this.link.request(0x09, new Uint8Array([2]));
		return Object.fromEntries(new TextDecoder().decode(r.subarray(5)).trim().split("\n").map((s) => {
			const i = s.indexOf("=");
			return [s.slice(0, i), s.slice(i + 1)];
		}));
	}
}

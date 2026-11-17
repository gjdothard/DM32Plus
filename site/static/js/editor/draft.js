// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Serialize draft writes so an older transaction cannot replace a newer edit.
export class DraftSaver {
	constructor({ snapshot, save, update, delay = 800 }) {
		Object.assign(this, { snapshot, save, update, delay });
		this.revision = 0;
		this.pending = Promise.resolve();
		this.scheduled = false;
	}

	schedule() {
		this.revision++;
		this.scheduled = true;
		clearTimeout(this.timer);
		this.update("saving");
		this.timer = setTimeout(() => this.flush(), this.delay);
	}

	restored() {
		this.revision++;
		this.scheduled = false;
		clearTimeout(this.timer);
		this.update("saved");
	}

	flush() {
		if (!this.scheduled) return this.pending;
		this.scheduled = false;
		clearTimeout(this.timer);
		const revision = this.revision;
		let snapshot;
		try {
			snapshot = this.snapshot();
		} catch {
			this.update("unavailable");
			return this.pending;
		}
		this.pending = this.pending.then(async () => {
			try {
				const saved = await this.save(snapshot);
				if (revision === this.revision) this.update(saved === false ? "unavailable" : "saved");
			} catch {
				if (revision === this.revision) this.update("unavailable");
			}
		});
		return this.pending;
	}
}

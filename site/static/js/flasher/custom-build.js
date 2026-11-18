// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Product build identity is separate from the chosen firmware version and its feature set.
import { releaseKey } from "./version.js";
export function publishedSelection(selection, options = {}) {
	return Object.keys(options).length === 0 && selection.on.size === selection.radio.config.full.length
		&& selection.radio.config.full.every((name) => selection.on.has(name));
}

export function buildSelectionKey(selection, ref, options = {}) {
	return JSON.stringify([ref || "", selection.radio.id, Object.entries(options).sort(), [...selection.on].sort()]);
}

export function checkCustomManifest(manifest, selection, product, version, testing = false) {
	const wanted = [...selection.on].sort();
	const tag = releaseKey(version)?.[0] === 0 ? `${testing ? "RC-" : ""}${product.prefix}-${version}` : version;
	if (manifest.product !== product.name || manifest.board !== product.board || manifest.version !== version
		|| manifest.source_tag !== tag
		|| manifest.variant !== "custom" || manifest.codec?.included !== false
		|| JSON.stringify(manifest.selected_features) !== JSON.stringify(wanted)
		|| JSON.stringify((manifest.features || "").split(",").filter(Boolean).sort()) !== JSON.stringify(wanted))
		throw new Error("Custom firmware does not match the selected product, version, or features");
}

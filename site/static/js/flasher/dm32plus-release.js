// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Resolve and verify firmware for both installation and upgrade.
import { verify } from "./manifest.js";

export const PRODUCT = "DM32Plus";
const BOARD = "dm32";
const RELEASES = "https://api.github.com/repos/gjdothard/DM32Plus/releases";
const VERSION = /^v(\d{2})\.(\d{1,2})(?:\.(\d+))?(?:b(\d+))?$/;
export const CHANNELS = ["bundled", "stable", "development"];

export function newestRelease(releases, channel) {
	if (channel !== "stable" && channel !== "development") return null;
	const candidates = [];
	for (const release of releases) {
		const version = /^dm32plus-(.+)$/.exec(release.tag_name)?.[1];
		const match = VERSION.exec(version ?? "");
		if (!match || release.draft || typeof release.prerelease !== "boolean" ||
			release.prerelease !== (channel === "development") || !Array.isArray(release.assets)) continue;
		const base = `dm32plus-dm32-${version}`;
		const image = release.assets.find((asset) => asset.name === base + ".bin" && asset.state === "uploaded");
		const manifest = release.assets.find((asset) => asset.name === base + ".json" && asset.state === "uploaded");
		const digest = /^sha256:([a-f0-9]{64})$/.exec(image?.digest ?? "")?.[1];
		if (!digest || !manifest) continue;
		const rank = [+match[1], +match[2], +(match[3] ?? 0), match[4] === undefined ? Infinity : +match[4]];
		candidates.push({ rank, version, digest, base });
	}
	candidates.sort((a, b) => {
		for (let i = 0; i < a.rank.length; i++)
			if (a.rank[i] !== b.rank[i]) return b.rank[i] - a.rank[i];
		return 0;
	});
	const latest = candidates[0];
	if (!latest) return null;
	return {
		board: BOARD, product: PRODUCT, variant: "full", version: latest.version, sha256: latest.digest, online: true,
		image: `firmware/dm32/${latest.base}.bin`, manifest: `firmware/dm32/${latest.base}.json`,
	};
}

export async function lookupRelease(channel, fetchFile = fetch) {
	const releases = [];
	for (let page = 1; ; page++) {
		const response = await fetchFile(`${RELEASES}?per_page=100&page=${page}`, { cache: "no-store" });
		if (!response.ok) throw new Error("GitHub release catalog is unavailable");
		const batch = await response.json();
		if (!Array.isArray(batch)) throw new Error("Invalid GitHub release catalog");
		releases.push(...batch);
		if (batch.length < 100) return newestRelease(releases, channel);
	}
}

export function supportsBundledFirmware(board) {
	return board === BOARD;
}

export function bundledRelease(releases, board) {
	if (!supportsBundledFirmware(board)) return null;
	return releases.findLast((release) => release.board === board && release.product === PRODUCT && release.variant === "full") ?? null;
}

export async function loadBundledRelease(release, root, boards, fetchFile = fetch) {
	if (!release || release.product !== PRODUCT || release.board !== BOARD)
		throw new Error("No DM32Plus release is available for this radio");
	const filename = /^firmware\/dm32\/(dm32plus-dm32-[A-Za-z0-9.]+)\.bin$/.exec(release.image)?.[1];
	if (!filename || release.manifest !== `firmware/dm32/${filename}.json`)
		throw new Error("The bundled DM32Plus release has invalid file paths");
	const path = (file) => release.online ? root + "_dm32plus_release/" + file.split("/").at(-1) : root + file;
	const [manifestResponse, imageResponse] = await Promise.all([fetchFile(path(release.manifest)), fetchFile(path(release.image))]);
	if (!manifestResponse.ok || !imageResponse.ok) throw new Error("The bundled DM32Plus release files are missing");
	const manifest = await manifestResponse.json();
	if (manifest.product !== PRODUCT || manifest.version !== release.version ||
		manifest.variant !== "full" || manifest.codec?.included !== false ||
		manifest.image?.file !== filename + ".bin" || manifest.image.sha256 !== release.sha256)
		throw new Error("The firmware manifest does not match the bundled DM32Plus release");
	const image = new Uint8Array(await imageResponse.arrayBuffer());
	const board = await verify(manifest, image, boards, BOARD);
	return { manifest, image, board, source: `${PRODUCT} ${manifest.version}`, origin: release.online ? "github" : "bundled", variant: "full" };
}

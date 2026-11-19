// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Resolve and verify firmware for both installation and upgrade.
import product from "../product.js";
import { verify } from "./manifest.js";
import { releaseKey } from "./version.js";

export const PRODUCT = product.name;
const BOARD = product.board;
const RELEASES = `https://api.github.com/repos/${product.repository}/releases`;
export const CHANNELS = ["bundled", "stable", "development", "testing"];

export function newestRelease(releases, channel) {
	if (!CHANNELS.includes(channel) || channel === "bundled") return null;
	const candidates = [];
	for (const release of releases) {
		if (typeof release.tag_name !== "string") continue;
		let version = release.tag_name, legacy = false, testing = false;
		for (const prefix of ["RC-" + product.prefix + "-", product.prefix + "-"]) {
			if (!version.startsWith(prefix)) continue;
			testing = prefix.startsWith("RC-");
			version = version.slice(prefix.length);
			legacy = true;
			break;
		}
		const rank = releaseKey(version);
		if (!rank || (legacy ? rank[0] !== 0 : rank[0] !== 1)) continue;
		const expected = rank[4] === 2 ? "stable" : testing || rank[4] === 1 ? "testing" : "development";
		if (expected !== channel || release.draft || typeof release.prerelease !== "boolean" ||
			release.prerelease !== (rank[4] < 2) || !Array.isArray(release.assets)) continue;
		testing = expected === "testing";
		const base = `${product.prefix}-${BOARD}-${version}`;
		const image = release.assets.find((asset) => asset.name === base + ".bin" && asset.state === "uploaded");
		const manifest = release.assets.find((asset) => asset.name === base + ".json" && asset.state === "uploaded");
		const digest = /^sha256:([a-f0-9]{64})$/.exec(image?.digest ?? "")?.[1];
		if (!digest || !manifest) continue;
		candidates.push({ rank, version, digest, base, testing });
	}
	candidates.sort((a, b) => {
		for (let i = 0; i < a.rank.length; i++)
			if (a.rank[i] !== b.rank[i]) return b.rank[i] - a.rank[i];
		return 0;
	});
	const latest = candidates[0];
	if (!latest) return null;
	return {
		board: BOARD, product: PRODUCT, variant: "full", version: latest.version, sha256: latest.digest, online: true, testing: latest.testing,
		image: `firmware/${BOARD}/${latest.base}.bin`, manifest: `firmware/${BOARD}/${latest.base}.json`,
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
		throw new Error(`No ${PRODUCT} release is available for this radio`);
	const filename = `${product.prefix}-${BOARD}-${release.version}`;
	if (!releaseKey(release.version) || release.image !== `firmware/${BOARD}/${filename}.bin` ||
		release.manifest !== `firmware/${BOARD}/${filename}.json`)
		throw new Error(`The bundled ${PRODUCT} release has invalid file paths`);
	const legacy = releaseKey(release.version)[0] === 0;
	const endpoint = `_${product.prefix}_release/` + (legacy && release.testing ? "RC/" : "");
	const path = (file) => release.online ? root + endpoint + file.split("/").at(-1) : root + file;
	const [manifestResponse, imageResponse] = await Promise.all([fetchFile(path(release.manifest)), fetchFile(path(release.image))]);
	if (!manifestResponse.ok || !imageResponse.ok) throw new Error(`The bundled ${PRODUCT} release files are missing`);
	const manifest = await manifestResponse.json();
	if (manifest.product !== PRODUCT || manifest.version !== release.version ||
		manifest.variant !== "full" || manifest.codec?.included !== false ||
		manifest.image?.file !== filename + ".bin" || manifest.image.sha256 !== release.sha256)
		throw new Error(`The firmware manifest does not match the bundled ${PRODUCT} release`);
	const image = new Uint8Array(await imageResponse.arrayBuffer());
	const board = await verify(manifest, image, boards, BOARD);
	return { manifest, image, board, source: `${release.testing ? "RC " : ""}${PRODUCT} ${manifest.version}`, origin: release.online ? "github" : "bundled", variant: "full" };
}

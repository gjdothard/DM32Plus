// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
// Use the release shipped with this CPS for both installation and upgrade.
import { verify } from "./manifest.js";

export const PRODUCT = "DM32Plus";
const BOARD = "dm32";

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
	const [manifestResponse, imageResponse] = await Promise.all([
		fetchFile(root + release.manifest),
		fetchFile(root + release.image),
	]);
	if (!manifestResponse.ok || !imageResponse.ok) throw new Error("The bundled DM32Plus release files are missing");
	const manifest = await manifestResponse.json();
	if (manifest.product !== PRODUCT || manifest.version !== release.version ||
		manifest.variant !== "full" || manifest.codec?.included !== false ||
		manifest.image?.file !== filename + ".bin" || manifest.image.sha256 !== release.sha256)
		throw new Error("The firmware manifest does not match the bundled DM32Plus release");
	const image = new Uint8Array(await imageResponse.arrayBuffer());
	const board = await verify(manifest, image, boards, BOARD);
	return { manifest, image, board, source: `${PRODUCT} ${manifest.version}`, origin: "bundled", variant: "full" };
}

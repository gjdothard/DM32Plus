// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard

import { readFile, writeFile } from "node:fs/promises";
import { convertOperatingTalkgroups } from "../site/static/js/codeplug/convert-talkgroups.js";

const [source, destination, ...extra] = process.argv.slice(2);
if (!source || extra.length || source === "--help") {
	console.error("Usage: node tools/convert-talkgroups.mjs <original-codeplug.bin> [converted-codeplug.bin]");
	console.error("Omit the output path to preview the conversion without creating a file.");
	process.exitCode = source === "--help" ? 0 : 2;
} else {
	try {
		const { image, report } = convertOperatingTalkgroups(await readFile(source));
		if (destination) await writeFile(destination, image.bytes, { flag: "wx" });
		console.log(JSON.stringify({ ...report, output: destination ?? null }, null, 2));
	} catch (error) {
		console.error(error.message);
		process.exitCode = 1;
	}
}

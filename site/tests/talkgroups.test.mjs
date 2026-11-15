// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
import assert from "node:assert/strict";
import { test } from "node:test";
import { decode, empty, encode, newChannel } from "../static/js/codeplug/model.js";
import { TALK_GROUP_START, decodeTalkGroups, validTalkGroups } from "../static/js/codeplug/talkgroups.js";

test("an independent ordered list survives the CPS codeplug round trip", () => {
	const model = empty();
	const repeater = newChannel(1);
	Object.assign(repeater, {
		name: "Repeater", mode: "dmr", rxHz: 443975000, txHz: 448975000, colorCode: 1,
		talkGroups: [{ id: 3100, timeslot: 1 }, { id: 3100000, timeslot: 2 }, { id: 3100, timeslot: 2 }],
	});
	model.channels.push(repeater);
	const image = encode(model);
	assert.deepEqual([...image.bytes.subarray(TALK_GROUP_START, TALK_GROUP_START + 4)], [0x1C, 0x0C, 0, 0xA0]);
	assert.deepEqual(decode(image).channels[0].talkGroups, repeater.talkGroups);
	assert.equal(decode(image).channels[0].rxList, null);
	repeater.mode = "fm";
	model.base = image;
	assert.deepEqual(decodeTalkGroups(encode(model).bytes, 1), []);
});

test("duplicate pairs, invalid slots and unknown records are rejected", () => {
	const pair = { id: 3100, timeslot: 2 };
	assert.equal(validTalkGroups([pair, pair]), false);
	assert.equal(validTalkGroups([{ id: 3100, timeslot: 3 }]), false);
	const model = empty();
	model.base.bytes[TALK_GROUP_START + 3] = 0xE0;
	assert.equal(encode(model).bytes[TALK_GROUP_START + 3], 0xE0);
	const repeater = newChannel(1);
	Object.assign(repeater, { mode: "dmr", talkGroups: [pair] });
	model.channels.push(repeater);
	assert.throws(() => encode(model), /unknown EEPROM record/);
});

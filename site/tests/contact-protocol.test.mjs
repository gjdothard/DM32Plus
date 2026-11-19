// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
import assert from "node:assert/strict";
import { test } from "node:test";
import { empty, newContact, newChannel, encode, decode, validate } from "../static/js/codeplug/model.js";
import { contactAllowed, decodeContactProtocols, CONTACT_PROTOCOL_KEY } from "../static/js/codeplug/contact-protocol.js";
import { setKind } from "../static/js/codeplug/channelmodes.js";
import { decodeArea, getValue, deleteValue, putArea } from "../static/js/codeplug/kv.js";

test("same-number DMR and P25 groups retain separate protocol scopes and names", () => {
	const model = empty(); model.sharedTalkgroups = true;
	const dmr = newContact({ number: 1, id: 1234, name: "DMR Operations" });
	const p25 = newContact({ number: 2, id: 1234, name: "P25 Operations", protocol: "p25" });
	const both = newContact({ number: 3, id: 91, name: "Shared", protocol: "both" });
	model.contacts.push(dmr, p25, both);
	const channel = newChannel({ number: 1, name: "P25", mode: "dmr", rxOnly: true });
	model.channels.push(channel); setKind(model, channel, 2); channel.talkgroupRefs = [p25, both];
	const saved = decode(encode(model));
	assert.deepEqual(saved.contacts.map(contact => [contact.name, contact.protocol]),
		[["DMR Operations", "dmr"], ["P25 Operations", "p25"], ["Shared", "both"]]);
	assert.equal(contactAllowed(saved.contacts[0], saved.channels[0]), false);
	assert.equal(contactAllowed(saved.contacts[1], { kind: 0, mode: "dmr" }), false);
	assert.equal(contactAllowed(saved.contacts[2], saved.channels[0]), true);
	saved.channels[0].talkgroupRefs.push(saved.contacts[0]);
	assert.ok(validate(saved).some(problem => problem.code === "talkgroup_protocol"));
});

test("legacy P25 assignments migrate by usage, and stale contact slots cannot inherit scope", () => {
	const model = empty(); model.sharedTalkgroups = true;
	const contact = newContact({ number: 1, id: 1234, name: "Operations", protocol: "p25" });
	model.contacts.push(contact);
	const channel = newChannel({ number: 1, name: "P25", rxOnly: true });
	model.channels.push(channel); setKind(model, channel, 2); channel.talkgroupRefs = [contact];
	const image = encode(model), area = decodeArea(image.bytes);
	putArea(image.bytes, deleteValue(area.entries, CONTACT_PROTOCOL_KEY));
	const saved = decode(image);
	assert.equal(saved.contacts[0].protocol, "p25");
	const rewritten = encode(saved);
	assert.equal(getValue(decodeArea(rewritten.bytes).entries, CONTACT_PROTOCOL_KEY)[1], 2);
	saved.channels.length = 0;
	saved.contacts[0].id = 5678;
	decodeContactProtocols(saved, rewritten.bytes);
	assert.equal(saved.contacts[0].protocol, "dmr");
});

test("protocol records count toward metadata capacity before writing", () => {
	const model = empty(); model.sharedTalkgroups = true;
	for (let i = 1; i <= 400; i++) model.contacts.push(newContact({ number: i, id: i, name: `Group ${i}`, protocol: "p25" }));
	assert.ok(validate(model).some(problem => problem.code === "settings_area_full"));
});

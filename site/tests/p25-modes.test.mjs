// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Garrett Dothard
import assert from "node:assert/strict";
import { test } from "node:test";
import { spawn } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { empty, newChannel, encode, decode, remove, validate } from "../static/js/codeplug/model.js";
import { setKind, newSystem } from "../static/js/codeplug/channelmodes.js";
import { decodeArea, putArea, KV_ADDR, getValue } from "../static/js/codeplug/kv.js";
import { prepareModeStorage } from "../static/js/codeplug/modecompat.js";
import { columns } from "../static/js/editor/views.js";
import { History } from "../static/js/editor/undo.js";
import { CpsSerial, keepIdentity, writeRanges } from "../static/js/codeplug/transport.js";
import { exportTable } from "../static/js/codeplug/csv.js";
import { exportQdmr } from "../static/js/codeplug/qdmr.js";

function conventional() {
	const m = empty(), c = newChannel({ number: 1, name: "P25 local", rxHz: 155000000, txHz: 155000000 });
	m.channels.push(c); setKind(m, c, 2); c.tagTx = false;
	c.params = { ...c.params, rxNac: 0x123, txNac: 0x456 };
	return m;
}
function trunked() {
	const m = conventional(), s = newSystem(m, 3, "Local system");
	s.number = 1; s.fields = { ...s.fields, wacn: 0x12345, sysid: 0x123, ccNac: 0x321 };
	s.entries.push({ cls: 0, group: 0, control: true, iden: 1, chanNo: 2, rxHz: 450000000, txHz: 455000000 });
	m.systems.push(s); setKind(m, m.channels[0], 3); m.channels[0].system = s;
	return m;
}

test("P25 NAC and logical Receive only survive binary save and grid edits", () => {
	const m = decode(encode(conventional())), c = m.channels[0];
	assert.equal(c.kind, 2); assert.equal(c.tagTx, false); assert.equal(c.rxOnly, true);
	assert.deepEqual(c.params, { squelch: 0, rxNac: 0x123, txNac: 0x456 });
	const f = columns("channels", m, k => k).find(x => x.key === "rxOnly");
	assert.equal(f.get(c), true); f.set(c, false);
	assert.equal(c.tagTx, true); assert.equal(c.rxOnly, true);
	assert.equal(decode(encode(m)).channels[0].tagTx, true);
});

test("trunk identity, controls, system listen only and channel permission remain independent", () => {
	const m = trunked(); m.channels[0].tagTx = true;
	const saved = decode(encode(m)), s = saved.systems[0];
	assert.equal(s.tx, false); assert.equal(saved.channels[0].tagTx, true);
	assert.equal(saved.channels[0].system, s); assert.equal(s.fields.wacn, 0x12345);
	assert.equal(s.entries[0].rxHz, 450000000);
	s.tx = true; saved.channels[0].tagTx = false;
	const again = decode(encode(saved)); assert.equal(again.systems[0].tx, true);
	assert.equal(again.channels[0].tagTx, false);
});

test("adding P25 migrates high-number inline DMR talkgroups without losing timeslots", () => {
	const old = empty(); old.channels.push(newChannel({ number: 900, name: "DMR tail", mode: "dmr", talkGroups: [{ id: 91, timeslot: 1 }, { id: 91, timeslot: 2 }] }));
	const m = decode(encode(old)), c = newChannel({ number: 1, name: "P25" }); m.channels.push(c); setKind(m, c, 2);
	const saved = decode(encode(m)), dmr = saved.channels.find(x => x.number === 900);
	assert.equal(saved.sharedTalkgroups, true);
	assert.deepEqual(dmr.talkgroupRefs.map(x => [x.id, x.timeslot]), [[91, 1], [91, 2]]);
	assert.ok(writeRanges(encode(saved)).some(([a, b]) => a <= KV_ADDR && b >= KV_ADDR + 4096));
});

test("unknown storage is refused before migration mutates the model", () => {
	const m = conventional(); m.base.bytes[KV_ADDR] = 0x35;
	const before = m.contacts.slice();
	assert.throws(() => prepareModeStorage(m), { code: "mode_storage_unrecognized" });
	assert.deepEqual(m.contacts, before); assert.equal(m.sharedTalkgroups, false);
	assert.ok(validate(m).some(p => p.code === "mode_storage_unrecognized"));
});

test("unknown metadata survives edits; renaming refreshes tags and system deletion is undoable", () => {
	let m = decode(encode(trunked())); const image = encode(m), a = decodeArea(image.bytes);
	putArea(image.bytes, [...a.entries, { key: 0x1F00, value: Uint8Array.of(4, 5, 6) }]);
	m = decode(image); m.channels[0].name = "Renamed";
	assert.deepEqual([...getValue(decodeArea(encode(m).bytes).entries, 0x1F00)], [4, 5, 6]);
	assert.equal(decode(encode(m)).channels[0].kind, 3);
	const h = new History(); h.record(m); remove(m, "systems", [m.systems[0]]);
	assert.equal(m.systems.length, 0); h.undo(m); assert.equal(m.channels[0].system, m.systems[0]);
});

test("Keep radio identity preserves its P25 unit ID and lossy CSV exports are refused", () => {
	const file = conventional(), radio = conventional();
	file.modeSettings[2] = { unitId: 1234 }; radio.modeSettings[2] = { unitId: 5678 };
	assert.equal(decode(keepIdentity(encode(file), encode(radio))).modeSettings[2].unitId, 5678);
	assert.throws(() => exportTable(file, "channels"), { code: "modes_binary" });
	assert.throws(() => exportQdmr(file), { code: "modes_binary" });
});

test("unknown P25 parameter tails survive NAC edits and unsupported system plans are reported", () => {
	const image = encode(conventional()), area = decodeArea(image.bytes);
	const tag = area.entries.find(e => e.key === 0x400), extended = new Uint8Array(tag.value.length + 2);
	extended.set(tag.value); extended.set([0xAB, 0xCD], tag.value.length); tag.value = extended;
	putArea(image.bytes, area.entries);
	const m = decode(image); m.channels[0].params = { ...m.channels[0].params, rxNac: 0x789 };
	assert.deepEqual([...getValue(decodeArea(encode(m).bytes).entries, 0x400).slice(-2)], [0xAB, 0xCD]);
	const trunk = trunked(), s = trunk.systems[0];
	s.entries = Array.from({ length: 17 }, (_, i) => ({ ...s.entries[0], chanNo: i }));
	s.entries.push({ cls: 2, group: 0, tdma: true });
	const problems = validate(trunk);
	assert.ok(problems.some(p => p.code === "p25_control_limit"));
	assert.ok(problems.some(p => p.code === "p25_phase1_only"));
});

test("permission-bearing writes cannot fall back to an older transport", async () => {
	const tr = new CpsSerial({});
	await assert.rejects(tr.write(encode(conventional()), null), { code: "modes_firmware" });
});

test("managed writes refuse oversized bundles before beginning and never retry through legacy CPS", async () => {
	const tr = new CpsSerial({}), plan = [[KV_ADDR, new Uint8Array(4096)]];
	plan.codeplug = 1;
	tr.fast = { managed: true, staging: true, stageMax: 0 };
	tr.fastPlan = async () => ({ plan, bytes: 4096 });
	tr.fastSession = async () => assert.fail("oversized bundle began");
	await assert.rejects(tr.fastWrite(null, null, { progress() {} }), { code: "modes_stage_full" });
	tr.fast.stageMax = 63;
	const refusal = new Error("managed range refusal");
	tr.fastSession = async (staged, blocks, options) => {
		assert.equal(staged, true); assert.equal(options.managed, true); assert.equal(options.refused, null);
		throw refusal;
	};
	tr.cpsWrite = async () => assert.fail("unsafe legacy fallback");
	await assert.rejects(tr.fastWrite(null, null, { progress() {} }), e => e === refusal);
});

test("CPS talks to the C firmware simulator using managed staged writes", { skip: !process.env.RFOUNDRY_CPS_SIM, timeout: 60000 }, async () => {
	const dir = mkdtempSync(join(tmpdir(), "p25-cps-"));
	const p = spawn(process.env.RFOUNDRY_CPS_SIM, ["--features", "FM,DMR,CPS_FAST,P25,P25_TRUNK,DMR_TALKGROUP_POOL,IDDB_COMPACT", "--dump", join(dir, "synthetic.bin")], { stdio: ["pipe", "pipe", "inherit"] });
	const stream = { buf: new Uint8Array(), clear() { this.buf = new Uint8Array(); },
		take(n) { const b = this.buf.slice(0, n); this.buf = this.buf.slice(n); return b; },
		async write(b) { p.stdin.write(b); }, async close() {} };
	p.stdout.on("data", b => { const all = new Uint8Array(stream.buf.length + b.length); all.set(stream.buf); all.set(b, stream.buf.length); stream.buf = all; });
	const tr = new CpsSerial(stream);
	try {
		await tr.identify([{ id: "sim", codeplug: { base: 0, end: 0xE00000, names: ["sim"] } }]);
		assert.equal(tr.fast.managed, true);
		const have = await tr.read({ iddb: false }), want = encode(trunked());
		const result = await tr.write(want, have); await tr.verify(want, result.plan);
		const saved = decode(await tr.read({ iddb: false }));
		assert.equal(saved.channels[0].kind, 3); assert.equal(saved.channels[0].tagTx, false);
		assert.equal(saved.systems[0].tx, false); assert.equal(saved.systems[0].fields.sysid, 0x123);
	} finally {
		const done = new Promise(resolve => p.once("close", resolve)); p.stdin.end(); await done;
		rmSync(dir, { recursive: true, force: true });
	}
});

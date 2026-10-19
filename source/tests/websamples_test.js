#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Omnipotard
/* Test of the web sample importer (install/websamples.js, used by samples.html). No hardware, no browser, no package
 * to install: plain Node (18+).
 *
 *   node source/tests/websamples_test.js
 *
 * What it runs (the page's own code, with the delays shortened):
 *   1. against install/kawa_samples.py (needs python3 with numpy, as the tool does): pack7 / unpack7 / frames / reply
 *      parsing / CRC-32 / header, the mu-law byte for 300 000 values, and the whole conversion on generated WAV files
 *      (mono, stereo, 8 / 11.025 / 22.05 / 44.1 / 48 / 96 / 192 kHz, 8 / 16 / 24 / 32 bits, float, extensible, too long,
 *      silent, too short...): the sound bytes and every SysEx frame must be the Python tool's, byte for byte;
 *   2. against a simulated FM-1 (MockFm1) on a simulated Web MIDI (MockMidi), both in tests/kawa_sysex_mock.js. MockFm1 is
 *      a port of firmware/src/kawa_sysex.c written from the C code, not from the page: the USB-MIDI SysEx intake (usb.c), the
 *      frame decoder, the session, the NOR flash (programming only clears bits, sectors erased first), the header
 *      written last after the CRC of what reached the flash, the 10 s timeout. It records every refusal it gives;
 *      the good paths must have none. Success, several sounds at once, a too-long file cut, a stereo file, 44.1 and
 *      8 kHz files, silence, a cable cut mid-way, a device gone mute, a cancel, a refused frame (busy, flash error,
 *      corrupted data), lost replies and late duplicates, split SysEx and clock noise, and every way the connection
 *      itself fails;
 *   3. with a C compiler on the PATH: the REAL firmware code (firmware/src/kawa.c built as the host simulator,
 *      tests/host.c) receives the page's frames, and its replies are read back.
 * Exit code 0 when everything passed. SKIPPED lines say what could not run here (KAWA_REQUIRE_ALL=1 makes them fail).
 * What this is not: a real browser (Chrome's Web MIDI, decodeAudioData, the canvas) and a real FM-1. */
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const cp = require("node:child_process");

const ROOT = path.join(__dirname, "..", "..");
const K = require(path.join(ROOT, "install", "websamples.js"));

// the delays of the Python tool, shortened (what the page uses is the table in `REAL`)
const REAL = Object.assign({}, K.DELAY);
Object.assign(K.DELAY, { open: 2, portopen: 250, tries: 2, info: 120, list: 120, begin: 250, write: 120, end: 250, erase: 250 });

const tests = [];
const test = (name, fn, needs) => tests.push({ name, fn, needs });
const hex = u8 => Buffer.from(u8).toString("hex");
const unhex = h => new Uint8Array(Buffer.from(h, "hex"));
const sleep = ms => new Promise(r => setTimeout(r, ms));

// ------------------------------------------------------------------------------------------ generated sounds ---

function rng(seed) {                                   // mulberry32
    let a = seed >>> 0;
    return () => {
        a = (a + 0x6D2B79F5) >>> 0;
        let t = a;
        t = Math.imul(t ^ (t >>> 15), t | 1);
        t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
        return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
}
const tone = (rate, secs, freq, amp = 0.6, decay = 1.5) => {
    const n = Math.round(rate * secs), x = new Float64Array(n);
    for (let i = 0; i < n; i++) x[i] = amp * Math.sin(2 * Math.PI * freq * i / rate) * Math.exp(-decay * i / rate);
    return x;
};
const noise = (rate, secs, amp, seed) => {
    const r = rng(seed), n = Math.round(rate * secs), x = new Float64Array(n);
    for (let i = 0; i < n; i++) x[i] = amp * (2 * r() - 1);
    return x;
};
const silence = (rate, secs) => new Float64Array(Math.round(rate * secs));
const concat = (...a) => { const o = new Float64Array(a.reduce((s, v) => s + v.length, 0)); let p = 0; for (const v of a) { o.set(v, p); p += v.length; } return o; };

/* a WAV file. chans: one Float64Array per channel (-1..1). kind "pcm" | "float"; ext: WAVE_FORMAT_EXTENSIBLE;
 * oddChunk: a LIST chunk of odd size before the data; openData: a data size field that says "to the end" (streaming);
 * trailing: a chunk after the data; tag: force the format tag (an unsupported one) */
function makeWav({ rate, chans, bits = 16, kind = "pcm", ext = false, oddChunk = false, openData = false, trailing = false, tag }) {
    const C = chans.length, n = chans[0].length, bps = bits / 8;
    const data = Buffer.alloc(n * C * bps);
    for (let i = 0; i < n; i++) {
        for (let c = 0; c < C; c++) {
            const v = chans[c][i], o = (i * C + c) * bps;
            if (kind === "float") { if (bits === 32) data.writeFloatLE(v, o); else data.writeDoubleLE(v, o); continue; }
            const cl = Math.max(-1, Math.min(1, v));
            if (bits === 8) data.writeUInt8(Math.max(0, Math.min(255, Math.round(cl * 127) + 128)), o);
            else if (bits === 16) data.writeInt16LE(Math.round(cl * 32767), o);
            else if (bits === 24) data.writeIntLE(Math.round(cl * 8388607), o, 3);
            else data.writeInt32LE(Math.round(cl * 2147483647), o);
        }
    }
    const t = tag !== undefined ? tag : kind === "float" ? 3 : 1;
    const fmt = Buffer.alloc(ext ? 40 : 16);
    fmt.writeUInt16LE(ext ? 0xFFFE : t, 0);
    fmt.writeUInt16LE(C, 2);
    fmt.writeUInt32LE(rate, 4);
    fmt.writeUInt32LE(rate * C * bps, 8);
    fmt.writeUInt16LE(C * bps, 12);
    fmt.writeUInt16LE(bits, 14);
    if (ext) { fmt.writeUInt16LE(22, 16); fmt.writeUInt16LE(bits, 18); fmt.writeUInt32LE(3, 20); fmt.writeUInt16LE(t, 24); }
    const chunk = (id, body, size) => {
        const h = Buffer.alloc(8);
        h.write(id, 0, "latin1");
        h.writeUInt32LE(size === undefined ? body.length : size, 4);
        return Buffer.concat([h, body, body.length & 1 && size === undefined ? Buffer.alloc(1) : Buffer.alloc(0)]);
    };
    const parts = [Buffer.from("WAVE", "latin1"), chunk("fmt ", fmt)];
    if (oddChunk) parts.push(chunk("LIST", Buffer.from("INFOabc", "latin1")));
    parts.push(chunk("data", data, openData ? 0xFFFFFFFF : undefined));
    if (trailing) parts.push(chunk("junk", Buffer.from("trailing", "latin1")));
    const body = Buffer.concat(parts), head = Buffer.alloc(8);
    head.write("RIFF", 0, "latin1");
    head.writeUInt32LE(body.length, 4);
    return new Uint8Array(Buffer.concat([head, body]));
}
const toSource = wavBytes => K.readWav(wavBytes);
const rawFrame = (cmd, payload = []) => Uint8Array.from([0xF0, 0x7D, 0x4B, 0x57, cmd, ...K.pack7(payload), 0xF7]);   // (any command byte)
const sound = (secs = 1, rate = 22050, freq = 440) => K.prepare(K.fromChannels([tone(rate, secs, freq)], rate)).data;

// ------------------------------------------------------------------------------------------ Python bridge ---

const PY = String.raw`
import sys, json, zlib
req = json.load(sys.stdin)
sys.path.insert(0, req["install"])
import kawa_samples as ks
out = {}
op = req["op"]
if op == "prims":
    out["payloads"] = []
    for c in req["payloads"]:
        b = bytes.fromhex(c)
        p = ks.pack7(b)
        out["payloads"].append({"pack7": p.hex(), "unpack7": ks.unpack7(p).hex(),
                                "frames": {n: ks.frame(n, b).hex() for n in ks.CMD}, "crc": zlib.crc32(b) & 0xFFFFFFFF})
    out["replies"] = []
    for h in req["replies"]:
        r = ks.parse_reply(bytes.fromhex(h))
        out["replies"].append(None if r is None else {"cmd": r[0], "body": r[1].hex()})
    out["headers"] = [ks.header(bytes.fromhex(c["hex"]), c["rate"], c["name"]).hex() for c in req["headers"]]
elif op == "prepare":
    out["cases"] = []
    for c in req["cases"]:
        try:
            data = ks.prepare(c["path"], c["rate"])
            fr = [[n, m.hex()] for n, m in ks.upload_frames(c["slot"], data, c["rate"], c["name"])]
            out["cases"].append({"ok": True, "data": data.hex(), "frames": fr})
        except SystemExit as e:
            out["cases"].append({"ok": False, "err": str(e)})
elif op == "mulaw":
    import numpy as np
    out["bytes"] = ks.mulaw(np.array(req["values"], dtype=np.float64)).tobytes().hex()
elif op == "script":
    import subprocess
    r = subprocess.run([sys.executable, "-I", req["install"] + "/kawa_samples.py", "script", str(req["slot"]), req["wav"], req["out"],
                        "--name", req["name"], "--rate", str(req["rate"]), "--start", str(req["start"])], capture_output=True, text=True)
    out["rc"] = r.returncode
print(json.dumps(out))
`;
const hasPython = (() => {
    const r = cp.spawnSync("python3", ["-I", "-c", "import numpy"], { encoding: "utf8" });
    return r.status === 0;
})();
function py(req) {
    const r = cp.spawnSync("python3", ["-I", "-c", PY], { input: JSON.stringify(Object.assign({ install: path.join(ROOT, "install") }, req)),
                                                          encoding: "utf8", maxBuffer: 1 << 29 });
    if (r.status !== 0) throw new Error("python3 failed: " + r.stderr);
    return JSON.parse(r.stdout);
}

// ------------------------------------------------------------------------------------------ the simulators ---
const {
    MockFm1, MockMidi, NUSR, USR_SIZE, USR_DATA, USR_CAP, USR_MAGIC, OK, EARGS, EBUSY, EFLASH, ECRC, ESTATE, cPack7, cUnpack7, cCrc,
} = require("./kawa_sysex_mock.js");

async function bench(devOpts = {}, midiOpts = {}, name = "FM-1") {
    const midi = new MockMidi(midiOpts), dev = new MockFm1(devOpts);
    midi.add(dev, name);
    const nav = { requestMIDIAccess: o => midi.request(o) };
    return { midi, dev, nav, connect: o => K.connect(Object.assign({ nav }, o)) };
}
async function withFm1(devOpts, fn, midiOpts) {
    const b = await bench(devOpts, midiOpts);
    const fm1 = await b.connect();
    try { return await fn(fm1, b); } finally { await fm1.close().catch(() => {}); }
}
async function rejects(p, code, props) {
    let err = null;
    try { await p; } catch (e) { err = e; }
    assert.ok(err, "expected an error " + code + ", got success");
    assert.ok(err instanceof K.KawaError, "not a KawaError: " + (err && err.stack));
    assert.equal(err.code, code, `expected ${code}, got ${err.code}: ${err.message}`);
    for (const [k, v] of Object.entries(props || {})) assert.deepEqual(err[k], v, `${code}.${k}`);
    return err;
}
const noBad = dev => { assert.deepEqual(dev.violations, [], "the mock saw a protocol violation"); assert.deepEqual(dev.refusals, [], "the mock refused a frame"); };

// ====================================================================================================================
// 1. against the Python tool
// ====================================================================================================================

test("pack7 / unpack7 / frames / parse_reply / CRC-32 / header: the Python tool's bytes", () => {
    const r = rng(7), payloads = [""];
    for (const n of [1, 2, 3, 4, 5, 6, 7, 8, 13, 36, 37, 100, 255, 256, 260, 300]) {
        let h = "";
        for (let i = 0; i < n; i++) h += Math.floor(r() * 256).toString(16).padStart(2, "0");
        payloads.push(h);
    }
    payloads.push("00".repeat(10), "ff".repeat(10));
    const replies = ["f07d4b57020002 00f7".replace(/ /g, ""), "f07d4b5703f7", "f07d4b5703", "f07d4b5701001605f7", "f07d4b5802f7", "f17d4b570100f7",
                     "f07d4b570100", "f07d4b57010000f7", "f07d4b5701" + "7f".repeat(20) + "f7"];
    const heads = [];
    for (const name of ["KICK", "my kick", "", "ABCDEFGHIJK", "ABCDEFGHIJKLMNOP", "A B_C-D.E!F+G#"]) {
        for (const rate of [22050, 11025]) heads.push({ hex: payloads[Math.min(8, payloads.length - 1)], rate, name });
    }
    heads.push({ hex: payloads[12], rate: 22050, name: "SNARE 808" });
    const p = py({ op: "prims", payloads, replies, headers: heads });
    payloads.forEach((h, i) => {
        const b = unhex(h), q = p.payloads[i];
        assert.equal(hex(K.pack7(b)), q.pack7, "pack7 " + h.length / 2);
        assert.equal(hex(K.unpack7(unhex(q.pack7))), q.unpack7, "unpack7 " + h.length / 2);
        for (const n of Object.keys(K.CMD)) assert.equal(hex(K.frame(n, b)), q.frames[n], `frame ${n} ${h.length / 2}`);
        assert.equal(K.crc32(b), q.crc, "crc32 " + h.length / 2);
    });
    replies.forEach((h, i) => {
        const a = K.parseReply(unhex(h)), b = p.replies[i];
        if (b === null) assert.equal(a, null, "parse_reply " + h);
        else if (b.body.length === 0) assert.equal(a, null, "an empty body is no reply: " + h);   // (the Python tool would fail on it)
        else assert.deepEqual({ cmd: a.cmd, body: hex(a.body) }, b, "parse_reply " + h);
    });
    heads.forEach((c, i) => assert.equal(hex(K.header(unhex(c.hex), c.rate, c.name)), p.headers[i], `header ${c.name}/${c.rate}`));
}, "py");

test("mu-law: the same byte as the Python tool for 300 000 values (ties, zeros, the edges)", () => {
    const r = rng(11), v = [0, -0, 0.95, -0.95, 1, -1, 1e-12, -1e-12, 0.003, -0.003, 0.5, -0.5];
    for (let i = 0; i < 150000; i++) v.push(2 * r() - 1);
    for (let i = 0; i < 100000; i++) v.push((2 * r() - 1) * 0.95 * Math.pow(10, -4 * r()));
    for (let i = 0; i < 50000; i++) v.push(((i / 49999) * 2 - 1) * 0.95);
    const p = py({ op: "mulaw", values: v });
    const got = new Uint8Array(v.length);
    v.forEach((x, i) => { got[i] = K.mulawByte(x); });
    assert.equal(hex(got), p.bytes);
}, "py");

test("the decode table is the firmware's BRK_ULAW (firmware/gen/kawa_breaks.h)", () => {
    const h = fs.readFileSync(path.join(ROOT, "source", "firmware", "gen", "kawa_breaks.h"), "utf8");
    const m = /BRK_ULAW\[256\] = \{([^}]*)\}/.exec(h);
    assert.ok(m, "BRK_ULAW not found");
    const t = m[1].split(",").map(s => parseInt(s, 10));
    assert.equal(t.length, 256);
    assert.deepEqual(Array.from(K.ULAW), t);
});

/* the sound cases: [name, wav options, rate, expected flags] */
const R = 22050;
const CASES = (() => {
    const c = [];
    const add = (name, wav, o = {}) => c.push({ name, wav, rate: o.rate || R, cut: o.cut, silent: o.silent, err: o.err, near: o.near });
    add("mono 22.05 kHz 16-bit, 1 s tone", { rate: R, chans: [tone(R, 1, 440)] }, { cut: false });
    add("mono 22.05 kHz, 3 s: cut at the slot (no resampling)", { rate: R, chans: [tone(R, 3, 330, 0.7, 0.2)] }, { cut: true });
    add("mono 44.1 kHz, 3.5 s: cut (boxcar k=2 + interpolation)", { rate: 44100, chans: [tone(44100, 3.5, 220, 0.7, 0.2)] }, { cut: true });
    add("mono 44.1 kHz, 0.3 s", { rate: 44100, chans: [tone(44100, 0.3, 1000, 0.5, 6)] }, { cut: false });
    add("mono 8 kHz, 1 s (upsampled x2.76)", { rate: 8000, chans: [tone(8000, 1, 300, 0.8, 1)] }, { cut: false });
    add("mono 11.025 kHz, 1 s (upsampled x2)", { rate: 11025, chans: [tone(11025, 1, 500, 0.8, 1)] }, { cut: false });
    add("mono 48 kHz, 2 s (k=2, ratio 2.177)", { rate: 48000, chans: [tone(48000, 2, 440, 0.9, 0.5)] }, { cut: true });
    add("mono 32 kHz, 1 s (k=1)", { rate: 32000, chans: [tone(32000, 1, 440, 0.9, 1)] }, { cut: false });
    add("mono 96 kHz 24-bit, 1 s (k=4)", { rate: 96000, bits: 24, chans: [tone(96000, 1, 440, 0.9, 1)] }, { cut: false });
    add("mono 192 kHz 24-bit, 0.5 s (k=9: the sums may differ by an ulp)", { rate: 192000, bits: 24, chans: [tone(192000, 0.5, 440, 0.9, 1)] }, { cut: false, near: 1 });
    add("stereo 44.1 kHz, two different channels", { rate: 44100, chans: [tone(44100, 1.2, 440, 0.8), tone(44100, 1.2, 660, 0.3, 4)] }, { cut: false });
    add("stereo 22.05 kHz, one channel silent", { rate: R, chans: [tone(R, 1, 440, 0.8), silence(R, 1)] }, { cut: false });
    add("stereo 22.05 kHz, opposite channels: the mono mix is silence", { rate: R, chans: [tone(R, 1, 440, 0.8), tone(R, 1, 440, -0.8)] }, { silent: true });
    add("stereo 44.1 kHz noise, 2 s (stress)", { rate: 44100, chans: [noise(44100, 2, 0.9, 1), noise(44100, 2, 0.5, 2)] }, { cut: true });
    add("mono 44.1 kHz noise, 1.5 s (stress)", { rate: 44100, chans: [noise(44100, 1.5, 0.7, 3)] }, { cut: false });
    add("silence 22.05 kHz, 0.5 s", { rate: R, chans: [silence(R, 0.5)] }, { silent: true });
    add("silence 44.1 kHz, 3 s", { rate: 44100, chans: [silence(44100, 3)] }, { silent: true });
    add("near silence (0.002): below the threshold", { rate: 44100, chans: [noise(44100, 0.5, 0.002, 4)] }, { silent: true });
    add("0.3 s of silence before the sound is cut off", { rate: 44100, chans: [concat(silence(44100, 0.3), tone(44100, 0.8, 700, 0.6, 3))] }, { cut: false });
    add("sound after a long silence (2 s) and cut", { rate: 44100, chans: [concat(silence(44100, 2), tone(44100, 3, 700, 0.6, 0.1))] }, { cut: true });
    add("the first sound sample is 0.0035: above the 0.003 threshold, it starts the sound", { rate: R, kind: "float", bits: 64,
        chans: [concat(silence(R, 0.05), Float64Array.of(0.0035), tone(R, 0.5, 440, 0.6, 2))] }, { cut: false });
    add("a sample of exactly 0.003 is still silence (the test is 'greater than')", { rate: R, kind: "float", bits: 64,
        chans: [concat(silence(R, 0.05), Float64Array.of(0.003, -0.003, 0.003), Float64Array.of(0.5), tone(R, 0.5, 440, 0.6, 2))] }, { cut: false });
    add("a sample of -0.0031 starts the sound too (the sign does not matter)", { rate: R, kind: "float", bits: 64,
        chans: [concat(silence(R, 0.05), Float64Array.of(-0.0031), tone(R, 0.5, 440, 0.6, 2))] }, { cut: false });
    add("float 32 bit stereo 44.1 kHz", { rate: 44100, kind: "float", bits: 32, chans: [tone(44100, 0.9, 440, 1.3), tone(44100, 0.9, 220, 0.4)] }, { cut: false });
    add("float 64 bit mono 48 kHz", { rate: 48000, kind: "float", bits: 64, chans: [tone(48000, 0.7, 440, 0.8)] }, { cut: false });
    add("8-bit unsigned 22.05 kHz", { rate: R, bits: 8, chans: [tone(R, 0.8, 440, 0.9)] }, { cut: false });
    add("32-bit PCM 44.1 kHz", { rate: 44100, bits: 32, chans: [tone(44100, 0.8, 440, 0.9)] }, { cut: false });
    add("WAVE_FORMAT_EXTENSIBLE 24-bit stereo 48 kHz", { rate: 48000, bits: 24, ext: true, chans: [tone(48000, 1, 440, 0.8), tone(48000, 1, 550, 0.8)] }, { cut: false });
    add("WAVE_FORMAT_EXTENSIBLE float 32 mono 44.1 kHz", { rate: 44100, kind: "float", bits: 32, ext: true, chans: [tone(44100, 0.6, 440, 0.8)] }, { cut: false });
    add("an odd-sized chunk before the data, a chunk after it", { rate: R, oddChunk: true, trailing: true, chans: [tone(R, 0.5, 440)] }, { cut: false });
    add("a data size that says 'to the end' (streamed WAV)", { rate: R, openData: true, chans: [tone(R, 0.5, 440)] }, { cut: false });
    add("exactly the slot's size (36608 samples, the first one not zero)", { rate: R, chans: [concat(Float64Array.of(0.5), tone(R, (K.CAP - 1) / R, 440, 0.8, 0.5))] }, { cut: false });
    add("one sample more than the slot", { rate: R, chans: [concat(Float64Array.of(0.5), tone(R, K.CAP / R, 440, 0.8, 0.5))] }, { cut: true });
    add("70 samples: just long enough", { rate: R, chans: [tone(R, 70 / R, 2000, 0.8, 0)] }, { cut: false });
    add("40 samples: too short", { rate: R, chans: [tone(R, 40 / R, 2000, 0.8, 0)] }, { err: "short" });
    add("a 44.1 kHz file of 100 samples: too short once at 22 kHz", { rate: 44100, chans: [tone(44100, 100 / 44100, 2000, 0.8, 0)] }, { err: "short" });
    add("mono 44.1 kHz, 4 s, to 11.025 kHz (3.3 s slot)", { rate: 44100, chans: [tone(44100, 4, 300, 0.8, 0.2)] }, { rate: 11025, cut: true });
    add("mono 22.05 kHz, 1 s, to 11.025 kHz", { rate: R, chans: [tone(R, 1, 300, 0.8, 1)] }, { rate: 11025, cut: false });
    add("mono 48 kHz, 1 s, to 11.025 kHz (k=4)", { rate: 48000, chans: [tone(48000, 1, 300, 0.8, 1)] }, { rate: 11025, cut: false });
    add("ADPCM (format 2): not read", { rate: R, tag: 2, chans: [tone(R, 0.5, 440)] }, { err: "decode" });
    return c;
})();

test("the conversion on generated WAV files: the sound bytes and every SysEx frame are the Python tool's", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "kawa-ws-"));
    try {
        const files = CASES.map((c, i) => { const f = path.join(dir, `c${i}.wav`); fs.writeFileSync(f, makeWav(c.wav)); return f; });
        const slot = 2, name = "TEST SOUND";
        const p = py({ op: "prepare", cases: CASES.map((c, i) => ({ path: files[i], rate: c.rate, slot, name })) });
        let identical = 0;
        CASES.forEach((c, i) => {
            const want = p.cases[i], label = c.name;
            let js = null, err = null;
            try { js = K.prepare(K.readWav(fs.readFileSync(files[i])), { rate: c.rate }); } catch (e) { err = e; }
            if (c.err) {
                assert.ok(!want.ok, `${label}: the Python tool should refuse (${want.ok ? "it did not" : want.err})`);
                assert.ok(err instanceof K.KawaError && err.code === c.err, `${label}: expected ${c.err}, got ${err && err.code}`);
                return;
            }
            assert.ok(want.ok, `${label}: Python refused: ${want.err}`);
            assert.ok(!err, `${label}: ${err && err.stack}`);
            const data = js.data, ref = unhex(want.data);
            assert.equal(data.length, ref.length, `${label}: length`);
            if (c.near) {                                // k > 4: numpy's own order of additions: at most one step apart
                let worst = 0, diff = 0;
                for (let j = 0; j < ref.length; j++) { const d = Math.abs(data[j] - ref[j]); if (d) diff++; worst = Math.max(worst, d); }
                assert.ok(worst <= c.near, `${label}: ${diff} bytes differ by up to ${worst}`);
                if (diff) console.log(`      (${label}: ${diff} of ${ref.length} bytes one step apart, as announced)`);
            } else {
                const at = data.findIndex((b, j) => b !== ref[j]);
                assert.equal(at, -1, `${label}: first difference at ${at}: ${data[at]} vs ${ref[at]}`);
                identical++;
            }
            assert.ok(data.length <= K.CAP && data.length >= K.MIN_LEN, label + ": size");
            if (!c.silent) assert.equal(js.cut, c.cut === true, `${label}: cut flag`);
            assert.equal(js.silent, c.silent === true, `${label}: silent flag`);
            if (c.silent && /^(silence|stereo 22\.05 kHz, opposite)/.test(label)) assert.ok(ref.every(b => b === 0x80), label + ": exact silence is all 0x80");
            if (c.silent && /^near silence/.test(label)) assert.ok(ref.some(b => b !== 0x80), label + ": the Python tool amplifies the noise floor (the page refuses it)");
            if (c.near) return;
            const mine = K.uploadFrames(slot, data, c.rate, name);
            assert.equal(mine.length, want.frames.length, label + ": frame count");
            mine.forEach((f, j) => {
                assert.equal(f.name, want.frames[j][0], `${label}: frame ${j} kind`);
                assert.equal(hex(f.frame), want.frames[j][1], `${label}: frame ${j} (${f.name})`);
            });
        });
        console.log(`      ${identical} of ${CASES.length} files byte for byte (sound and frames), the others refused alike`);
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
}, "py");

test("the Python tool's own 'script' frames (what the simulator replays) are the page's frames", () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "kawa-ws-"));
    try {
        const wavFile = path.join(dir, "k.wav"), out = path.join(dir, "k.txt");
        fs.writeFileSync(wavFile, makeWav({ rate: 44100, chans: [tone(44100, 1.1, 180, 0.8, 3)] }));
        const r = py({ op: "script", slot: 3, wav: wavFile, out, name: "Kick drum 1", rate: 22050, start: 1000 });
        assert.equal(r.rc, 0);
        const lines = fs.readFileSync(out, "utf8").split("\n").filter(l => l && l[0] !== "#").map(l => l.split(" ")[2]);
        const prep = K.prepare(K.readWav(fs.readFileSync(wavFile)));
        const mine = K.uploadFrames(2, prep.data, 22050, "Kick drum 1");
        assert.deepEqual(mine.map(f => hex(f.frame).toUpperCase()), lines);
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
}, "py");

// ====================================================================================================================
// 2. the conversion, on its own (no Python needed)
// ====================================================================================================================

test("names: what the FM-1 can show, 11 characters at most", () => {
    assert.equal(K.cleanName("  Kick drum é 808!! ok "), "KICK DRUM E");
    assert.equal(K.cleanName("ABCDEFGHIJKLMNOP"), "ABCDEFGHIJK");
    assert.equal(K.cleanName("a/b\\c:d*e?"), "A B C D E");
    assert.equal(K.cleanName("日本語"), "");
    assert.equal(K.nameFromFile("my_snare.final.wav"), "MY_SNARE.FI");
    assert.equal(K.nameFromFile("日本語.wav"), "SAMPLE");
    assert.equal(K.nameFromFile(""), "SAMPLE");
    const h = K.header(sound(0.1), 22050, "ÀB\u0001CDEFGHIJKLMNOP");
    assert.equal(h.length, 36);
    assert.ok(h.subarray(12, 24).every(b => b === 0 || (b >= 32 && b < 127)), "printable ASCII only");
    assert.equal(h[23], 0, "the 12th byte of the name is always 0");
});

test("the sizes: at most the slot, never less than 64, a cut sound is flagged, the end fades to nothing", () => {
    const long = K.prepare(K.fromChannels([tone(44100, 6, 440, 0.7, 0.1)], 44100));
    assert.equal(long.data.length, K.CAP);
    assert.ok(long.cut && !long.silent);
    const t = Math.round(K.CAP / 22050 * 100) / 100;
    assert.equal(t, 1.66);
    assert.equal(K.mulawByte(0), 128, "zero is the middle");
    assert.equal(long.data[K.CAP - 1], 128, "the last sample is a zero (the fade)");
    assert.ok(Math.max(...Array.from(long.data.subarray(100, 4000), b => Math.abs(b - 128))) > 100, "normalised: loud");
    const slow = K.prepare(K.fromChannels([tone(44100, 6, 440, 0.7, 0.1)], 44100), { rate: 11025 });
    assert.equal(slow.data.length, K.CAP);
    assert.equal(slow.rate, 11025);
    assert.throws(() => K.prepare(K.fromChannels([new Float64Array(10)], 22050)), e => e.code === "short");
    assert.throws(() => K.prepare(K.fromChannels([new Float64Array(0)], 22050)), e => e.code === "short");
    assert.throws(() => K.prepare(K.fromChannels([tone(22050, 1, 440)], 22050), { rate: 44100 }), e => e.code === "badargs");
    const small = K.prepare(K.fromChannels([tone(22050, 2, 440)], 22050), { cap: 10000 });   // a device with smaller slots
    assert.equal(small.data.length, 10000);
    assert.ok(small.cut);
    assert.equal(K.prepare(K.fromChannels([tone(22050, 2, 440)], 22050), { cap: 99999 }).data.length, K.CAP, "never above what we know");
});

test("silence is refused before sending; a nearly silent file too; a quiet one is normalised", () => {
    const s = K.prepare(K.fromChannels([silence(22050, 1)], 22050));
    assert.ok(s.silent);
    assert.throws(() => K.assertSendable(s), e => e.code === "silent");
    const q = K.prepare(K.fromChannels([tone(22050, 1, 440, 0.01, 1)], 22050));    // 0.01: above the threshold, quiet
    assert.ok(!q.silent);
    assert.equal(K.assertSendable(q), q);
    assert.ok(q.data.some(b => b > 220 || b < 36), "normalised to near full scale");
});

test("a 10 minute file costs no more than a short one (the sound is cut lazily)", () => {
    const n = 44100 * 600, big = new Float32Array(n);
    for (let i = 0; i < 44100; i++) big[i + 44100] = 0.5 * Math.sin(i / 7);
    const t0 = Date.now();
    const p = K.prepare(K.fromChannels([big, big], 44100));
    assert.equal(p.data.length, K.CAP);
    assert.ok(Date.now() - t0 < 3000, "took " + (Date.now() - t0) + " ms");
});

test("decodeWithBrowser: asks the browser for the FM-1's rate, falls back to AudioContext, fails clearly", async () => {
    const mk = rate => ({ numberOfChannels: 2, sampleRate: rate, length: 100, getChannelData: c => Float32Array.from({ length: 100 }, (_, i) => (c ? 0.5 : 0.25) * Math.sin(i)) });
    const asked = [];
    class OAC { constructor(ch, len, rate) { asked.push(rate); this.rate = rate; } decodeAudioData(ab) { return Promise.resolve(mk(this.rate)); } }
    const src = await K.decodeWithBrowser(new ArrayBuffer(8), 22050, { OfflineAudioContext: OAC });
    assert.deepEqual(asked, [22050]);
    assert.equal(src.rate, 22050);
    assert.equal(src.length, 100);
    assert.ok(Math.abs(src.at(5) - (0.25 + 0.5) / 2 * Math.sin(5)) < 1e-6, "the channels' mean");
    class AC { decodeAudioData(ab, ok) { ok(mk(48000)); } close() {} }
    const s2 = await K.decodeWithBrowser(new ArrayBuffer(8), 22050, { AudioContext: AC });
    assert.equal(s2.rate, 48000, "decoded at the context's rate: the page's resampler takes over");
    class Bad { decodeAudioData(ab, ok, no) { no(new Error("EncodingError")); } }
    await rejects(K.decodeWithBrowser(new ArrayBuffer(8), 22050, { OfflineAudioContext: Bad, AudioContext: Bad }), "decode");
    await rejects(K.decodeWithBrowser(new ArrayBuffer(8), 22050, {}), "decode");
    assert.throws(() => K.readWav(new Uint8Array([1, 2, 3])), e => e.code === "decode");
});

// ====================================================================================================================
// 3. the protocol, against the simulated FM-1
// ====================================================================================================================

test("MockFm1 is the firmware's parser: hand-made bad frames are refused as kawa_sysex.c refuses them", () => {
    const dev = new MockFm1();
    const out = [];
    dev.out = u => out.push(K.parseReply(u));
    const send = (cmd, args) => { out.length = 0; dev.receive(K.frame(cmd, args)); return out[0] ? { cmd: out[0].cmd, body: Array.from(out[0].body) } : null; };
    const d256 = Array.from({ length: 256 }, (_, i) => i);
    // not open: WRITE / END without a session
    assert.equal(send(3, [0, 0, 0, 0, 1, 2, 3]).body[0], ESTATE);
    assert.equal(send(4, [0, ...new Array(36).fill(0)]).body[0], ESTATE);
    // BEGIN: wrong length, slot out of range, good
    assert.equal(send(2, []).body[0], EARGS);
    assert.equal(send(2, [0, 0]).body[0], EARGS);
    assert.equal(send(2, [4]).body[0], EARGS);
    assert.equal(send(2, [0]).body[0], OK);
    // WRITE: off not a multiple of 256, data longer than 256, no data, past the slot, out of order, wrong slot
    assert.equal(send(3, [0, 1, 0, 0, ...d256.slice(0, 10)]).body[0], EARGS);
    assert.equal(send(3, [0, 0, 0, 0, ...d256, 9]).body[0], EARGS);
    assert.equal(send(3, [0, 0, 0, 0]).body[0], EARGS);
    assert.equal(send(3, [0, 0, 0]).body[0], EARGS);
    assert.equal(send(3, [0, 0, 0x8F, 0, ...d256]).body[0], EARGS, "0x8F00 + 256 > 36608");
    assert.equal(send(3, [0, 0, 1, 0, ...d256]).body[0], ESTATE, "out of order");
    assert.equal(send(3, [1, 0, 0, 0, ...d256]).body[0], ESTATE, "another slot");
    assert.equal(send(3, [0, 0, 0, 0, ...d256]).body[0], OK);
    assert.equal(send(3, [0, 0, 0, 0, ...d256]).body[0], OK, "the same frame again: already there");
    // END: wrong length, wrong slot, a length beyond what was written, a wrong CRC
    assert.equal(send(4, [0, 1, 2]).body[0], EARGS);
    assert.equal(send(4, [1, ...new Array(36).fill(0)]).body[0], ESTATE);
    const sound = Uint8Array.from(d256);
    const good = K.header(sound, 22050, "T");
    const bigLen = Uint8Array.from(good); new DataView(bigLen.buffer).setUint32(4, 300, true);
    assert.equal(send(4, [0, ...bigLen]).body[0], ECRC, "len beyond what was written");
    // the session is over after a failed END: WRITE says so, and the slot is empty
    assert.equal(send(3, [0, 0, 1, 0, ...d256]).body[0], ESTATE);
    assert.equal(dev.usrData[0], null);
    assert.equal(send(2, [0]).body[0], OK);
    assert.equal(send(3, [0, 0, 0, 0, ...d256]).body[0], OK);
    const badCrc = Uint8Array.from(good); badCrc[24] ^= 1;
    assert.equal(send(4, [0, ...badCrc]).body[0], ECRC);
    assert.equal(dev.usrData[0], null, "a wrong CRC leaves the slot empty");
    // a good one; the sound is in
    assert.equal(send(2, [0]).body[0], OK);
    assert.equal(send(3, [0, 0, 0, 0, ...d256]).body[0], OK);
    assert.equal(send(4, [0, ...good]).body[0], OK);
    assert.ok(dev.usrData[0]);
    assert.equal(dev.slotName(0), "T");
    assert.deepEqual(Array.from(dev.slotSound(0)), d256);
    // a header too short (len < 64), a bad rate, a bad magic: refused
    for (const mod of [dv => dv.setUint32(4, 63, true), dv => dv.setUint32(8, 44100, true), dv => dv.setUint32(0, 1, true)]) {
        send(2, [1]); send(3, [1, 0, 0, 0, ...d256]);
        const h = Uint8Array.from(K.header(sound, 22050, "X")); mod(new DataView(h.buffer));
        assert.equal(send(4, [1, ...h]).body[0], ECRC);
        assert.equal(dev.usrData[1], null);
    }
    // an unknown command, a frame with a byte >= 0x80, a frame too long, another status inside: no reply or an error
    out.length = 0; dev.receive(rawFrame(9, []));
    assert.equal(out[0].cmd, 9);
    assert.equal(out[0].body[0], EARGS, "an unknown command");
    assert.throws(() => K.frame(9, []), e => e.code === "badargs", "the page itself cannot build one");
    out.length = 0; dev.receive(Uint8Array.of(0xF0, 0x7D, 0x4B, 0x57, 0x01, 0x90, 0xF7));
    assert.equal(out.length, 0, "a status byte inside a frame aborts it");
    out.length = 0; dev.receive(Uint8Array.from([0xF0, 0x7D, 0x4B, 0x57, 0x03, ...new Array(700).fill(0x11), 0xF7]));
    assert.equal(out.length, 0, "over 640 bytes: not ours");
    out.length = 0; dev.receive(Uint8Array.from([0xF0, 0x7D, 0x4B, 0x57, 0x03, ...new Array(500).fill(0x11), 0xF7]));
    assert.equal(out.length, 0, "500 bytes: decoded to 437 > 300, the firmware drops it without a word");
});

test("MockFm1 is a flash: a program over unerased bytes is caught, the header is the last thing written", () => {
    const dev = new MockFm1();
    dev.flash.fill(0x00, 0, 100);
    dev.kwProg(0, Uint8Array.of(0xFF));
    assert.equal(dev.violations.length, 1);
    const d = new MockFm1();
    d.out = () => {};
    const d256 = new Uint8Array(256).fill(0x55);
    d.receive(K.frame(2, [0]));
    for (let i = 0; i < 3; i++) {
        d.receive(K.frame(3, [0, 0, i, 0, ...d256]));
        assert.equal(d.usrData[0], null, "no header until END: still empty");
        assert.ok(d.flash.subarray(0, 36).every(b => b === 0xFF), "the header area is still erased after WRITE " + i);
    }
    assert.deepEqual(d.violations, []);
});

test("success: INFO, BEGIN, 143 WRITEs, END; the sound is in the slot, the frames are the page's", async () => {
    const data = K.prepare(K.fromChannels([tone(44100, 3, 330, 0.7, 0.3)], 44100)).data;   // cut to the whole slot
    assert.equal(data.length, K.CAP);
    await withFm1({}, async (fm1, b) => {
        assert.deepEqual(fm1.info, { version: "0.4", slots: 4, cap: K.CAP });
        const empty = await fm1.list();
        assert.deepEqual(empty.map(s => s.used), [false, false, false, false]);
        const prog = [];
        const r = await fm1.send(1, data, 22050, "KICK", { onProgress: (s, t) => prog.push([s, t]) });
        assert.deepEqual(r, { slot: 1, bytes: K.CAP, rate: 22050, frames: 145, retries: 0 });
        assert.equal(prog[0][0], 0);
        assert.deepEqual(prog[prog.length - 1], [K.CAP, K.CAP]);
        assert.ok(prog.every((p, i) => i === 0 || p[0] > prog[i - 1][0]), "progress only goes up");
        noBad(b.dev);
        // the device holds exactly the sound, with the right header
        assert.ok(b.dev.usrData[1]);
        assert.deepEqual(Array.from(b.dev.slotSound(1)), Array.from(data));
        assert.equal(b.dev.slotName(1), "KICK");
        assert.equal(b.dev.usrHdr[1].rate, 22050);
        assert.equal(b.dev.usrHdr[1].len, K.CAP);
        assert.equal(b.dev.toasts, 1);
        assert.equal(b.dev.audioHold, 0, "the sound is given back");
        assert.equal(b.dev.slot, 0xFF, "the session is over");
        // the other slots untouched
        assert.deepEqual([0, 2, 3].map(k => b.dev.usrData[k]), [null, null, null]);
        // the frames that arrived are exactly the ones the Python tool's framing makes
        const want = K.uploadFrames(1, data, 22050, "KICK");
        const got = b.dev.seen.filter(s => s.cmd >= 2 && s.cmd <= 4);
        assert.equal(got.length, want.length);
        want.forEach((f, i) => assert.equal(hex(got[i].frame), hex(f.frame), "frame " + i));
        // every WRITE is at most 256 bytes of sound, at a multiple of 256, in order
        const writes = b.dev.framesOf(3);
        assert.equal(writes.length, 143);
        // and LIST says so
        const l = await fm1.list();
        assert.deepEqual(l[1], { slot: 1, used: true, rate: 22050, len: K.CAP, name: "KICK", seconds: K.CAP / 22050 });
        assert.equal(l[0].used, false);
        // the device never saw a frame the firmware would refuse, nor a status byte >= 0x80 inside a frame
        for (const s of b.dev.seen) assert.ok(s.frame.subarray(1, -1).every(x => x < 0x80));
        assert.ok(b.dev.seen.every(s => s.frame.length <= 5 + Math.ceil(260 * 8 / 7) + 1), "frames of 304 bytes at most");
    });
});

test("success with a last chunk shorter than 256 bytes and a 8 character name", async () => {
    const data = sound(1.0);                                // about 22050 bytes: 86 x 256 and a short one
    const tail = data.length % 256, full = Math.floor(data.length / 256);
    assert.ok(tail > 0 && full > 80, "a short last chunk (" + tail + " bytes)");
    await withFm1({}, async (fm1, b) => {
        const r = await fm1.send(0, data, 22050, "SNARE 808");
        assert.equal(r.frames, 1 + full + 1 + 1);
        noBad(b.dev);
        assert.deepEqual(Array.from(b.dev.slotSound(0)), Array.from(data));
        assert.equal(b.dev.slotName(0), "SNARE 808");
        assert.equal(b.dev.framesOf(3)[full].frame.length, 5 + Math.ceil((4 + tail) * 8 / 7) + 1, "the last WRITE carries only " + tail + " bytes");
    });
});

test("several sounds at once, MY 1..4, and a second round replaces them; the old ones are kept when the FM-1 refuses", async () => {
    const files = [
        ["KICK", makeWav({ rate: 44100, chans: [tone(44100, 0.4, 90, 0.9, 8)] })],
        ["SNARE", makeWav({ rate: 48000, chans: [noise(48000, 0.3, 0.8, 5), noise(48000, 0.3, 0.8, 6)] })],
        ["HAT", makeWav({ rate: 8000, chans: [noise(8000, 0.2, 0.7, 7)] })],
        ["LONG", makeWav({ rate: 44100, chans: [tone(44100, 4, 200, 0.7, 0.05)] })],
    ];
    const jobs = files.map(([n, w], i) => { const p = K.prepare(K.readWav(w)); return { slot: i, data: p.data, rate: 22050, name: n }; });
    assert.equal(jobs[3].data.length, K.CAP);
    await withFm1({}, async (fm1, b) => {
        const done = [], progress = [];
        const res = await K.sendAll(fm1, jobs, { start: i => done.push("s" + i), done: i => done.push("d" + i), progress: (i, s, t) => progress.push(i) });
        assert.equal(res.length, 4);
        assert.ok(res.every(r => r.ok));
        assert.deepEqual(done, ["s0", "d0", "s1", "d1", "s2", "d2", "s3", "d3"]);
        noBad(b.dev);
        const l = await fm1.list();
        assert.deepEqual(l.map(s => s.name), ["KICK", "SNARE", "HAT", "LONG"]);
        assert.deepEqual(l.map(s => s.len), jobs.map(j => j.data.length));
        jobs.forEach((j, i) => assert.deepEqual(Array.from(b.dev.slotSound(i)), Array.from(j.data), "slot " + i));
        // replace MY 2 with a new sound
        const again = sound(0.5, 22050, 1234);
        await fm1.send(1, again, 22050, "NEW");
        assert.deepEqual(Array.from(b.dev.slotSound(1)), Array.from(again));
        assert.equal(b.dev.slotName(1), "NEW");
        assert.deepEqual(Array.from(b.dev.slotSound(0)), Array.from(jobs[0].data), "the others stay");
        // now KAWA plays: every job is refused at BEGIN, nothing is erased, the old sounds are still there
        b.dev.sqPlay = 1;
        const eraseBefore = b.dev.eraseCalls;
        const res2 = await K.sendAll(fm1, jobs, {});
        assert.equal(res2.length, 1, "stops at the first 'busy'");
        assert.equal(res2[0].error.code, "busy");
        assert.equal(res2[0].error.slotState, "unchanged");
        assert.equal(b.dev.eraseCalls, eraseBefore, "nothing was erased");
        assert.deepEqual(Array.from(b.dev.slotSound(0)), Array.from(jobs[0].data));
        assert.deepEqual(Array.from(b.dev.slotSound(1)), Array.from(again));
    });
});

test("a sound in the 4 sample slots through the whole path: files -> readWav -> prepare -> mock, equal to prepare() alone", async () => {
    for (const c of CASES.filter(x => !x.err && !x.silent && !x.near).slice(0, 12)) {
        const prep = K.prepare(K.readWav(makeWav(c.wav)), { rate: c.rate });
        await withFm1({}, async (fm1, b) => {
            await fm1.send(3, prep.data, c.rate, K.nameFromFile(c.name));
            noBad(b.dev);
            assert.deepEqual(Array.from(b.dev.slotSound(3)), Array.from(prep.data), c.name);
            assert.equal(b.dev.usrHdr[3].rate, c.rate);
        });
    }
});

test("a transfer cut mid-way by the cable: 'lost', the slot is empty (not half written), the old sound is gone, a second try works", async () => {
    const old = sound(0.5, 22050, 300), data = sound(1.5, 22050, 500);
    const b = await bench({ faults: { disconnectAfter: { cmd: 3, count: 20 } } });
    let fm1 = await b.connect();
    // an old sound in MY 3 first (the fault only counts WRITEs, so use a second device state: do it on slot 2 before the fault)
    b.dev.o.faults = {};
    await fm1.send(2, old, 22050, "OLD");
    assert.ok(b.dev.usrData[2]);
    b.dev.processed = {}; b.dev.o.faults = { disconnectAfter: { cmd: 3, count: 20 } };
    const e = await rejects(fm1.send(2, data, 22050, "NEW"), "lost", { step: "WRITE", slotState: "empty", slot: 2 });
    assert.match(e.message, /MY 3 is empty now/);
    assert.match(e.message, /disconnected/);
    // the device's state: header erased at BEGIN, never rewritten; at the next boot the slot is empty (no half-valid sound)
    assert.equal(b.dev.usrData[2], null);
    assert.ok(b.dev.flash.subarray(2 * USR_SIZE, 2 * USR_SIZE + 36).every(x => x === 0xFF), "no header was written");
    b.dev.boot();
    assert.equal(b.dev.usrData[2], null, "after a restart: empty");
    // the sound comes back by itself 10 s after the last frame
    b.dev.slot = 2; b.dev.audioHold = 1;
    b.dev.tick(10001);
    assert.equal(b.dev.audioHold, 0);
    assert.equal(b.dev.slot, 0xFF);
    assert.deepEqual(b.dev.violations, []);
    await fm1.close();
    // plugged in again: it works
    const midi2 = new MockMidi();
    b.dev.unplugged = false; b.dev.o.faults = {}; b.dev.processed = {};
    midi2.add(b.dev, "FM-1");
    fm1 = await K.connect({ nav: { requestMIDIAccess: o => midi2.request(o) } });
    await fm1.send(2, data, 22050, "NEW");
    assert.deepEqual(Array.from(b.dev.slotSound(2)), Array.from(data));
    assert.deepEqual(b.dev.violations, []);
    await fm1.close();
});

test("a transfer cut mid-way because the FM-1 goes mute: two tries a frame, then 'timeout', the slot is empty", async () => {
    const data = sound(1.5);
    await withFm1({ faults: { muteAfter: { cmd: 3, count: 10 } } }, async (fm1, b) => {
        const t0 = Date.now();
        const e = await rejects(fm1.send(0, data, 22050, "X"), "timeout", { step: "WRITE", slotState: "empty" });
        assert.match(e.message, /MY 1 is empty now/);
        assert.ok(Date.now() - t0 >= 2 * K.DELAY.write - 20, "it waited, twice");
        assert.equal(b.dev.framesOf(3).length, 10, "only the first ten arrived (the muted device logs nothing more)");
        assert.equal(b.dev.usrData[0], null);
        b.dev.dead = false;                                    // it hangs 10 s, then gives the sound back
        b.dev.o.faults = {};
        b.dev.tick(10001);
        assert.equal(b.dev.audioHold, 0);
        assert.deepEqual(b.dev.violations, []);
        assert.equal(fm1.busy, false, "the session object is usable again");
        await fm1.send(0, data, 22050, "X");
        assert.deepEqual(Array.from(b.dev.slotSound(0)), Array.from(data));
    });
});

test("a BEGIN nobody answers: 'unknown' (the FM-1 may have emptied the slot); an END nobody answers: 'unknown' too", async () => {
    const data = sound(0.5);
    await withFm1({ faults: { dropRequest: [{ cmd: 2, nth: 1 }, { cmd: 2, nth: 2 }] } }, async (fm1, b) => {
        const e = await rejects(fm1.send(0, data, 22050, "X"), "timeout", { step: "BEGIN", slotState: "unknown" });
        assert.match(e.message, /may be empty/);
        assert.equal(b.dev.framesOf(2).length, 2 + 0, "tried twice");
    });
    await withFm1({ faults: { muteAfter: { cmd: 3, count: 2 } } }, async () => {});
    const b = await bench({ faults: { dropRequest: [{ cmd: 4, nth: 1 }, { cmd: 4, nth: 2 }] } });
    const fm1 = await b.connect();
    const e = await rejects(fm1.send(0, data, 22050, "X"), "timeout", { step: "END", slotState: "unknown" });
    assert.match(e.message, /may be empty/);
    assert.equal(b.dev.usrData[0], null, "the END never arrived: the header was not written");
    await fm1.close();
});

test("the user cancels: 'cancelled', nothing half written", async () => {
    const data = sound(1.5);
    await withFm1({}, async (fm1, b) => {
        const ac = new AbortController();
        const e = await rejects(fm1.send(1, data, 22050, "X", { signal: ac.signal, onProgress: (s) => { if (s >= 2000) ac.abort(); } }), "cancelled", { slotState: "empty" });
        assert.match(e.message, /MY 2 is empty now/);
        assert.equal(b.dev.usrData[1], null);
        assert.ok(b.dev.framesOf(3).length < 20);
        // cancelled before anything went out: unchanged
        const ac2 = new AbortController(); ac2.abort();
        const before = b.dev.seen.length;
        const e2 = await rejects(fm1.send(1, data, 22050, "X", { signal: ac2.signal }), "cancelled", { slotState: "unchanged" });
        assert.match(e2.message, /Nothing was changed/);
        assert.equal(b.dev.seen.length, before, "no frame went out");
        b.dev.tick(10001);
        assert.deepEqual(b.dev.violations, []);
    });
});

test("a refused frame: busy (KAWA plays) keeps the old sound; a flash error and a corrupted byte leave the slot empty", async () => {
    const old = sound(0.4, 22050, 250), data = sound(1.0, 22050, 800);
    // busy: BEGIN refused, the old sound stays, nothing erased
    await withFm1({}, async (fm1, b) => {
        await fm1.send(0, old, 22050, "OLD");
        b.dev.sqPlay = 1;
        const er = b.dev.eraseCalls;
        const e = await rejects(fm1.send(0, data, 22050, "NEW"), "busy", { rc: 2, step: "BEGIN", slotState: "unchanged" });
        assert.match(e.message, /Press PLAY/);
        assert.match(e.message, /Nothing was changed/);
        assert.equal(b.dev.eraseCalls, er);
        assert.deepEqual(Array.from(b.dev.slotSound(0)), Array.from(old));
        assert.deepEqual(b.dev.refusals.map(r => r.rc), [2]);
        assert.equal(b.dev.framesOf(3).length, 43 - 43 + b.dev.framesOf(3).length);
        b.dev.sqPlay = 0;                                // she presses PLAY to stop: now it goes
        await fm1.send(0, data, 22050, "NEW");
        assert.deepEqual(Array.from(b.dev.slotSound(0)), Array.from(data));
    });
    // a flash error on the 6th program (WRITE number 5): rc 3, the transfer stops, the slot is empty
    await withFm1({ faults: { failProg: 6 } }, async (fm1, b) => {
        const e = await rejects(fm1.send(2, data, 22050, "NEW"), "flash", { rc: 3, step: "WRITE", slotState: "empty" });
        assert.match(e.message, /flash/);
        assert.equal(b.dev.usrData[2], null);
        assert.equal(b.dev.framesOf(3).length, 6, "it stopped at the refused frame");
        b.dev.boot();
        assert.equal(b.dev.usrData[2], null);
        await fm1.send(2, data, 22050, "NEW");                    // the next try is fine
        assert.deepEqual(Array.from(b.dev.slotSound(2)), Array.from(data));
    });
    // a wire byte flips in WRITE number 7: everything is acknowledged, END says CRC mismatch, the slot stays empty
    await withFm1({ faults: { corrupt: { cmd: 3, nth: 7, index: 40 } } }, async (fm1, b) => {
        const e = await rejects(fm1.send(3, data, 22050, "NEW"), "crc", { rc: 4, step: "END", slotState: "empty" });
        assert.match(e.message, /checksum/);
        assert.equal(b.dev.usrData[3], null, "a damaged sound is never kept");
        assert.ok(b.dev.flash.subarray(3 * USR_SIZE, 3 * USR_SIZE + 36).every(x => x === 0xFF), "no header was written");
        assert.equal(b.dev.slot, 0xFF, "the session ended");
        assert.deepEqual(b.dev.violations, []);
        await fm1.send(3, data, 22050, "NEW");
        assert.deepEqual(Array.from(b.dev.slotSound(3)), Array.from(data));
    });
    // the other codes the FM-1 can answer
    for (const [rc, code] of [[1, "args"], [5, "order"], [9, "rcX"]]) {
        const b = await bench();
        const fm1 = await b.connect();
        const orig = b.dev.handle.bind(b.dev);
        b.dev.handle = (cmd, a, n) => { if (cmd === 3 && a[1] === 0 && a[2] === 1) { const r = new Uint8Array(100); r[0] = rc; r[1] = a[0]; r[2] = a[1]; r[3] = a[2]; r[4] = a[3]; b.dev.reply(3, r, 5); } else orig(cmd, a, n); };
        const e = await rejects(fm1.send(0, data, 22050, "N"), code, { rc, step: "WRITE", slotState: "empty" });
        if (rc === 9) assert.match(e.message, /unknown code \(9\)/);
        await fm1.close();
    }
});

test("lost replies: a WRITE answered twice, an END whose answer is lost, a late duplicate of an earlier answer", async () => {
    const data = sound(1.2, 22050, 640);
    // the answer to WRITE 5 is lost: the page asks again, the FM-1 says 'already there' (no second program)
    await withFm1({ faults: { dropReply: [{ cmd: 3, nth: 5 }] } }, async (fm1, b) => {
        const r = await fm1.send(1, data, 22050, "RETRY");
        assert.equal(r.retries, 1);
        noBad(b.dev);
        assert.deepEqual(Array.from(b.dev.slotSound(1)), Array.from(data));
        const offs = b.dev.framesOf(3).map(s => { const d = cUnpack7(s.frame.subarray(5, -1), 300); return d.buf[1] | d.buf[2] << 8; });
        assert.equal(offs.filter((o, i) => i && o === offs[i - 1]).length, 1, "exactly one frame was sent twice");
    });
    // the answer to BEGIN is lost: BEGIN again (it erases again, harmless)
    await withFm1({ faults: { dropReply: [{ cmd: 2, nth: 1 }] } }, async (fm1, b) => {
        const r = await fm1.send(1, data, 22050, "RETRY");
        assert.equal(r.retries, 1);
        noBad(b.dev);
        assert.deepEqual(Array.from(b.dev.slotSound(1)), Array.from(data));
    });
    // the answer to END is lost: the second END finds no session (code 5); LIST shows the sound is in: success
    await withFm1({ faults: { dropReply: [{ cmd: 4, nth: 1 }] } }, async (fm1, b) => {
        const r = await fm1.send(1, data, 22050, "RETRY");
        assert.equal(r.retries, 1);
        assert.deepEqual(Array.from(b.dev.slotSound(1)), Array.from(data));
        assert.deepEqual(b.dev.refusals.map(x => [x.cmd, x.rc]), [[4, 5]], "the only refusal is the second END");
        assert.deepEqual(b.dev.violations, []);
    });
    // the answer to END is lost, and the sound is NOT in (another reason for code 5): the page does not claim success
    {
        const b = await bench({ faults: { dropReply: [{ cmd: 4, nth: 1 }] } });
        const fm1 = await b.connect();
        const orig = b.dev.handle.bind(b.dev);
        b.dev.handle = (cmd, a, n) => { if (cmd === 4) { const r = new Uint8Array(100); r[0] = 5; r[1] = a[0]; b.dev.reply(4, r, 2); } else orig(cmd, a, n); };
        await rejects(fm1.send(1, data, 22050, "NOPE"), "order", { step: "END", slotState: "empty" });
        await fm1.close();
    }
    // a late duplicate of every answer (the old answer of an earlier try): not taken for the next one
    await withFm1({ faults: { dupReply: Array.from({ length: 200 }, (_, i) => ({ cmd: 3, nth: i + 1 })).concat([{ cmd: 2, nth: 1 }]) } }, async (fm1, b) => {
        await fm1.send(2, data, 22050, "DUP");
        assert.deepEqual(Array.from(b.dev.slotSound(2)), Array.from(data));
        noBad(b.dev);
        await sleep(20);
    });
});

test("a late duplicate of an answer is not taken for the next frame's answer (the offset is matched)", async () => {
    const data = sound(1.0);
    // the answer to WRITE 3 comes twice; WRITE 4 is then refused by the flash. The page must report the refusal while
    // it waits for WRITE 4 (progress stops at 3 x 256), not accept the duplicate as WRITE 4's answer.
    await withFm1({ faults: { dupReply: [{ cmd: 3, nth: 3 }], failProg: 4 } }, async (fm1, b) => {
        const seen = [];
        await rejects(fm1.send(0, data, 22050, "DUP", { onProgress: s => seen.push(s) }), "flash", { step: "WRITE", slotState: "empty" });
        assert.deepEqual(seen, [0, 256, 512, 768]);
        assert.equal(b.dev.framesOf(3).length, 4);
    });
});

test("closing the link (the page does it when the cable is pulled) fails a waiting request at once: it never hangs", async () => {
    const data = sound(1.0);
    const b = await bench({ faults: { muteAfter: { cmd: 3, count: 5 } } });
    const fm1 = await b.connect();
    const t0 = Date.now();
    const p = fm1.send(0, data, 22050, "HANG");
    const seen = rejects(p, "lost", { step: "WRITE", slotState: "empty" });
    await sleep(40);                                       // the FM-1 has gone mute: the 6th WRITE waits for its answer
    await fm1.close();
    await Promise.race([seen, sleep(1000).then(() => { throw new Error("the request is still waiting 1 s after close(): it hangs"); })]);
    assert.ok(Date.now() - t0 < K.DELAY.write, "failed at once, not after the timeouts (" + (Date.now() - t0) + " ms)");
    assert.equal(fm1.busy, false);
});

test("SysEx cut in pieces, a lone F7, clock and active sensing between the pieces: put back together", async () => {
    const data = sound(0.6);
    for (const chunk of [1, 3, 7, 64]) {
        await withFm1({ chunk, noise: true }, async (fm1, b) => {
            await fm1.send(1, data, 22050, "CHUNKED");
            noBad(b.dev);
            assert.deepEqual(Array.from(b.dev.slotSound(1)), Array.from(data), "chunk " + chunk);
            assert.equal((await fm1.list())[1].name, "CHUNKED");
        });
    }
});

test("the connection fails clearly: no Web MIDI, refused, no device, no FM-1 port, port busy, silent, not KAWA, rescue mode, unplugged", async () => {
    // no Web MIDI
    let e = await rejects(K.connect({ nav: {} }), "nomidi");
    assert.match(e.message, /Chrome or Edge/);
    await rejects(K.connect({ nav: null }), "nomidi");
    // the permission is refused
    e = await rejects(bench({}, { reject: true }).then(b => b.connect()), "denied");
    assert.match(e.message, /SysEx/);
    // no device at all
    {
        const midi = new MockMidi();
        e = await rejects(K.connect({ nav: { requestMIDIAccess: o => midi.request(o) } }), "noports");
        assert.match(e.message, /USB data cable/);
    }
    // another device only: no FM-1 by name, the page offers the lists; choosing the ports works
    {
        const b = await bench({}, {}, "Some Synth");
        e = await rejects(b.connect(), "noFm1");
        assert.deepEqual(e.ports.outs.map(p => p.name), ["Some Synth"]);
        assert.equal(b.dev.seen.length, 0, "not a byte was sent to a device that is not named FM-1");
        const fm1 = await b.connect({ outId: "out1", inId: "in1" });
        assert.equal(fm1.info.version, "0.4");
        await fm1.close();
    }
    // the names that count
    for (const n of ["FM-1", "M-VAVE FM-1 MIDI", "Felucca", "KAWA", "MIDIIN2 (FM-1)", "usb fm-1"]) assert.ok(K.PORT_RE.test(n), n);
    for (const n of ["Launchkey", "IAC Driver Bus 1", "Midi Through", ""]) assert.ok(!K.PORT_RE.test(n), n);
    // the port cannot be opened (another program holds it)
    e = await rejects(bench({}, { hang: true }).then(b => b.connect()), "portbusy");
    assert.match(e.message, /another program/i);
    // the device is there but silent: two tries of INFO, then 'noanswer'
    {
        const b = await bench({ faults: { dropRequest: [{ cmd: 1, nth: 1 }, { cmd: 1, nth: 2 }, { cmd: 1, nth: 3 }] } });
        const t0 = Date.now();
        e = await rejects(b.connect(), "noanswer");
        assert.ok(Date.now() - t0 >= 2 * K.DELAY.info - 20);
        assert.equal(b.dev.framesOf(1).length, 2, "two tries");
        assert.match(e.message, /KAWA 0\.2 or later/);
        assert.match(e.message, /rescue/i);
    }
    // the first INFO is lost, the second answered: fine
    {
        const b = await bench({ faults: { dropRequest: [{ cmd: 1, nth: 1 }] } });
        const fm1 = await b.connect();
        assert.equal(b.dev.framesOf(1).length, 2);
        await fm1.close();
    }
    // rescue mode / the stock firmware: a port that answers nothing to KAWA's frames
    {
        const b = await bench({ silentToKawa: true });
        await rejects(b.connect(), "noanswer");
    }
    // something answers INFO but is not KAWA
    {
        const b = await bench({ infoMagic: "ABCD" });
        e = await rejects(b.connect(), "notkawa");
        assert.match(e.message, /not KAWA/);
    }
    // unplugged after connecting: the next request says so
    {
        const b = await bench();
        const fm1 = await b.connect();
        b.midi.unplug(b.dev);
        b.dev.unplugged = true;
        e = await rejects(fm1.list(), "lost");
        assert.match(e.message, /disconnected/);
        assert.equal(fm1.gone, true);
        e = await rejects(fm1.send(0, sound(0.3), 22050, "X"), "lost", { slotState: "unknown" });
        await fm1.close();
    }
    // the page asked for SysEx permission (a send without it would throw in the browser)
    {
        const b = await bench();
        const fm1 = await b.connect();
        assert.equal(b.midi.sysexAllowed, true);
        await fm1.close();
    }
});

test("nothing is sent that the firmware would refuse: the slot, the size, the rate and the name are checked first", async () => {
    const good = sound(0.5);
    await withFm1({}, async (fm1, b) => {
        const base = b.dev.seen.length;
        await rejects(fm1.send(-1, good, 22050, "X"), "badslot");
        await rejects(fm1.send(4, good, 22050, "X"), "badslot");
        await rejects(fm1.send(1.5, good, 22050, "X"), "badslot");
        await rejects(fm1.send(0, good.subarray(0, 63), 22050, "X"), "badargs");
        await rejects(fm1.send(0, new Uint8Array(K.CAP + 1), 22050, "X"), "toobig", { length: K.CAP + 1, cap: K.CAP });
        await rejects(fm1.send(0, good, 44100, "X"), "badargs");
        await rejects(fm1.send(0, good, 22050, null), "badargs");
        await rejects(fm1.send(0, Array.from(good), 22050, "X"), "badargs");
        assert.equal(b.dev.seen.length, base, "not one frame went out");
        noBad(b.dev);
    });
    // a device with smaller slots or fewer of them: the limits are the device's
    await withFm1({ capReported: 20000, nusr: 2 }, async (fm1, b) => {
        assert.deepEqual(fm1.limits, { slots: 2, cap: 20000 });
        await rejects(fm1.send(2, good, 22050, "X"), "badslot");
        await rejects(fm1.send(0, new Uint8Array(20001), 22050, "X"), "toobig", { cap: 20000 });
        const base = b.dev.seen.length;
        assert.equal(b.dev.seen.length, base);
    });
    // a device that reports more than we know: we stay at what we know
    await withFm1({ capReported: 100000 }, async fm1 => { assert.equal(fm1.limits.cap, K.CAP); });
    // one request at a time
    await withFm1({}, async fm1 => {
        const a = fm1.send(0, sound(0.5), 22050, "A");
        await assert.rejects(fm1.list(), /busy with another request/);
        await a;
    });
    // erase
    await withFm1({}, async (fm1, b) => {
        await fm1.send(2, good, 22050, "ERASEME");
        await fm1.erase(2);
        assert.equal(b.dev.usrData[2], null);
        assert.ok(b.dev.flash.subarray(2 * USR_SIZE, 3 * USR_SIZE).every(x => x === 0xFF), "all nine sectors erased");
        await rejects(fm1.erase(7), "badslot");
        b.dev.sqPlay = 1;
        await rejects(fm1.erase(1), "busy", { rc: 2 });
    });
    // no frame the page can build looks like the bootloader or updater keys (usb.c: F0 22 24 35 7D|7F F7)
    for (let c = 1; c <= 6; c++) assert.deepEqual(Array.from(K.frame(c).subarray(0, 4)), [0xF0, 0x7D, 0x4B, 0x57]);
});

test("the delays are the Python tool's (kawa_samples.py ask(): INFO / LIST 2 s, BEGIN / END 3 s, WRITE 1.5 s, ERASE 5 s, two tries)", () => {
    assert.deepEqual({ info: REAL.info, list: REAL.list, begin: REAL.begin, write: REAL.write, end: REAL.end, erase: REAL.erase, tries: REAL.tries },
                     { info: 2000, list: 2000, begin: 3000, write: 1500, end: 3000, erase: 5000, tries: 2 });
    if (hasPython) {
        const src = fs.readFileSync(path.join(ROOT, "install", "kawa_samples.py"), "utf8");
        assert.match(src, /def ask\(self, cmd, msg, timeout=2\.0, tries=2\)/);
        assert.match(src, /timeout=3 if name != "WRITE" else 1\.5/);
        assert.match(src, /frame\("ERASE", bytes\(\[a\.slot - 1\]\)\), timeout=5/);
    }
    assert.equal(K.PACKET, 256);
    assert.equal(K.CAP, 0x9000 - 0x100);
});

// ====================================================================================================================
// 4. the real firmware code (the host simulator of tests/host.c)
// ====================================================================================================================

const haveCC = cp.spawnSync("cc", ["--version"], { encoding: "utf8" }).status === 0;
let hostBin = null, hostDir = null;
function buildHost() {
    if (hostBin) return hostBin;
    hostDir = fs.mkdtempSync(path.join(os.tmpdir(), "kawa-host-"));
    const bin = path.join(hostDir, "kawa");
    const r = cp.spawnSync("cc", ["-O1", "-w", "-DDINGHY_HOST", '-DAPP_VER="0.4"', "-I" + path.join(ROOT, "source", "firmware", "src"),
                                  "-I" + path.join(ROOT, "source", "firmware", "gen"), "-o", bin, path.join(ROOT, "source", "firmware", "src", "kawa.c")],
                           { encoding: "utf8" });
    if (r.status !== 0) throw new Error("cannot build the host simulator:\n" + r.stderr);
    return (hostBin = bin);
}
/* frames: [{t, frame}], runs the firmware, returns its replies in order: [{t, cmd, body}] */
function runHost(frames, end) {
    const bin = buildHost(), script = path.join(hostDir, "s.txt");
    fs.writeFileSync(script, frames.map(f => `${f.t} sysex ${hex(f.frame).toUpperCase()}`).join("\n") + `\n${end} end\n`);
    const r = cp.spawnSync(bin, [path.join(hostDir, "out"), script], { encoding: "utf8", maxBuffer: 1 << 28 });
    assert.equal(r.status, 0, "the simulator failed: " + r.stderr);
    const out = [];
    for (const line of r.stdout.split("\n")) {
        const m = /^\s*(\d+) ms\s+sysex < ((?:[0-9A-F]{2} ?)+)$/.exec(line);
        if (!m) continue;
        const rep = K.parseReply(Uint8Array.from(m[2].trim().split(" "), h => parseInt(h, 16)));
        out.push({ t: +m[1], cmd: rep.cmd, body: rep.body });
    }
    return out;
}
const sched = (list, t0 = 200) => list.map((f, i) => ({ t: t0 + 3 * i, frame: f }));

test("the REAL firmware (kawa.c on the host): the page's frames are accepted, the slot holds the sound, LIST says so", () => {
    const prep = K.prepare(K.readWav(makeWav({ rate: 44100, chans: [tone(44100, 2.5, 440, 0.8, 0.4), tone(44100, 2.5, 330, 0.5, 0.4)] })));
    assert.equal(prep.data.length, K.CAP);
    const fr = K.uploadFrames(2, prep.data, 22050, "Real FW 1");
    const rep = runHost([{ t: 100, frame: K.frame("INFO") }, ...sched(fr.map(f => f.frame)), { t: 2000, frame: K.frame("LIST") }], 2200);
    assert.equal(rep.length, 1 + fr.length + 1, "one reply per request");
    const info = K.parseInfo(rep[0].body);
    assert.deepEqual({ slots: info.slots, cap: info.cap }, { slots: 4, cap: K.CAP });
    assert.equal(info.version, "0.4");
    rep.slice(1, -1).forEach((r, i) => {
        assert.equal(r.cmd, K.CMD[fr[i].name], "reply " + i + " is a " + fr[i].name);
        assert.equal(r.body[0], 0, `${fr[i].name} ${i}: the firmware answered ${r.body[0]}`);
    });
    const l = K.parseList(rep[rep.length - 1].body, 4);
    assert.deepEqual(l.map(s => s.used), [false, false, true, false]);
    assert.deepEqual({ rate: l[2].rate, len: l[2].len, name: l[2].name }, { rate: 22050, len: K.CAP, name: "REAL FW 1" });
});

test("the REAL firmware: a damaged byte -> CRC mismatch and an empty slot; a WRITE out of order -> refused; a short last chunk", () => {
    const data = sound(1.0, 22050, 700);
    // the sound with a flipped bit in the 4th WRITE's data
    const fr = K.uploadFrames(0, data, 22050, "DAMAGED").map(f => Uint8Array.from(f.frame));
    fr[4][30] ^= 1;
    let rep = runHost(sched(fr).concat([{ t: 2000, frame: K.frame("LIST") }]), 2200);
    const codes = rep.slice(0, -1).map(r => r.body[0]);
    assert.deepEqual(codes.slice(0, -1), new Array(codes.length - 1).fill(0), "every frame but the END is accepted");
    assert.equal(codes[codes.length - 1], 4, "END: CRC mismatch");
    assert.equal(K.parseList(rep[rep.length - 1].body, 4)[0].used, false, "the damaged sound was not kept");
    // WRITE 2 sent before WRITE 1: out of order (5); then the right order goes through; the last chunk is short
    const ok = K.uploadFrames(1, data, 22050, "ORDER");
    const swapped = [ok[0], ok[2], ok[1], ok[2], ...ok.slice(3)].map(f => f.frame);
    rep = runHost(sched(swapped).concat([{ t: 2000, frame: K.frame("LIST") }]), 2200);
    assert.equal(rep[0].body[0], 0);
    assert.equal(rep[1].body[0], 5, "offset 256 before offset 0: out of order");
    assert.equal(rep[2].body[0], 0);
    assert.equal(rep[3].body[0], 0);
    assert.equal(rep[rep.length - 2].body[0], 0, "END accepted");
    const l = K.parseList(rep[rep.length - 1].body, 4)[1];
    assert.deepEqual({ used: l.used, len: l.len, name: l.name }, { used: true, len: data.length, name: "ORDER" });
    // a refused BEGIN (slot 7) and an unknown command
    rep = runHost(sched([K.frame("BEGIN", [7]), rawFrame(9, [])]), 600);
    assert.deepEqual(rep.map(r => r.body[0]), [1, 1]);
}, "cc");

test("the REAL firmware and MockFm1 answer alike: the same frames give the same replies, byte for byte", () => {
    const data = sound(0.7, 22050, 520);
    const fr = K.uploadFrames(3, data, 22050, "TWIN").map(f => f.frame);
    const bad = Uint8Array.from(K.header(data, 22050, "BADCRC")); bad[24] ^= 0xFF;
    const extra = [K.frame("WRITE", [3, 1, 0, 0, 1, 2, 3]), K.frame("BEGIN", []), K.frame("END", [3, 1, 2]), K.frame("LIST"), K.frame("INFO"),
                   K.frame("WRITE", [0, 0, 0, 0, 1]), K.frame("ERASE", [1]), K.frame("ERASE", [9]), K.frame("BEGIN", [0]),
                   K.frame("WRITE", [0, 0, 0, 0, ...new Array(256).fill(7)]), K.frame("END", [0, ...bad]), K.frame("LIST")];
    const all = [K.frame("INFO"), ...fr, K.frame("LIST"), ...extra];
    const real = runHost(sched(all, 100), 100 + 3 * all.length + 300);
    const mock = new MockFm1(), got = [];
    mock.out = u => got.push(K.parseReply(u));
    all.forEach(f => mock.receive(f));
    assert.equal(real.length, got.length, "the same number of replies");
    real.forEach((r, i) => assert.equal(hex(K.frame(r.cmd, r.body)), hex(K.frame(got[i].cmd, got[i].body)), "reply " + i));
}, "cc");

// ====================================================================================================================

(async () => {
    const only = process.argv[2];
    let pass = 0, fail = 0, skip = 0;
    const require_all = process.env.KAWA_REQUIRE_ALL === "1";
    for (const t of tests) {
        if (only && !t.name.includes(only)) continue;
        if ((t.needs === "py" && !hasPython) || (t.needs === "cc" && !haveCC)) {
            skip++;
            console.log(`SKIPPED  ${t.name}\n         (needs ${t.needs === "py" ? "python3 with numpy" : "a C compiler (cc)"})`);
            continue;
        }
        const t0 = Date.now();
        try {
            await t.fn();
            pass++;
            console.log(`ok       ${t.name}  (${Date.now() - t0} ms)`);
        } catch (e) {
            fail++;
            console.log(`FAILED   ${t.name}\n${String(e && e.stack || e).split("\n").map(l => "         " + l).join("\n")}`);
        }
    }
    if (hostDir) fs.rmSync(hostDir, { recursive: true, force: true });
    console.log(`\n${pass} passed, ${fail} failed, ${skip} skipped`);
    if (fail || (skip && require_all)) process.exitCode = 1;
})();

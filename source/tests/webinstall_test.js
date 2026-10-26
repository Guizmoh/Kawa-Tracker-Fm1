#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Omnipotard
/* Test of the web installer (install/webinstall.js, used by index.html) against a simulated FM-1 on a simulated
 * Web MIDI (tests/fm1_mock.js). No hardware, no browser, no package to install: plain Node (18+).
 *
 *   node source/tests/webinstall_test.js
 *
 * It runs the installer's own code, with the delays shortened, on the real package install/kawa-*.fwsc:
 *   - a good install (stock firmware -> loader -> KAWA), with noise on the MIDI line, SysEx cut in pieces, a stale port
 *     list, a device that re-enumerates under another name; the flash of the simulated chip must end up equal to the
 *     package's app area, the flash head and the user's data untouched;
 *   - a damaged package (every kind: header, file list, a file's CRC, cut short, too short, not an FM-1 package, no
 *     loader): refused, and no MIDI port is even opened;
 *   - the wrong model (device or package), another firmware's update mode: refused before anything is written;
 *   - a device found in update mode: only the write is finished; a cable cut mid-write, then the resume; a cut in
 *     step 1 (nothing written); a device that stops answering;
 *   - no MIDI port / silent port / busy port, a browser without Web MIDI, a refused permission.
 * With python3 on the PATH it also checks the port against install/fm1_install.py itself (same constants, same delays,
 * pack7 / unpack7 / response / request / identity, CRC16, pair_output, and the same verdict and message for every
 * variant of the package). Exit code 0 when everything passed. The simulator is not the chip: see fm1_mock.js. */
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const cp = require("node:child_process");

const ROOT = path.join(__dirname, "..", "..");
const W = require(path.join(ROOT, "install", "webinstall.js"));
const M = require("./fm1_mock.js");

// the delays of the Python tool, shortened (what is compared with the Python values is the table in `REAL`)
const REAL = Object.assign({}, W.DELAY);
Object.assign(W.DELAY, { open: 5, start: 20, reply: 0, loader: 20, reboot: 20, retry: 20, poll: 30, hs: 100,
                         idle_check: 400, idle_write: 600, wait_loader: 2000, wait_reboot: 2000, portopen: 500, refresh: 50 });

const sums = fs.readFileSync(path.join(ROOT, "install", "SHA256SUMS"), "utf8");
const latest = W.newestInSums(sums);                          // the package the page installs
const PKGNAME = latest.name;
const PKG = new Uint8Array(fs.readFileSync(path.join(ROOT, "install", PKGNAME)));
const PRODUCT = M.productOf(PKG);                             // the simulator's own reading of the markers
const rawIndex = L => (L < 940 ? Math.floor(L / 0x2F) * 0x30 + (L % 0x2F) : L + 20);   // logical image offset -> package offset

const tests = [];
const test = (name, fn) => tests.push([name, fn]);

function mutate(raw, f) { const r = Uint8Array.from(raw); f(r); return r; }
function withProduct(raw, text) {                              // the same package for another identity (the markers only)
    return mutate(raw, r => { for (let i = 0; i < 20; i++) r[i * 0x30 + 0x2F] = i < text.length ? (text.charCodeAt(i) + i + 1) & 0xFF : 0x7D; });
}

/* one run of the installer's flow against a simulated bench:
 *   dev: options of the simulated FM-1 (null: none), extra: other devices, midi: options of the Web MIDI, nav: replaces it */
async function bench(o = {}) {
    const midi = new M.MockMidi(o.midi || {});
    const dev = o.dev === null ? null : midi.add(new M.Fm1(o.dev || {}));
    for (const d of o.extra || []) midi.add(d);
    const r = await bench.run(midi, o);
    return Object.assign(r, { midi, dev });
}
bench.run = function (midi, o) {                               // (also used again on the same bench, after a replug)
    const r = { steps: [], lines: [], busy: [], asked: [] };
    return Promise.resolve().then(() => W.runInstall({
        nav: "nav" in o ? o.nav : midi, bytes: o.bytes || PKG, name: o.name || PKGNAME, port: o.port,
        say: l => r.lines.push(l), step: (k, a) => r.steps.push([k, a]), busy: b => r.busy.push(b),
        confirm: "confirm" in o ? o.confirm : async info => { r.asked.push(info); return true; },
    })).then(res => { r.res = res; return r; }, err => { r.err = err; return r; });
};
const kinds = r => r.steps.map(s => s[0]);
const quiet = (dev) => { assert.deepEqual(dev.protocolErrors, [], "the simulator saw a malformed SysEx"); };

// ---------------------------------------------------------------- the good install ---

async function goodInstall(name, o = {}) {
    const r = await bench(o);
    const dev = r.dev;
    assert.equal(r.err, undefined, r.err && r.err.message);
    assert.deepEqual(r.res, { done: true, id: PRODUCT });
    assert.equal(PRODUCT, "FM-1_956");
    // the Python tool's sequence: start, check..., loader, write 0..100, reboot, done
    const k = kinds(r), ks = [...new Set(k)];
    assert.deepEqual(ks, ["start", "check", "loader", "write", "reboot", "done"]);
    assert.deepEqual(r.steps[0], ["start", o.dev && o.dev.appId || "FM-1_015"]);
    assert.deepEqual(r.steps[r.steps.length - 1], ["done", PRODUCT]);
    const w = r.steps.filter(s => s[0] === "write").map(s => s[1]);
    assert.equal(w[0], 0); assert.equal(w[w.length - 1], 100);
    assert.ok(w.slice(0, -1).every(x => x <= 99), "write % stays under 100 until the end");
    assert.ok(w.every((x, i) => i === 0 || x >= w[i - 1]), "write % never goes back");
    assert.equal(W.STEP_TEXT.done(PRODUCT), "done: the FM-1 runs FM-1_956");
    // the chip: the app area is the package's, the flash head and the user's data were never written
    assert.ok(Buffer.from(dev.appArea()).equals(Buffer.from(dev.expectedApp(PKG))), "app area differs from the package's flash.bin");
    assert.ok(dev.appChanged() && dev.untouched());
    assert.equal(dev.stats.commits, 1); assert.equal(dev.stats.finishes, 1);
    assert.ok(dev.stats.stepReads > 10 && dev.stats.loaderReads > 2000, `reads ${dev.stats.stepReads}/${dev.stats.loaderReads}`);
    assert.deepEqual([dev.stats.step1Fails, dev.stats.loaderFails], [[], []]);
    assert.equal(dev.stats.stray, 0); assert.equal(dev.stats.junk, 0);
    assert.equal(dev.stats.upgrades, 2, "UPGRADE once per step");
    assert.equal(dev.appId, "FM-1_956"); assert.equal(dev.mode, "app");
    assert.deepEqual(dev.events.filter(e => e.startsWith("plug")), ["plug:app", "plug:loader", "plug:app"]);
    assert.deepEqual(r.busy, [true, false]);
    assert.deepEqual(r.midi.sysexAsked, r.midi.sysexAsked.map(() => true), "SysEx must be asked for");
    assert.ok(r.midi.sysexAsked.length >= 1);
    assert.equal(r.asked.length, 1); assert.equal(r.asked[0].product, PRODUCT); assert.equal(r.asked[0].resume, false);
    assert.match(r.lines[0], /^package: FM-1_956 {2}\(kawa-\d/);
    assert.equal(r.lines[1], `device:  ${o.dev && o.dev.appId || "FM-1_015"}  (FM-1)`);
    assert.equal(dev.inPort.connection, "closed"); assert.equal(dev.outPort.connection, "closed");   // no port left open
    quiet(dev);
    return r;
}
test("good install: stock firmware -> loader -> KAWA (flash == package)", () => goodInstall());
test("good install: noise on the line (clock, sensing, notes) and SysEx cut in 37-byte pieces", () => goodInstall("", { midi: { noise: true, chunk: 37 } }));
test("good install: a browser whose port list goes stale (re-asks for MIDI access)", async () => {
    const r = await goodInstall("", { midi: { stale: true } });
    assert.ok(r.midi.requests >= 3, "asked the browser again while waiting for the re-enumeration");
});
test("good install: from an older KAWA (FM-1_953) with a port filter 'fm-1'", () => goodInstall("", { dev: { appId: "FM-1_953" }, port: "fm-1" }));
test("good install: the loader and the firmware keep the same port name", () =>
    goodInstall("", { dev: { names: { app: "FM-1", loader: "FM-1" } } }));
test("good install: slow re-enumeration (the loader appears after 400 ms)", () => goodInstall("", { dev: { reenum: 400 } }));
test("good install: other MIDI gear on the bus is never probed", async () => {
    const synth = new M.Mute("Some Synth");
    const r = await bench({ extra: [synth] });
    assert.deepEqual(r.res, { done: true, id: PRODUCT });
    assert.equal(synth.received.length, 0, "a port that is not an FM-1's received a message");
});

// ------------------------------------------------------------ the package, before anything ---

const BAD = {
    "a byte of flash.bin changed": r => { r[rawIndex(0x400 + 0x40000)] ^= 1; },
    "a byte of ota.bin (the loader) changed": r => { r[rawIndex(0x93400 + 100)] ^= 0x80; },
    "the header changed": r => { r[5] ^= 1; },
    "the file list changed": r => { r[rawIndex(0x45)] ^= 4; },
    "cut short": r => r.subarray(0, r.length - 5000),
    "much too short (100 bytes)": r => r.subarray(0, 100),
    "not a package (random bytes)": r => { let s = 7; for (let i = 0; i < r.length; i++) { s = (s * 1103515245 + 12345) >>> 0; r[i] = s >>> 16; } },
    "no Felucca loader mark": r => { const i = Buffer.from(r).indexOf("FELUCCA-LOADER-1"); assert.ok(i > 0); r[i + 3] = 0x58; },
};
for (const [what, f] of Object.entries(BAD)) test(`damaged package refused, nothing written, no MIDI touched: ${what}`, async () => {
    let raw = Uint8Array.from(PKG);
    const cut = f(raw);
    if (cut) raw = Uint8Array.from(cut);
    const r = await bench({ bytes: raw });
    assert.ok(r.err instanceof W.InstallError && r.err.code === "badpkg", r.err && r.err.message);
    assert.equal(r.midi.requests, 0, "the browser's MIDI was asked for before the package was checked");
    assert.equal(r.dev.stats.handshakes + r.dev.stats.upgrades, 0);
    assert.ok(!r.dev.appChanged() && r.dev.untouched());
    assert.deepEqual([r.asked.length, r.busy.length, r.steps.length], [0, 0, 0]);
    assert.equal(r.dev.mode, "app");
    BAD[what].msg = r.err.message;
});
test("damaged package: the messages say what and that nothing was written", () => {
    assert.match(BAD["a byte of flash.bin changed"].msg, /: damaged package \(flash\.bin: CRC\); nothing was written\. Download it again\.$/);
    assert.match(BAD["a byte of ota.bin (the loader) changed"].msg, /\(ota\.bin: CRC\)/);
    assert.match(BAD["the header changed"].msg, /\(header CRC\)/);
    assert.match(BAD["the file list changed"].msg, /\(file list CRC\)/);
    assert.match(BAD["cut short"].msg, /\(ota\.bin: cut short\)/);
    assert.equal(BAD["much too short (100 bytes)"].msg, "not an FM-1 package (too short)");
    assert.match(BAD["not a package (random bytes)"].msg, /: not an FM-1 package \(identity ['"]/);
    assert.match(BAD["no Felucca loader mark"].msg, /: no Felucca update loader in this package; only Felucca's own packages are installed$/);
});

// ------------------------------------------------------------- before anything is written ---

function refused(code, text, o) {
    return async () => {
        const r = await bench(o);
        assert.ok(r.err instanceof W.InstallError, "no error: " + JSON.stringify(r.res));
        assert.equal(r.err.code, code);
        assert.equal(r.err.message, text);
        assert.equal(r.asked.length, 0, "the user was asked to confirm something that is refused");
        assert.deepEqual(r.busy, []);
        assert.equal(r.dev.stats.upgrades, 0, "UPGRADE was sent");
        assert.equal(r.dev.stats.stepReads + r.dev.stats.loaderReads, 0);
        assert.ok(!r.dev.appChanged() && r.dev.untouched());
        assert.ok(r.dev.stats.handshakes >= 1, "the identity was asked for");
        assert.equal(r.dev.inPort.connection, "closed", "port left open");
    };
}
test("wrong model refused: the device is an FM-2", refused("model", `the device is FM-2_015, the package is for ${PRODUCT}`,
                                                                { dev: { appId: "FM-2_015" } }));
test("wrong model refused: the package is for an FM-2", refused("model", "the device is FM-1_015, the package is for FM-2_956",
                                                                { bytes: withProduct(PKG, "FM-2_956") }));
test("wrong model refused: an FM-2 in update mode", refused("model", `the device is ota-FM-2_900, the package is for ${PRODUCT}`,
                                                              { dev: { mode: "loader", loaderId: "ota-FM-2_900" } }));
test("another firmware's update mode refused (ota-FM-1_015: M-UPGRADE's)", refused("foreign",
    "the FM-1 is in the update mode of another firmware (ota-FM-1_015): finish that update with its own updater (M-UPGRADE), then install again",
    { dev: { mode: "loader", loaderId: "ota-FM-1_015", names: { app: "FM-1", loader: "ota-FM-1_015" } } }));
test("the user says no: cancelled, nothing written", async () => {
    const r = await bench({ confirm: async () => false });
    assert.deepEqual(r.res, { cancelled: true });
    assert.equal(r.lines[r.lines.length - 1], "cancelled");
    assert.equal(r.dev.stats.upgrades, 0); assert.ok(!r.dev.appChanged());
    assert.deepEqual(r.busy, []); assert.equal(r.dev.inPort.connection, "closed");
});
test("no confirm callback: nothing is ever written", async () => {
    const r = await bench({ confirm: undefined });
    assert.deepEqual(r.res, { cancelled: true });
    assert.equal(r.dev.stats.upgrades, 0);
});

// ----------------------------------------------------- update mode, cut cable, resume ---

test("device found in update mode: only the write is finished (and the sectors already right are skipped)", async () => {
    const midi = new M.MockMidi();
    const dev = midi.add(new M.Fm1({ mode: "loader" }));
    const exp = dev.expectedApp(PKG);
    dev.flash.set(exp.subarray(0, 0x20000), M.APP_LO);                 // a third of the app was written before the cut
    const r = await bench.run(midi, {});
    assert.equal(r.err, undefined, r.err && r.err.message);
    assert.deepEqual(r.res, { done: true, id: PRODUCT });
    assert.deepEqual([...new Set(kinds(r))], ["write", "reboot", "done"]);
    assert.match(r.lines[1], /^device: {2}ota-FM-1_900 {2}\(ota-FM-1_900\) {2}in update mode: the write will be finished$/);
    assert.equal(r.asked[0].resume, true);
    assert.equal(dev.stats.stepReads, 0); assert.equal(dev.stats.upgrades, 1); assert.equal(dev.stats.commits, 0);
    assert.ok(dev.stats.sectorsSkipped >= 32, "sectors already equal were rewritten: " + dev.stats.sectorsSkipped);
    assert.ok(Buffer.from(dev.appArea()).equals(Buffer.from(exp)) && dev.untouched());
    assert.equal(dev.stats.finishes, 1); assert.equal(dev.appId, PRODUCT);
    quiet(dev);
});
test("cable cut mid-write: a clear error; replugged, the install resumes and finishes", async () => {
    const midi = new M.MockMidi();
    const dev = midi.add(new M.Fm1({ cutAtSector: 100 }));
    const first = await bench.run(midi, {});
    assert.equal(first.err && first.err.code, "lost");
    assert.equal(first.err.message, `the loader was disconnected after ${dev.stats.loaderReads} requests. Replug the USB cable and run the install again to finish`);
    assert.equal(dev.mode, "off"); assert.equal(dev.record, true, "the update record must survive a power cut");
    assert.equal(dev.stats.sectorsWritten, 100);
    assert.ok(!Buffer.from(dev.appArea()).equals(Buffer.from(dev.expectedApp(PKG))), "half-written");
    assert.ok(dev.appChanged() && dev.untouched());
    assert.deepEqual(first.busy, [true, false], "busy is released after the error");
    assert.equal(first.steps[first.steps.length - 1][0], "write");
    // the user replugs: the FM-1 comes up in its loader; same page, same click
    dev.o.cutAtSector = -1;
    dev.replug();
    assert.equal(dev.mode, "loader");
    const reads1 = dev.stats.stepReads;
    const second = await bench.run(midi, {});
    assert.equal(second.err, undefined, second.err && second.err.message);
    assert.deepEqual(second.res, { done: true, id: PRODUCT });
    assert.equal(dev.stats.stepReads, reads1, "no step 1 on a resume");
    assert.ok(dev.stats.sectorsSkipped >= 100, "the 100 sectors written before the cut are not written again: " + dev.stats.sectorsSkipped);
    assert.ok(Buffer.from(dev.appArea()).equals(Buffer.from(dev.expectedApp(PKG))) && dev.untouched());
    assert.equal(dev.stats.finishes, 1); assert.equal(dev.appId, PRODUCT); assert.equal(dev.mode, "app");
    assert.match(second.lines[1], /in update mode: the write will be finished/);
    quiet(dev);
});
test("cable cut in step 1: 'nothing was written', the old firmware is still there; replugged, a new install works", async () => {
    const midi = new M.MockMidi();
    const dev = midi.add(new M.Fm1({ cutAtStepRead: 10 }));
    const first = await bench.run(midi, {});
    assert.equal(first.err && first.err.code, "lost");
    assert.equal(first.err.message, `the FM-1 was disconnected after ${dev.stats.stepReads - 1} requests; nothing was written`);
    assert.equal(dev.record, false); assert.ok(!dev.appChanged());
    dev.o.cutAtStepRead = 0;
    dev.replug();
    assert.equal(dev.mode, "app");
    const second = await bench.run(midi, {});
    assert.deepEqual(second.res, { done: true, id: PRODUCT });
    assert.ok(Buffer.from(dev.appArea()).equals(Buffer.from(dev.expectedApp(PKG))));
});
test("the loader stops answering mid-write: 'stopped answering', and the install can be run again", async () => {
    const midi = new M.MockMidi();
    const dev = midi.add(new M.Fm1({ muteAtLoaderRead: 1500 }));
    const first = await bench.run(midi, {});
    assert.equal(first.err && first.err.code, "stopped");
    assert.equal(first.err.message, `the loader stopped answering after ${dev.stats.loaderReads - 1} requests. Replug the USB cable and run the install again to finish`);
    assert.deepEqual(first.busy, [true, false]);
});
test("the update loader never appears after step 1: 'did not appear' (FM-1 stays in update mode)", async () => {
    const midi = new M.MockMidi();
    const dev = midi.add(new M.Fm1({ reenum: 60000 }));          // gone for a minute
    const save = W.DELAY.wait_loader;
    W.DELAY.wait_loader = 300;
    const r = await bench.run(midi, {});
    W.DELAY.wait_loader = save;
    assert.equal(r.err && r.err.code, "noloader");
    assert.equal(r.err.message, "the update loader did not appear. Replug the USB cable and run the install again: the FM-1 stays in update mode until it is done");
    dev.unplug();                                                  // (stops the simulator's pending restart)
});
test("the FM-1 comes back with another identity: 'written, but the FM-1 reports ...'", async () => {
    const r = await bench({ dev: { afterId: "FM-1_999" } });
    assert.equal(r.err && r.err.code, "mismatch");
    assert.equal(r.err.message, `written, but the FM-1 reports FM-1_999 (expected ${PRODUCT})`);
});
test("the FM-1 does not come back after the write: 'power-cycle it'", async () => {
    const midi = new M.MockMidi();
    const dev = midi.add(new M.Fm1({ mode: "loader" }));
    dev.o.reenum = 60000;
    const save = W.DELAY.wait_reboot;
    W.DELAY.wait_reboot = 300;
    const r = await bench.run(midi, {});
    W.DELAY.wait_reboot = save;
    assert.equal(r.err && r.err.code, "noreturn");
    assert.equal(r.err.message, "the FM-1 did not come back: power-cycle it");
    dev.unplug();
});
test("a device that asks outside the package: refused with the offset", async () => {
    const midi = new M.MockMidi();
    const dev = midi.add(new M.Fm1({ mode: "loader" }));
    const r0 = dev.otaRead.bind(dev);
    dev.otaRead = (tok, addr, len, c) => r0(tok, addr === 0x4000 + 0x400 ? 0x7FFFFFF0 : addr, len, c);   // (a broken device)
    const r = await bench.run(midi, {});
    assert.equal(r.err && r.err.code, "badreq");
    assert.match(r.err.message, /^the device asked for 0x7ffffff0\+\d+, outside the package$/);
    assert.equal(dev.inPort.connection, "closed", "the port is closed after an error");
    dev.unplug();
});

// ------------------------------------------------------------- no port, no Web MIDI ---

test("no MIDI port at all: 'FM-1 not found' with the (none) list", async () => {
    const r = await bench({ dev: null });
    assert.equal(r.err.code, "notfound");
    assert.equal(r.err.message, "FM-1 not found (USB data cable? another app using it?)\n  MIDI inputs: (none)");
    assert.equal(r.midi.sysexAsked[0], true);
});
test("only other gear: not found, its ports are listed and never probed", async () => {
    const synth = new M.Mute("Some Synth"), pad = new M.Mute("Launchpad X");
    const r = await bench({ dev: null, extra: [synth, pad] });
    assert.equal(r.err.code, "notfound");
    assert.equal(r.err.message, "FM-1 not found (USB data cable? another app using it?)\n  MIDI inputs: Some Synth, Launchpad X");
    assert.equal(synth.received.length + pad.received.length, 0);
});
test("a port named like an FM-1 that does not answer: not found", async () => {
    const mute = new M.Mute("FM-1 (silent)");
    const r = await bench({ dev: null, extra: [mute] });
    assert.equal(r.err.code, "notfound");
    assert.match(r.err.message, /MIDI inputs: FM-1 \(silent\)$/);
    assert.ok(mute.received.length >= 2, "two handshake tries");
});
test("the FM-1's port is taken by another program (open fails): not found", async () => {
    const midi = new M.MockMidi();
    const dev = midi.add(new M.Fm1());
    dev.inPort.open = () => Promise.reject(Object.assign(new Error("busy"), { name: "InvalidAccessError" }));
    const r = await bench.run(midi, {});
    assert.equal(r.err.code, "notfound"); assert.match(r.err.message, /another app using it\?\)\n {2}MIDI inputs: FM-1$/);
    assert.equal(dev.stats.upgrades, 0);
});
test("an FM-1 input with no output to pair with: not found", async () => {
    const midi = new M.MockMidi();
    const dev = midi.add(new M.Fm1());
    dev.outPort.state = "disconnected"; midi.changed(dev.outPort);
    const r = await bench.run(midi, {});
    assert.equal(r.err.code, "notfound");
});
test("a port filter that matches nothing: the message names it", async () => {
    const r = await bench({ port: "zzz" });
    assert.equal(r.err.code, "notfound");
    assert.equal(r.err.message, "no MIDI port matching 'zzz' answered (USB data cable? another app using it?)\n  MIDI inputs: FM-1");
});
test("MIDI inputs list that cannot be read: the browser's reason is shown", async () => {
    const midi = new M.MockMidi();
    midi.add(new M.Fm1());
    const orig = midi.requestMIDIAccess.bind(midi);
    midi.requestMIDIAccess = async o => { const a = await orig(o); Object.defineProperty(a, "inputs", { get() { throw new Error("MIDI system unavailable"); } }); return a; };
    const r = await bench.run(midi, {});
    assert.equal(r.err.code, "notfound");
    assert.equal(r.err.message, "FM-1 not found: the MIDI system could not be read\n  MIDI system unavailable");
});
for (const [what, nav] of [["navigator without requestMIDIAccess", {}], ["no navigator", undefined], ["requestMIDIAccess is not a function", { requestMIDIAccess: 1 }]])
    test(`browser without Web MIDI (${what}): clear message, nothing touched`, async () => {
        const r = await bench({ nav, dev: null });
        assert.equal(r.err.code, "nomidi");
        assert.equal(r.err.message, "this browser has no Web MIDI with SysEx, which the installer needs to talk to the FM-1. " +
                     "Use Chrome, Edge or Opera on a computer, or the INSTALL-Windows.bat / INSTALL-Mac.command / INSTALL-Linux.sh launcher of the repository");
        assert.deepEqual([r.asked.length, r.busy.length], [0, 0]);
    });
test("MIDI permission refused: clear message", async () => {
    const r = await bench({ midi: { reject: "SecurityError" } });
    assert.equal(r.err.code, "nomidi");
    assert.equal(r.err.message, "the browser refused MIDI access with SysEx (SecurityError): allow it for this page (the permission prompt, or the padlock next to the address), then try again");
    assert.equal(r.dev.stats.handshakes, 0);
});
test("probe (fm1_install.py --info): what the connected FM-1 runs", async () => {
    const midi = new M.MockMidi();
    midi.add(new M.Fm1());
    assert.deepEqual(await W.probe({ nav: midi }), { id: "FM-1_015", loader: false, port: "FM-1", text: "FM-1_015  [running]  port: FM-1" });
    const m2 = new M.MockMidi();
    m2.add(new M.Fm1({ mode: "loader" }));
    assert.equal((await W.probe({ nav: m2 })).text, "ota-FM-1_900  [update loader (update not finished)]  port: ota-FM-1_900");
    await assert.rejects(W.probe({ nav: new M.MockMidi() }), e => e.code === "notfound");
});

// ---------------------------------------------------------------- units ---

test("pack7 / unpack7 round trip and the wire of a read request and its answer", () => {
    for (const n of [0, 1, 6, 7, 8, 15, 34, 512, 513]) {
        const d = Uint8Array.from({ length: n }, (_, i) => (i * 37 + 11) & 0xFF);
        const p = W.pack7(d);
        assert.ok(p.every(b => b < 0x80));
        assert.equal(p.length, Math.ceil(n * 8 / 7));
        assert.deepEqual(W.unpack7(p), d);
        assert.deepEqual(Array.from(W.pack7(d)), Array.from(M.pack7(d)), "installer and simulator pack7 differ");
    }
    // the simulator asks (device side, ota.c): the installer must read it
    const req = M.msgFrame(0x30, Uint8Array.of(0, 0x34, 0x12, 0x00, 0x00, 0x00, 0x02, 0));    // [0, addr32, len16, 0]
    assert.deepEqual(W.parseRequest(req), { fl: 0, addr: 0x1234, len: 512 });
    const ans = W.response(0x1234, Uint8Array.of(1, 2, 3), 0);
    assert.ok(ans.slice(1, -1).every(b => b < 0x80) && ans[0] === 0xF0 && ans[ans.length - 1] === 0xF7);
    // the host's HS_QUERY decodes to the 7-byte message the firmware answers (ota_service)
    assert.deepEqual(Array.from(M.unpack7(W.HS_QUERY.subarray(1, -1))), [0, 0x59, 0x11, 0, 0, 0, 0xFF]);
    assert.deepEqual(Array.from(W.UPGRADE), [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7]);
});
test("crc16 (table) equals the bit-by-bit CRC of the firmware; the real package's sections add up", () => {
    const b = Uint8Array.from({ length: 5000 }, (_, i) => (i * 131 + 7) & 0xFF);
    assert.equal(W.crc16(b), M.crc16(b));
    assert.equal(W.crc16(b.subarray(0, 100), W.crc16(b.subarray(100))), M.crc16(b.subarray(0, 100), M.crc16(b.subarray(100))));
    assert.equal(W.crc16(new TextEncoder().encode("123456789")), 0x31C3);       // CRC-16/XMODEM check value
    const img = W.logicalImage(PKG);
    assert.ok(Buffer.from(img).equals(Buffer.from(M.logicalOf(PKG))), "logical image differs from the simulator's");
    assert.equal(W.packageDamage(img), null);
    assert.equal(W.productOf(PKG), PRODUCT);
});
test("pair_output: exact name, Windows numbering, the only output, else none", () => {
    assert.equal(W.pairOutput("FM-1", ["X", "FM-1"]), "FM-1");
    assert.equal(W.pairOutput("FM-1 2", ["FM-1 3", "Other"]), "FM-1 3");
    assert.equal(W.pairOutput("FM-1 2", ["FM-1 3", "FM-1 4"]), null);
    assert.equal(W.pairOutput("A", ["B"]), "B");
    assert.equal(W.pairOutput("A", ["B", "C"]), null);
    assert.equal(W.pairOutput("A", []), null);
});
test("the package of the site: newest in SHA256SUMS, its hash is the file's, fetched with relative URLs", async () => {
    assert.equal(W.newestInSums(sums).name, PKGNAME);
    const files = fs.readdirSync(path.join(ROOT, "install")).filter(f => /^kawa-[\d.]+\.fwsc$/.test(f)).sort((a, b) => a.localeCompare(b, "en", { numeric: true }));
    assert.equal(files[files.length - 1], PKGNAME, "the newest install/kawa-*.fwsc is not the one SHA256SUMS names");
    assert.equal(await W.sha256hex(PKG), latest.sha256, "install/SHA256SUMS does not match the package: the page would refuse it");
    const h = "a".repeat(64), g = "b".repeat(64);
    assert.deepEqual(W.newestInSums(`${h}  older/kawa-0.9.fwsc\n${g}  kawa-0.9.fwsc\n${h}  kawa-0.10.fwsc\n${h}  kawa-0.2.fwsc\n`), { name: "kawa-0.10.fwsc", sha256: h });
    assert.equal(W.newestInSums("junk\n"), null);
    const asked = [];
    const bytesOf = b => b.buffer.slice(b.byteOffset, b.byteOffset + b.length);
    const fake = async (url, o) => { asked.push(url); assert.equal(o.cache, "no-cache");
        if (url === "install/SHA256SUMS") return { ok: true, text: async () => sums };
        if (url === "install/" + PKGNAME) return { ok: true, arrayBuffer: async () => bytesOf(PKG) };
        return { ok: false, status: 404 }; };
    const got = await W.fetchLatest(fake);
    assert.deepEqual(asked, ["install/SHA256SUMS", "install/" + PKGNAME]);
    assert.equal(got.name, PKGNAME); assert.deepEqual(Array.from(got.bytes), Array.from(PKG)); assert.equal(got.sha256, latest.sha256);
    // a newer package named in the sums (0.5 after 0.4, said nowhere else): that one is fetched, by its relative URL
    const h5 = await W.sha256hex(PKG), asked5 = [];
    const sums5 = `${h5}  kawa-0.6.fwsc\n${"c".repeat(64)}  kawa-0.4.fwsc\n${"d".repeat(64)}  older/kawa-0.3.fwsc\n`;
    const fake5 = async url => { asked5.push(url);
        return url === "install/SHA256SUMS" ? { ok: true, text: async () => sums5 } : { ok: true, arrayBuffer: async () => bytesOf(PKG) }; };
    assert.equal((await W.fetchLatest(fake5)).name, "kawa-0.6.fwsc");
    assert.deepEqual(asked5, ["install/SHA256SUMS", "install/kawa-0.6.fwsc"]);
    // no SHA256SUMS (a page opened from a file, offline): the newest cannot be known, so it says so instead of guessing a version
    const noSums = async url => (url === "install/SHA256SUMS" ? { ok: false, status: 404 } : fake(url, { cache: "no-cache" }));
    await assert.rejects(W.fetchLatest(noSums), e => e.code === "download" && /^cannot read install\/SHA256SUMS to find the newest package \(HTTP 404\); opened from a file, or offline\? Choose the \.fwsc file from the install folder instead$/.test(e.message));
    await assert.rejects(W.fetchLatest(async url => ({ ok: true, text: async () => "nothing here\n" })), e => /no kawa-\*\.fwsc in it/.test(e.message));
    // a download that is not what the sums say
    const wrong = async url => (url === "install/SHA256SUMS" ? fake(url, { cache: "no-cache" }) : { ok: true, arrayBuffer: async () => new ArrayBuffer(1000) });
    await assert.rejects(W.fetchLatest(wrong), e => e.code === "download" && /does not match install\/SHA256SUMS; nothing was written/.test(e.message));
    // network failure (offline, or a page opened from a file): says what to do
    await assert.rejects(W.fetchLatest(async () => { throw new TypeError("Failed to fetch"); }), e => e.code === "download" && /\(Failed to fetch\); opened from a file, or offline\? Choose the \.fwsc file/.test(e.message));
    const sumsOnly = async url => (url === "install/SHA256SUMS" ? { ok: true, text: async () => sums } : { ok: false, status: 404 });
    await assert.rejects(W.fetchLatest(sumsOnly), e => e.code === "download" && /^cannot download install\/kawa-[\d.]+\.fwsc \(HTTP 404\); choose the \.fwsc file yourself instead$/.test(e.message));
});
test("index.html: loads install/webinstall.js, links exist, nothing from the network", () => {
    const html = fs.readFileSync(path.join(ROOT, "index.html"), "utf8");
    assert.match(html, /<script src="install\/webinstall\.js"><\/script>/);
    const rel = [...html.matchAll(/(?:href|src)="([^"#]+)(?:#[^"]*)?"/g)].map(m => m[1]).filter(u => !/^(https?:|mailto:|data:)/.test(u));
    assert.ok(rel.length >= 6);
    const PENDING = ["samples.html"];                          // the sample importer, being written by someone else: linked, not here yet
    for (const u of rel) assert.ok(fs.existsSync(path.join(ROOT, u)) || PENDING.includes(u), "broken link: " + u);
    for (const u of PENDING) if (!fs.existsSync(path.join(ROOT, u))) console.log(`       (note: ${u} is linked but does not exist yet)`);
    assert.doesNotMatch(html, /kawa-\d/, "a package version is written in index.html");
    assert.doesNotMatch(fs.readFileSync(path.join(ROOT, "install", "webinstall.js"), "utf8"), /kawa-\d/, "a package version is written in webinstall.js");
    assert.match(html, /Made for chiptune and IDM: breakbeat slicing, rolls, stutter, glitch effects, FM and 4-bit waves, 6 tracks/);
    for (const u of ["manual/index.html", "manual/fr.html", "manual/es.html", "emulator/index.html", "samples.html", "manual/img/kawa-preview.png"])
        assert.ok(rel.includes(u), "index.html does not use " + u);
    const ext = [...html.matchAll(/<(?:script|link|img|iframe|source)\b[^>]*(?:src|href)="(https?:[^"]+)"/g)].map(m => m[1]);
    assert.deepEqual(ext.filter(u => !/^https:\/\/github\.com\/Guizmoh\/Kawa-Tracker-Fm1/.test(u)), [], "a resource loaded from the network");
    assert.ok(!/@import|url\(\s*["']?https?:/.test(html), "a stylesheet loads from the network");
    assert.match(html, /https:\/\/github\.com\/Guizmoh\/Kawa-Tracker-Fm1/);
    assert.ok(!/<meta[^>]+http-equiv="refresh"/i.test(html), "still a redirect");
});

// ------------------------------------------------ against the Python tool itself ---

function python(job) {
    const script = `
import sys, json, importlib.util
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("fm1_install", sys.argv[1]); m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
j = json.load(sys.stdin); h = bytes.fromhex
o = {"delay": m.DELAY, "consts": {"HS_QUERY": m.HS_QUERY.hex(), "UPGRADE": m.UPGRADE.hex(), "FINISH_CHECK": m.FINISH_CHECK,
     "FINISH_WRITE": m.FINISH_WRITE, "MAXDATA": m.MAXDATA, "BLOCKS": m.BLOCKS, "BLK": m.BLK, "KEEP": m.KEEP,
     "LOADER_MARK": m.LOADER_MARK.decode(), "PORT_RE": m.PORT_RE.pattern, "PORT_RE_I": bool(m.PORT_RE.flags & 2)}}
o["pack"] = [[m.pack7(h(x)).hex(), m.unpack7(h(x)).hex()] for x in j["hex"]]
o["resp"] = [m.response(a, h(d), fl).hex() for a, d, fl in j["resp"]]
o["req"] = [list(r) if (r := m.parse_request(h(x))) else None for x in j["req"]]
o["ident"] = [[i.text, i.model, i.version, i.loader] if (i := m.parse_identity(h(x))) else None for x in j["ident"]]
o["crc"] = [[m._crc16(h(x)), m._dec(h(x)).hex()] for x in j["crc"]]
o["pair"] = [m.pair_output(n, outs) for n, outs in j["pair"]]
o["model"] = [m.model_of(t) for t in j["model"]]
pk = []
for p in j["pkgs"]:
    try:
        raw = open(p, "rb").read()
        try: prod = m.product_of(raw)
        except m.InstallError as e: prod = None
        try:
            product, image = m.load_package(p, False); pk.append({"ok": True, "product": product, "image": __import__("hashlib").sha256(image).hexdigest()})
        except m.InstallError as e: pk.append({"ok": False, "msg": str(e), "product": prod})
    except Exception as e: pk.append({"ok": False, "msg": "EXC " + repr(e)})
o["pkgs"] = pk
print(json.dumps(o))
`;
    const r = cp.spawnSync("python3", ["-B", "-I", "-c", script, path.join(ROOT, "install", "fm1_install.py")], { input: JSON.stringify(job), encoding: "utf8", maxBuffer: 1 << 28 });
    if (r.status !== 0) throw new Error("python3 failed: " + r.stderr);
    return JSON.parse(r.stdout);
}
const hex = b => Buffer.from(b).toString("hex");
const hasPython = (() => { try { return cp.spawnSync("python3", ["-B", "-I", "-c", "import sys; assert sys.version_info >= (3, 8)"]).status === 0; } catch (e) { return false; } })();

test(hasPython ? "parity with install/fm1_install.py: constants, delays, wire format, CRC, pair_output, packages" : "parity with install/fm1_install.py: SKIPPED (no python3 on the PATH)", () => {
    if (!hasPython) return;
    let s = 99;
    const rnd = () => { s = (s * 1103515245 + 12345) >>> 0; return s >>> 16 & 0xFF; };
    const rb = n => Uint8Array.from({ length: n }, rnd);
    const hexes = [0, 1, 2, 5, 6, 7, 8, 9, 14, 15, 16, 27, 34, 100, 511, 512, 513].map(n => hex(rb(n)));
    const resp = [[0, hex(rb(512)), 0], [0x400, hex(rb(3)), 0x12], [0x93000, hex(rb(0)), 0], [0xE0000000, hex(Buffer.from("success\0")), 0],
                  [0xF0000000, hex(Buffer.from("success\0")), 0], [0xFFFFFFFF, hex(rb(7)), 0x7F], [0x12345678, hex(rb(300)), 1]];
    const reqs = [], frames = [0, 1, 0x1234, 0x93000, 0xFFFFFFFF].map(a => M.msgFrame(0x30, Uint8Array.of(0, a & 255, a >>> 8 & 255, a >>> 16 & 255, a >>> 24 & 255, 0, 2, 0)));
    for (const f of frames) {
        reqs.push(hex(f));
        for (const i of [1, 3, 5, 9, 11, 14]) reqs.push(hex(mutate(f, g => { g[i] = (g[i] + 1) & 0x7F; })));
        reqs.push(hex(f.subarray(0, f.length - 1)), hex(f.subarray(1)), hex(Buffer.concat([f.subarray(0, f.length - 1), Buffer.from([0x11, 0xF7])])));
    }
    reqs.push(hex(W.HS_QUERY), hex(W.UPGRADE), "", "f0", "f7f0");
    const idents = [];
    for (const t of ["FM-1_015", "ota-FM-1_900", "FM-1_956", "x", "FM-1", "A_B_1", "FM-1_", "FM-1_9a", "FM-\xE91_001", "OTA-FM-1_9", "_1", "ota-_5"]) {
        const body = new Uint8Array(27); body.set(Uint8Array.from(t, c => c.charCodeAt(0)));
        idents.push(hex(M.msgFrame(0x11, body)));
    }
    idents.push(hex(M.msgFrame(0x11, new Uint8Array(26))), hex(M.msgFrame(0x12, new Uint8Array(27))), hex(M.msgFrame(0x11, new Uint8Array(27))));
    const crcs = [0, 1, 5, 64, 80, 1000, 4096].map(n => hex(rb(n)));
    const pair = [["FM-1", ["X", "FM-1"]], ["FM-1 2", ["FM-1 3", "Other"]], ["FM-1 2", ["FM-1 3", "FM-1 4"]], ["A", ["B"]], ["A", ["B", "C"]], ["A", []],
                  ["MIDIIN2 (FM-1)", ["MIDIOUT2 (FM-1)"]], ["FM-1  7", ["FM-1 9"]], ["USB MIDI 12 34", ["USB MIDI 12 99", "x"]]];
    const model = ["FM-1_956", "ota-FM-1_900", "ota-FM-2_1", "FM-1", "ota-", "x_y_z", "OTA-FM-1_9"];
    // every variant of the package: the same verdict and the same message (the file name is the path, as in Python)
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "kawa-webinstall-"));
    const variants = { good: PKG, other_model: withProduct(PKG, "FM-2_956"), long_product: withProduct(PKG, "FM-1_956_x"), no_product: withProduct(PKG, "") };
    for (const [w, f] of Object.entries(BAD)) { const r = Uint8Array.from(PKG), c = f(r); variants[w.replace(/\W+/g, "_")] = c ? Uint8Array.from(c) : r; }
    for (const i of [0, 7, 0x3F, 0x45, 0x3FF, 0x401, 123456, 0x93500]) variants["flip_" + i] = mutate(PKG, r => { r[rawIndex(Math.min(i, 610245))] ^= 0x10; });
    variants.empty = new Uint8Array(0); variants.header_only = PKG.subarray(0, 600); variants.short_body = PKG.subarray(0, 1000);
    const files = Object.keys(variants).map(k => { const p = path.join(dir, k + ".fwsc"); fs.writeFileSync(p, variants[k]); return [k, p]; });
    let py;
    try { py = python({ hex: hexes, resp, req: reqs, ident: idents, crc: crcs, pair, model, pkgs: files.map(f => f[1]) }); }
    finally { fs.rmSync(dir, { recursive: true, force: true }); }
    // constants and delays (JS in ms = Python's seconds x 1000)
    for (const [k, v] of Object.entries(py.delay)) assert.equal(REAL[k], Math.round(v * 1000), "DELAY." + k);
    assert.deepEqual(Object.keys(REAL).filter(k => !(k in py.delay)).sort(), ["portopen", "refresh"], "delays the Python tool does not have");
    const c = py.consts;
    assert.equal(hex(W.HS_QUERY), c.HS_QUERY); assert.equal(hex(W.UPGRADE), c.UPGRADE);
    assert.deepEqual([W.FINISH_CHECK, W.FINISH_WRITE, W.MAXDATA, W.LOADER_MARK], [c.FINISH_CHECK, c.FINISH_WRITE, c.MAXDATA, c.LOADER_MARK]);
    assert.equal(W.PORT_RE.source, c.PORT_RE); assert.equal(W.PORT_RE.ignoreCase, c.PORT_RE_I);
    // wire format
    hexes.forEach((x, i) => { assert.deepEqual([hex(W.pack7(Buffer.from(x, "hex"))), hex(W.unpack7(Buffer.from(x, "hex")))], py.pack[i], "pack7/unpack7 " + x.slice(0, 20)); });
    resp.forEach(([a, d, fl], i) => assert.equal(hex(W.response(a, Buffer.from(d, "hex"), fl)), py.resp[i], "response " + a));
    reqs.forEach((x, i) => { const r = W.parseRequest(Uint8Array.from(Buffer.from(x, "hex"))); assert.deepEqual(r && [r.fl, r.addr, r.len], py.req[i], "parse_request " + x); });
    assert.ok(py.req.filter(Boolean).length >= 5 && py.req.filter(r => !r).length >= 10);
    idents.forEach((x, i) => { const r = W.parseIdentity(Uint8Array.from(Buffer.from(x, "hex"))); assert.deepEqual(r && [r.text, r.model, r.version, r.loader], py.ident[i], "parse_identity #" + i); });
    assert.ok(py.ident.filter(Boolean).length >= 5);
    crcs.forEach((x, i) => assert.deepEqual([W.crc16(Buffer.from(x, "hex")), hex(W.dec(Buffer.from(x, "hex")))], py.crc[i], "crc16/dec"));
    pair.forEach(([n, o], i) => assert.equal(W.pairOutput(n, o), py.pair[i], "pair_output " + n));
    model.forEach((t, i) => assert.equal(W.modelOf(t), py.model[i], "model_of " + t));
    // packages: same verdict, same message (the Python one adds " (--force overrides)", an option the page does not have)
    let refusedN = 0;
    files.forEach(([k, p], i) => {
        const raw = Uint8Array.from(variants[k]), want = py.pkgs[i];
        let got;
        try { const r = W.loadPackage(raw, p, false); got = { ok: true, product: r.product, image: require("node:crypto").createHash("sha256").update(r.image).digest("hex") }; }
        catch (e) { assert.ok(e instanceof W.InstallError, k + ": " + e); got = { ok: false, msg: e.message }; }
        if (!want.ok) { refusedN++; want.msg = want.msg.replace(" (--force overrides)", ""); }
        if (want.ok) assert.deepEqual(got, want, "package " + k);
        else assert.equal(got.msg, want.msg, "package " + k);
    });
    assert.ok(py.pkgs[0].ok && py.pkgs[0].product === PRODUCT, "the real package is accepted by both");
    assert.ok(refusedN >= 15, "only " + refusedN + " variants were refused");
});

// ------------------------------------------------------------------ run ---

(async () => {
    let pass = 0, fail = 0;
    const t00 = Date.now();
    console.log(`webinstall_test: package ${PKGNAME} (${PRODUCT}), node ${process.version}, python3 ${hasPython ? "found" : "not found"}`);
    for (const [name, fn] of tests) {
        const t0 = Date.now();
        try {
            await Promise.race([fn(), new Promise((_, rej) => setTimeout(() => rej(new Error("timeout (60 s)")), 60000).unref())]);
            console.log(`ok   - ${name} (${Date.now() - t0} ms)`);
            pass++;
        } catch (e) {
            console.log(`FAIL - ${name}\n       ${String((e && e.stack) || e).split("\n").slice(0, 6).join("\n       ")}`);
            fail++;
        }
    }
    console.log(`\n${pass} passed, ${fail} failed (${((Date.now() - t00) / 1000).toFixed(1)} s)`);
    process.exit(fail ? 1 : 0);
})();

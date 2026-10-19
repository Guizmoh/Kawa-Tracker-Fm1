// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Omnipotard
/* A simulated FM-1 on a simulated Web MIDI, for install/webinstall.js (source/tests/webinstall_test.js and
 * webinstall_ui_test.js). No hardware was available: the device side is written from the firmware's own
 * code, not from the installer under test:
 *   - the wire: firmware/src/ota.c (ota_pack7 / ota_unpack7, ota_send_msg, ota_take, ota_read: a read request is
 *     sent three times, 5 s apart (here: opt.readTimeout), and a handshake is answered meanwhile);
 *   - step 1 (the running firmware, ota.c ota_stage): the UFW header and entry list, the loader's header, the
 *     loader body (CRC16), then "success" at 0xE0000000, then the update record and a restart into the loader;
 *   - step 2 (the loader, loader/ldr_core.c ldr_session): flash.bin read once and checked against its CRC16 before
 *     anything is erased (the sector CRCs kept), then the app area [0x4000, 0x93000) sector by sector, skipping
 *     equal sectors, each verified, then "success" at 0xF0000000, the record dropped, a restart into the new app.
 * What is NOT the firmware: the stock M-VAVE firmware's own step 1 (its code is not in this tree), the chip,
 * USB, and the SPL. A real FM-1 is the only proof.
 * The Web MIDI side follows the spec's shape: MIDIAccess {inputs, outputs, onstatechange}, ports {id, name, state,
 * connection, open(), close(), onmidimessage, send()}; unplugging sets state "disconnected" and drops the port from
 * the maps, replugging makes new port objects; send() on a disconnected port throws InvalidStateError.
 * Faults (options of Fm1): cutAtLoaderRead / cutAtSector / cutAtStepRead (cable cut), muteAtLoaderRead (device stops
 * asking); of MockMidi: reject (permission error), stale (an access object never hears of plugs: only a new
 * requestMIDIAccess() does), noise (clock / active sensing / notes between messages), chunk (SysEx cut in pieces). */
(function (root, factory) {
    if (typeof module === "object" && module.exports) module.exports = factory();
    else root.Fm1Mock = factory();
})(typeof self !== "undefined" ? self : this, function () {
"use strict";

// --------------------------------------------------------------- helpers ---

let deferQ = [], deferCh = null;
function defer(fn) {                     // the next turn of the event loop, in order
    if (typeof setImmediate === "function") return void setImmediate(fn);
    if (!deferCh) {
        deferCh = new MessageChannel();
        deferCh.port1.onmessage = () => { const q = deferQ; deferQ = []; q.forEach(f => f()); };
    }
    deferQ.push(fn);
    deferCh.port2.postMessage(0);
}
const wait = ms => new Promise(r => (ms > 0 ? setTimeout(r, ms) : defer(r)));
const nowMs = () => (typeof performance !== "undefined" ? performance.now() : Date.now());

// firmware/src/ota.c: ota_pack7, ota_unpack7 (8-to-7, LSB first)
function pack7(inp) {
    const out = [];
    let acc = 0, nb = 0;
    for (const b of inp) {
        acc |= b << nb; nb += 8;
        while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; }
    }
    if (nb) out.push(acc & 0x7F);
    return Uint8Array.from(out);
}
function unpack7(inp) {
    const out = [];
    let acc = 0, nb = 0;
    for (const b of inp) {
        acc |= (b & 0x7F) << nb; nb += 7;
        if (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; }
    }
    return Uint8Array.from(out);
}
function crc16(p, c = 0) {               // ota_crc16: poly 0x1021, init 0 (bit by bit, as the C)
    for (let i = 0; i < p.length; i++) {
        c ^= p[i] << 8;
        for (let k = 0; k < 8; k++) c = c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1;
        c &= 0xFFFF;
    }
    return c;
}
function crc16a(p, c = 0) {              // ldr_crc16a: reflected 0xA001
    for (let i = 0; i < p.length; i++) {
        c ^= p[i];
        for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xA001 : c >>> 1;
    }
    return c >>> 0;
}
function jlEnc(p, off, n) {              // ota_jl_enc: UFW header cipher, key 0xFFFF
    let k = 0xFFFF;
    for (let i = 0; i < n; i++) {
        p[off + i] ^= k & 0xFF;
        k = ((k << 1) ^ (k & 0x8000 ? 0x1021 : 0)) & 0xFFFF;
    }
}
const rd16 = (p, o) => p[o] | p[o + 1] << 8;
const rd24 = (p, o) => rd16(p, o) | p[o + 2] << 16;
const rd32 = (p, o) => (rd24(p, o) | p[o + 3] << 24) >>> 0;
const eq = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
const ascii = s => Uint8Array.from(s, c => c.charCodeAt(0));

function msgFrame(cmd, body) {           // ota_send_msg: F0 pack7(00 59 cmd len24 body ~sum(body)) F7
    const m = new Uint8Array(8 + body.length);
    m.set([0, 0x59, cmd, body.length, 0, 0]);
    m.set(body, 6);
    let s = 0;
    for (const b of body) s += b;
    m[6 + body.length] = ~s & 0xFF;
    const w = pack7(m.subarray(0, 7 + body.length)), f = new Uint8Array(w.length + 2);
    f[0] = 0xF0; f.set(w, 1); f[f.length - 1] = 0xF7;
    return f;
}

function logicalOf(raw) {                // the package without its 20 marker bytes (what the device reads)
    const out = [];
    for (let i = 0; i < 20; i++) for (let k = 0; k < 0x2F; k++) out.push(raw[i * 0x30 + k]);
    const rest = raw.subarray(20 * 0x30), r = new Uint8Array(out.length + rest.length);
    r.set(out); r.set(rest, out.length);
    return r;
}
function productOf(raw) {                // the identity the new firmware will answer
    let s = "";
    for (let i = 0; i < 20; i++) if (raw[i * 0x30 + 0x2F] !== 0x7D) s += String.fromCharCode((raw[i * 0x30 + 0x2F] - i - 1) & 0xFF);
    return s;
}

/* ota_ufw's parse, on the first 1 KiB of the image: -> {fl: {off, len, crc}, ota: {off, len}} or a negative code
 * (-2: a CRC does not add up, -3: no flash.bin or ota.bin) */
function ufwParse(h) {
    h = Uint8Array.from(h);
    jlEnc(h, 0, 0x40);
    const nent = rd16(h, 8);
    if (crc16(h.subarray(2, 0x40)) !== rd16(h, 0) || nent === 0 || nent > 11 ||
        crc16(h.subarray(0x40, 0x40 + nent * 0x50)) !== rd16(h, 2)) return -2;
    const r = { fl: { off: 0, len: 0, crc: 0 }, ota: { off: 0, len: 0 } };
    for (let i = 0; i < nent; i++) {
        const e = h.subarray(0x40 + i * 0x50, 0x40 + (i + 1) * 0x50);
        jlEnc(e, 0, 0x50);
        if (rd16(e, 0) === 0) r.fl = { crc: rd16(e, 4), off: rd32(e, 8), len: rd32(e, 12) };
        if (rd16(e, 0) === 100) r.ota = { off: rd32(e, 8), len: rd32(e, 12) };
    }
    return r.fl.off && r.ota.off ? r : -3;
}

const UPGRADE = Uint8Array.of(0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7);
const APP_LO = 0x4000, APP_HI = 0x93000;

// ------------------------------------------------------------ Web MIDI ---

class MockPort {
    constructor(midi, dev, type, name) {
        this.id = `${type}-${++midi.seq}`;
        this.name = name;
        this.manufacturer = "mock";
        this.type = type;
        this.state = "connected";
        this.connection = "closed";
        this.onmidimessage = null;
        this.dev = dev;
    }
    open() {
        if (this.state === "disconnected") return Promise.reject(Object.assign(new Error("not connected"), { name: "InvalidAccessError" }));
        this.connection = "open";
        return Promise.resolve(this);
    }
    close() { this.connection = "closed"; return Promise.resolve(this); }
    send(data) {
        if (this.type !== "output") throw new TypeError("not an output");
        if (this.state === "disconnected") throw Object.assign(new Error("The port is not connected."), { name: "InvalidStateError" });
        this.connection = "open";
        this.dev.fromHost(Uint8Array.from(data));
    }
}

class MockAccess {
    constructor(midi) {
        this.midi = midi;
        this.inputs = new Map();
        this.outputs = new Map();
        this.onstatechange = null;
        for (const d of midi.devices) for (const p of [d.inPort, d.outPort]) if (p && p.state === "connected") this._add(p);
    }
    _add(p) { (p.type === "input" ? this.inputs : this.outputs).set(p.id, p); }
    _changed(p) {                        // the browser updates the maps and tells; `stale` accesses never hear
        if (this.midi.opt.stale) return;
        const m = p.type === "input" ? this.inputs : this.outputs;
        if (p.state === "connected") m.set(p.id, p); else m.delete(p.id);
        if (this.onstatechange) this.onstatechange({ port: p });
    }
}

class MockMidi {
    constructor(opt = {}) {
        this.opt = opt;                  // reject: error name; stale; noise; chunk
        this.seq = 0;
        this.devices = [];
        this.accesses = [];
        this.requests = 0;
        this.sysexAsked = [];
    }
    add(dev) { dev.midi = this; this.devices.push(dev); dev.plug(); return dev; }
    changed(p) { for (const a of this.accesses) a._changed(p); }
    async requestMIDIAccess(o) {
        this.requests++;
        this.sysexAsked.push(!!(o && o.sysex));
        await wait(0);
        if (this.opt.reject) throw Object.assign(new Error("permission refused"), { name: this.opt.reject });
        if (!(o && o.sysex)) throw Object.assign(new Error("SysEx not asked"), { name: "SecurityError" });   // the installer must ask for it
        const a = new MockAccess(this);
        this.accesses.push(a);
        return a;
    }
}

// a port pair that answers nothing (other gear, or an FM-1 that is silent), for "not found"
class Mute {
    constructor(name, outName = name) { this.names = [name, outName]; this.inPort = this.outPort = null; this.received = []; }
    plug() {
        this.inPort = new MockPort(this.midi, this, "input", this.names[0]);
        this.outPort = new MockPort(this.midi, this, "output", this.names[1]);
        this.midi.changed(this.inPort); this.midi.changed(this.outPort);
    }
    fromHost(b) { this.received.push(b); }
}

// ---------------------------------------------------------------- the FM-1 ---

class Fm1 {
    constructor(opt = {}) {
        this.o = Object.assign({
            mode: "app",                 // what it runs when plugged: app | loader
            appId: "FM-1_015",           // what the running firmware answers (the stock V15)
            loaderId: "ota-FM-1_900",    // what the update loader answers
            afterId: "FM-1_955",         // what the new firmware answers after a good install
            names: { app: "FM-1", loader: "ota-FM-1_900" },   // MIDI port names (the device re-enumerates)
            readTimeout: 300,            // ota_read: 5000 ms in the firmware, 3 tries
            reenum: 40,                  // ms off the bus while it restarts
            deadline: 120000,            // step 1: the whole session
            cutAtLoaderRead: 0, cutAtSector: -1, cutAtStepRead: 0, muteAtLoaderRead: 0,
            seed: 12345,
        }, opt);
        this.mode = "off";
        this.appId = this.o.appId;
        this.record = this.o.mode === "loader";          // the update record: set at step 1, dropped at the loader's end
        this.tok = 0;
        this.gen = 0;
        this.pending = null;
        this.inPort = this.outPort = null;
        this.events = [];
        this.protocolErrors = [];
        this.stats = { handshakes: 0, upgrades: 0, junk: 0, stray: 0, stepReads: 0, loaderReads: 0, sectorsWritten: 0,
                       sectorsSkipped: 0, commits: 0, finishes: 0, step1Fails: [], loaderFails: [] };
        this.flash = new Uint8Array(0x100000);
        let s = this.o.seed;                              // the app installed before, and the user's data: random bytes
        for (let i = 0; i < this.flash.length; i++) { s = (s * 1103515245 + 12345) >>> 0; this.flash[i] = s >>> 16 & 0xFF; }
        this.flash0 = this.flash.slice();                 // as plugged for the first time (to see what changed)
        this.staged = null;
    }

    // ---- bus
    plug() {
        const mode = this.record ? "loader" : "app";
        this.mode = mode;
        const name = this.o.names[mode];
        this.inPort = new MockPort(this.midi, this, "input", name);
        this.outPort = new MockPort(this.midi, this, "output", name);
        this.midi.changed(this.inPort);
        this.midi.changed(this.outPort);
        this.events.push("plug:" + mode);
    }
    unplug() {                                            // the cable goes (or the FM-1 resets): whatever it was doing stops
        this.gen++; this.tok++;
        this.mode = "off";
        this.pending = null;
        for (const p of [this.inPort, this.outPort]) if (p) { p.state = "disconnected"; p.connection = "pending"; this.midi.changed(p); }
        this.events.push("unplug");
    }
    replug() { this.plug(); }                             // the user plugs it back: a cut install comes up in the loader
    restart(after) {                                      // reset: gone for opt.reenum ms, then back
        this.unplug();
        const gen = this.gen;
        setTimeout(() => { if (this.gen === gen) { this.plug(); if (after) after(); } }, this.o.reenum);
    }

    // ---- host -> device
    fromHost(b) {
        if (b[0] !== 0xF0 || b[b.length - 1] !== 0xF7 || b.subarray(1, b.length - 1).some(x => x >= 0x80))
            this.protocolErrors.push("bad SysEx framing: " + Array.from(b.subarray(0, 8)).join(","));
        if (b.length > 640) this.protocolErrors.push("frame longer than the device's buffer (640): " + b.length);
        if (this.mode === "off") return;
        const gen = this.gen;
        defer(() => { if (this.gen === gen) this.handle(b); });
    }
    handle(b) {
        if (eq(b, UPGRADE)) { this.stats.upgrades++; this.events.push("upgrade"); return this.onUpgrade(); }
        const d = unpack7(b.subarray(1, b.length - 1));                         // ota_take
        let s = 0;
        for (let i = 6; i + 1 < d.length; i++) s += d[i];
        if (d.length < 7 || d.length > 640 || d[0] !== 0 || d[1] !== 0x59 || rd24(d, 3) + 7 !== d.length || (~s & 0xFF) !== d[d.length - 1]) {
            this.stats.junk++;
            return;
        }
        if (d.length === 7 && d[2] === 0x11) { this.stats.handshakes++; return this.replyIdentity(); }
        const p = this.pending;
        if (p && d.length === p.len + 15 && d[2] === 0x30 && rd24(d, 3) === p.len + 8 && rd32(d, 7) === p.addr && rd24(d, 11) === p.len) {
            this.pending = null;
            return p.resolve(d.slice(14, 14 + p.len));
        }
        this.stats.stray++;
    }
    replyIdentity() {                                                           // 27 bytes, zero padded
        const id = new Uint8Array(27);
        id.set(ascii(this.mode === "loader" ? this.o.loaderId : this.appId));
        this.emit(msgFrame(0x11, id));
    }

    // ---- device -> host
    emit(frame) {
        const gen = this.gen, out = this.inPort, opt = this.midi.opt;
        defer(() => {
            if (this.gen !== gen || out.state !== "connected" || out.connection !== "open" || !out.onmidimessage) return;
            if (opt.noise) for (const m of [[0xFE], [0xF8], [0x90, 0x3C, 0x7F]]) out.onmidimessage({ data: Uint8Array.from(m) });
            const n = opt.chunk || frame.length;
            for (let i = 0; i < frame.length; i += n) out.onmidimessage({ data: frame.slice(i, i + n) });
        });
    }
    alive(tok) { return this.tok === tok && this.mode !== "off"; }

    // ota_read: ask, wait for the matching answer (3 tries); null when it does not come
    async otaRead(tok, addr, len, counter) {
        if (this.deadline && nowMs() > this.deadline) return null;
        const k = ++this.stats[counter];
        if (counter === "stepReads" && this.o.cutAtStepRead && k === this.o.cutAtStepRead) { this.unplug(); return null; }
        if (counter === "loaderReads" && this.o.cutAtLoaderRead && k === this.o.cutAtLoaderRead) { this.unplug(); return null; }
        if (counter === "loaderReads" && this.o.muteAtLoaderRead && k === this.o.muteAtLoaderRead) {
            this.events.push("hung");
            return new Promise(() => {});                 // the device stops talking and never answers again
        }
        const q = new Uint8Array(8);
        q[1] = addr & 0xFF; q[2] = addr >>> 8 & 0xFF; q[3] = addr >>> 16 & 0xFF; q[4] = addr >>> 24 & 0xFF;
        q[5] = len & 0xFF; q[6] = len >> 8 & 0xFF;
        for (let t = 0; t < 3; t++) {
            if (!this.alive(tok)) return null;
            const got = new Promise(res => { this.pending = { addr, len, resolve: res }; });
            this.emit(msgFrame(0x30, q));
            const r = await Promise.race([got, wait(this.o.readTimeout).then(() => null)]);
            if (r) return r;
            this.pending = null;
        }
        return null;
    }

    onUpgrade() {
        if (this.session) return;                         // already in a session
        if (this.mode === "app") this.step1();
        else if (this.mode === "loader") this.loaderSession();
    }

    // ---- step 1: the running firmware (ota.c ota_stage / ota_session)
    async step1() {
        const tok = ++this.tok;
        this.session = "step1";
        this.deadline = nowMs() + this.o.deadline;
        const rd = (a, n) => this.otaRead(tok, a, n, "stepReads");
        let rc = await this.stage(tok, rd);
        this.session = null; this.deadline = 0;
        if (!this.alive(tok)) return;
        if (rc) { this.stats.step1Fails.push(rc); this.events.push("step1-fail:" + rc); this.staged = null; return; }
        this.record = true;                               // UPDATA_PARM written: the next boot runs the loader
        this.stats.commits++;
        this.restart();
    }

    async ufw(rd) {                                       // ota_ufw: the 1 KiB header from the host
        const h = new Uint8Array(0x400);
        const a = await rd(0, 512), b = a && await rd(512, 512);
        if (!a || !b) return -1;
        h.set(a, 0); h.set(b, 512);
        return ufwParse(h);
    }

    async stage(tok, rd) {
        const u = await this.ufw(rd);
        if (typeof u === "number") return u;
        let b = await rd(u.ota.off, 512);
        if (!b) return -6;
        const len = rd32(b, 8);
        if (crc16(b.subarray(2, 32)) !== rd16(b, 0) || rd32(b, 4) !== 0x20 || b[12] !== 0x41 || u.ota.len !== len + 0x20 ||
            len > 0x5000 - 0x200 || crc16(b.subarray(0x22, 0x40)) !== rd16(b, 0x20) || rd32(b, 0x28) !== 0x01C0A800) return -7;
        let own = false;                                  // the first LZ4 literals: "FELUCCA-LOADER-1"
        for (let i = 0x48; i + 16 <= 0x48 + 64 && !own; i++) own = eq(b.subarray(i, i + 16), ascii("FELUCCA-LOADER-1")) && rd32(b, 0x24) <= 0x14000;
        if (!own) return -7;                              // (the official loader's branch, which compares the flash head, is not simulated)
        const c = rd16(b, 2);                             // the outer CRC of the body
        const area = new Uint8Array(0x5000);
        let crc = 0;
        for (let i = 0; i < len; i += 512) {
            const n = Math.min(512, len - i);
            b = await rd(u.ota.off + 0x20 + i, n);
            if (!b) return -9;
            crc = crc16(b, crc);
            area.set(b, 32 + i);
        }
        if (crc !== c) return -11;
        this.staged = area;
        for (let i = 0; i < 4; i++) {                     // the host confirms: nothing is committed without it
            b = await rd(0xE0000000, 8);
            if (b && eq(b, ascii("success\0"))) return 0;
            if (!this.alive(tok)) return -13;
        }
        return -13;
    }

    // ---- step 2: the update loader (ldr_core.c ldr_session)
    async loaderSession() {
        const tok = ++this.tok;
        this.session = "loader";
        const rd = (a, n) => this.otaRead(tok, a, n, "loaderReads");
        const rc = await this.ldr(tok, rd);
        this.session = null;
        if (!this.alive(tok)) return;
        if (rc) { this.stats.loaderFails.push(rc); this.events.push("loader-fail:" + rc); return; }   // the loader stays, the host can resume
        this.record = false;
        this.stats.finishes++;
        this.appId = this.o.afterId;
        this.restart();
    }

    async ldr(tok, rd) {
        const u = await this.ufw(rd);
        if (typeof u === "number") return u;
        const fl = u.fl;
        if (fl.len < APP_HI || fl.len > 0x100000) return -3;
        if (!await rd(fl.off + APP_LO, 32)) return -5;    // (the chip-key check: not simulated)
        const nsec = (APP_HI - APP_LO) >> 12;
        const sc = new Array(nsec).fill(0);
        let ok = false;                                   // 2b. flash.bin whole, before any erase: three passes at most
        for (let pass = 0; pass < 3 && !ok; pass++) {
            let crc = 0;
            for (let i = 0; i < fl.len; i += 512) {
                const n = Math.min(512, fl.len - i), b = await rd(fl.off + i, n);
                if (!b) return -7;
                crc = crc16(b, crc);
                if (i >= APP_LO && i < APP_HI) {
                    const k = (i - APP_LO) >> 12, c0 = i & 0xFFF ? sc[k] : 0;
                    sc[k] = (crc16(b, c0 & 0xFFFF) | (crc16a(b, c0 >>> 16) << 16)) >>> 0;
                }
            }
            ok = crc === fl.crc;
        }
        if (!ok) return -20;                              // LDR_EBADPKG: refused, nothing erased
        const sec = new Uint8Array(0x1000);
        for (let s = APP_LO, k = 0; s < APP_HI; s += 0x1000, k++) {
            for (let i = 0; ; i++) {                      // as checked in 2b, else read it again
                for (let o = 0; o < 0x1000; o += 512) {
                    const b = await rd(fl.off + s + o, 512);
                    if (!b) return -7;
                    sec.set(b, o);
                }
                if (((crc16(sec) | (crc16a(sec) << 16)) >>> 0) === sc[k]) break;
                if (i === 2) return -21;
            }
            if (k === this.o.cutAtSector) { this.unplug(); return -99; }   // power gone before this sector
            if (eq(sec, this.flash.subarray(s, s + 0x1000))) this.stats.sectorsSkipped++;
            else { this.flash.set(sec, s); this.stats.sectorsWritten++; }  // erase, program, verify
            this.progress = (k + 1) / nsec;
        }
        for (let i = 0; i < 4; i++) {                     // the host confirms, then the record goes
            const b = await rd(0xF0000000, 8);
            if (b && eq(b, ascii("success\0"))) return 0;
            if (!this.alive(tok)) return -13;
        }
        return -13;
    }

    // ---- what the test checks
    expectedApp(raw) {                                    // [0x4000, 0x93000) of the package's flash.bin
        const img = logicalOf(raw), u = ufwParse(img.subarray(0, 0x400));
        return img.subarray(u.fl.off + APP_LO, u.fl.off + APP_HI);
    }
    appArea() { return this.flash.subarray(APP_LO, APP_HI); }
    untouched() {                                         // the flash head (SPL) and the user's data: never written
        const a = (x, y) => eq(this.flash.subarray(x, y), this.flash0.subarray(x, y));
        return a(0, APP_LO) && a(APP_HI, 0x100000);
    }
    appChanged() { return !eq(this.appArea(), this.flash0.subarray(APP_LO, APP_HI)); }
}

return { Fm1, Mute, MockMidi, logicalOf, productOf, ufwParse, pack7, unpack7, crc16, msgFrame, APP_LO, APP_HI, wait };
});

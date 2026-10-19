// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Omnipotard
/* A simulated FM-1 running KAWA on a simulated Web MIDI, for install/websamples.js (source/tests/websamples_test.js, in
 * Node, and websamples_ui_test.js, inside a real Chromium whose headless build has no MIDI device).
 *
 * MockFm1 is kawa_sysex.c, port for port, written from the C code and not from the page under test: usb.c's SysEx intake
 * (status bytes abort a frame, 640 bytes at most), app_sysex (the decoder holds 300 bytes), kw_handle (INFO, BEGIN,
 * WRITE, END, ERASE, LIST with their codes), the session and its 10 s timeout, a NOR flash (programming only clears
 * bits; a program over unerased bytes is recorded as a violation) with the four slots of 36 KiB, the header written
 * last after the CRC of what reached the flash. It records every refusal it gives (`refusals`) and everything a correct
 * client never causes (`violations`). Faults (option `faults`): dropReply / dropRequest / dupReply [{cmd, nth}],
 * muteAfter / disconnectAfter {cmd, count}, corrupt {cmd, nth, index}, failProg / failErase (the n-th call).
 * MockMidi has the shape of Web MIDI: MIDIAccess {inputs, outputs, onstatechange}, ports {id, name, state, connection,
 * open(), close(), onmidimessage, addEventListener, send()}; input events only while the port is open; send() on an
 * unplugged port throws InvalidStateError; a SysEx without the permission throws InvalidAccessError.
 * It is not the chip, nor USB, nor Chrome's Web MIDI: only a real FM-1 proves those. */
(function (root, factory) {
    if (typeof module === "object" && module.exports) module.exports = factory();
    else root.KawaSysexMock = factory();
})(typeof self !== "undefined" ? self : this, function () {
"use strict";

const defer = fn => (typeof setImmediate === "function" ? setImmediate(fn) : setTimeout(fn, 0));
const sleep = ms => new Promise(r => setTimeout(r, ms));

/* kawa_sysex.c, port for port (and usb.c's SysEx intake). Constants as kawa_core.c. */
const NUSR = 4, USR_SIZE = 0x9000, USR_DATA = 0x100, USR_CAP = USR_SIZE - USR_DATA, USR_MAGIC = 0x504D534B;
const OK = 0, EARGS = 1, EBUSY = 2, EFLASH = 3, ECRC = 4, ESTATE = 5;

function cPack7(inp, n) {                              // kw_pack7
    const out = [];
    let acc = 0, nb = 0;
    for (let i = 0; i < n; i++) {
        acc |= inp[i] << nb; nb += 8;
        while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; }
    }
    if (nb) out.push(acc & 0x7F);
    return out;
}
function cUnpack7(inp, max) {                          // kw_unpack7: the count may exceed max (nothing stored past it)
    const out = new Uint8Array(max);
    let acc = 0, nb = 0, o = 0;
    for (const b of inp) {
        acc |= (b & 0x7F) << nb; nb += 7;
        if (nb >= 8) { if (o < max) out[o] = acc & 0xFF; o++; acc >>>= 8; nb -= 8; }
    }
    return { buf: out, count: o };
}
function cCrc(u8) {                                    // zlib's CRC-32, bit by bit (the page's is table driven)
    let c = 0xFFFFFFFF;
    for (const b of u8) { c ^= b; for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xEDB88320 : c >>> 1; }
    return (~c) >>> 0;
}

class MockFm1 {
    constructor(o = {}) {
        this.o = Object.assign({ version: "0.4", playing: false, nusr: NUSR, capReported: USR_CAP, faults: {}, silentToKawa: false,
                                 infoMagic: "KAWA", chunk: 0, noise: false }, o);
        this.flash = new Uint8Array(NUSR * USR_SIZE).fill(0xFF);       // [STORE .. ] the four slots, NOR
        this.ms = 0;                                    // fm1_ms (tick() moves it)
        this.slot = 0xFF; this.next = 0; this.kwMs = 0; this.lastRc = 0;
        this.audioHold = 0; this.smpKill = 0; this.sqPlay = this.o.playing ? 1 : 0;
        this.refusals = [];                             // [{cmd, rc, why}]: every non-zero code it gave
        this.violations = [];                           // what a correct client never causes
        this.toasts = 0;
        this.seen = [];                                 // every frame that reached the intake, by command
        this.counts = {};                               // frames arrived, per command (the faults' "nth")
        this.processed = {};
        this.sxOn = 0; this.sxPos = 0; this.sxCollect = 0; this.sxReady = 0; this.sxFrame = [];
        this.eraseCalls = 0; this.progCalls = 0; this.erased = [];
        this.dead = false;                              // mute: nothing is processed any more
        this.unplugged = false;
        this.out = null;                                // set by MockMidi: (Uint8Array) -> void
        this.onUnplug = null;
        this.boot();
    }
    // ---- the flash (kw_erase, kw_prog, kw_read, kw_xip; host stand-in of kawa_sysex.c, with the faults)
    kwErase(off) {
        this.eraseCalls++;
        if (this.o.faults.failErase && this.eraseCalls === this.o.faults.failErase) return -1;
        const b = off & ~0xFFF;
        if (off !== b) this.violations.push("erase not on a sector boundary");
        this.erased.push(b);
        this.flash.fill(0xFF, b, b + 0x1000);
        return 0;
    }
    kwProg(off, p) {
        this.progCalls++;
        if (this.o.faults.failProg && this.progCalls === this.o.faults.failProg) return -1;
        for (let i = 0; i < p.length; i++) {
            const v = this.flash[off + i] & p[i];       // NOR: programming only clears bits
            if (v !== p[i]) this.violations.push(`programmed over unerased flash at ${off + i}`);
            this.flash[off + i] = v;
        }
        return 0;
    }
    kwRead(off, n) { return this.flash.slice(off, off + n); }
    slotAt(k) { return k * USR_SIZE; }
    hdrOf(b) {
        const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
        return { magic: dv.getUint32(0, true), len: dv.getUint32(4, true), rate: dv.getUint32(8, true), name: b.slice(12, 24),
                 crc: dv.getUint32(24, true), raw: b };
    }
    usrCheck(k, h) {                                    // a slot's header and its sound agree
        if (h.magic !== USR_MAGIC || h.len < 64 || h.len > USR_CAP || (h.rate !== 22050 && h.rate !== 11025)) return false;
        return cCrc(this.flash.subarray(this.slotAt(k) + USR_DATA, this.slotAt(k) + USR_DATA + h.len)) === h.crc;
    }
    boot() {                                            // usr_boot: the slots that hold a sound
        this.usrData = new Array(NUSR).fill(null);
        this.usrHdr = new Array(NUSR).fill(null);
        for (let k = 0; k < NUSR; k++) {
            const h = this.hdrOf(this.kwRead(this.slotAt(k), 36));
            if (!this.usrCheck(k, h)) continue;
            h.name[11] = 0;
            this.usrHdr[k] = h;
            this.usrData[k] = true;
        }
        this.slot = 0xFF; this.audioHold = 0;
    }
    slotSound(k) {                                      // what the audio interrupt would read, or null
        return this.usrData[k] ? this.flash.slice(this.slotAt(k) + USR_DATA, this.slotAt(k) + USR_DATA + this.usrHdr[k].len) : null;
    }
    slotName(k) { const n = this.usrHdr[k].name; const z = n.indexOf(0); return String.fromCharCode(...(z < 0 ? n : n.subarray(0, z))); }
    tick(ms) {                                          // time passes: kw_poll
        this.ms += ms;
        if (this.slot !== 0xFF && this.ms - this.kwMs > 10000) this.endSession();
    }
    // ---- replies
    reply(cmd, b, n) {
        const w = [0xF0, 0x7D, 0x4B, 0x57, cmd, ...cPack7(b, n), 0xF7];
        const f = this.o.faults, c = (this.repliesOf = this.repliesOf || {});
        c[cmd] = (c[cmd] || 0) + 1;
        if (this.dropReplyNow) { this.dropReplyNow = false; return; }
        this.emit(Uint8Array.from(w));
        if (f.dupReply && f.dupReply.some(d => d.cmd === cmd && d.nth === this.counts[cmd])) this.emit(Uint8Array.from(w));
    }
    emit(u8) { if (this.out && !this.unplugged) this.out(u8); }
    endSession() { this.slot = 0xFF; this.audioHold = 0; }
    takeSlot(k) {
        if (k >= NUSR) return EARGS;
        if (this.sqPlay) return EBUSY;
        this.usrData[k] = null; this.smpKill = 1; this.audioHold = 1;
        return OK;
    }
    // ---- kw_handle
    handle(cmd, a, n) {
        const r = new Uint8Array(100);
        const k = n ? a[0] : 0xFF;
        r[0] = OK; r[1] = k & 0xFF;
        this.kwMs = this.ms;
        const rc = (c, why) => { r[0] = c; this.refusals.push({ cmd, rc: c, why }); };
        switch (cmd) {
        case 1: {
            const V = (this.o.version + "    ").slice(0, 4);
            const M = this.o.infoMagic;
            for (let i = 0; i < 4; i++) r[1 + i] = M.charCodeAt(i);
            for (let i = 0; i < 4; i++) r[5 + i] = V.charCodeAt(i);
            r[9] = this.o.nusr; r[10] = this.o.capReported & 255; r[11] = (this.o.capReported >> 8) & 255; r[12] = (this.o.capReported >> 16) & 255;
            this.reply(cmd, r, 13);
            return;
        }
        case 2: {
            if (n !== 1) { rc(EARGS, "BEGIN length"); break; }
            if (this.slot !== 0xFF && this.slot !== k) this.endSession();
            const t = this.takeSlot(k);
            if (t) { rc(t, "take_slot"); break; }
            if (this.kwErase(this.slotAt(k))) { rc(EFLASH, "erase header"); this.endSession(); break; }   // the header first: empty
            this.slot = k; this.next = 0;
            break;
        }
        case 3: {
            const off = n >= 4 ? (a[1] | a[2] << 8 | a[3] << 16) >>> 0 : 0xFFFFFFFF, len = (n - 4) >>> 0;
            r[2] = n >= 4 ? a[1] : 0; r[3] = n >= 4 ? a[2] : 0; r[4] = n >= 4 ? a[3] : 0;
            if (n < 5 || len > 256 || (off & 0xFF) || off + len > USR_CAP) { rc(EARGS, "WRITE arguments"); this.reply(cmd, r, 5); return; }
            if (k !== this.slot) { rc(ESTATE, "WRITE without session"); this.reply(cmd, r, 5); return; }
            if (off + 256 === this.next) { r[0] = this.lastRc; this.reply(cmd, r, 5); return; }   // the same frame again
            if (off !== this.next) { rc(ESTATE, "WRITE out of order"); this.reply(cmd, r, 5); return; }
            const at = this.slotAt(k) + USR_DATA + off;
            if (((at & 0xFFF) === 0 || off === 0) && (at & ~0xFFF) !== this.slotAt(k) && this.kwErase(at & ~0xFFF)) rc(EFLASH, "erase sector");
            else if (this.kwProg(at, a.subarray(4, 4 + len))) rc(EFLASH, "program");
            if (r[0] === OK) this.next = off + 256;
            this.lastRc = r[0];
            this.reply(cmd, r, 5);
            return;
        }
        case 4: {
            if (n !== 1 + 36) { rc(EARGS, "END length"); break; }
            if (k !== this.slot) { rc(ESTATE, "END without session"); break; }
            const h = this.hdrOf(Uint8Array.from(a.subarray(1, 37)));
            h.name[11] = 0; h.raw.set(h.name, 12);
            if (h.len > this.next || !this.usrCheck(k, h)) { rc(ECRC, "END header/CRC"); this.endSession(); break; }
            const back = this.kwProg(this.slotAt(k), h.raw);
            const rb = this.kwRead(this.slotAt(k), 36);
            if (back || rb.length !== h.raw.length || rb.some((x, i) => x !== h.raw[i])) { rc(EFLASH, "header"); this.endSession(); break; }
            this.usrHdr[k] = this.hdrOf(rb); this.usrData[k] = true;
            this.endSession();
            this.toasts++;
            break;
        }
        case 5: {
            if (n !== 1) { rc(EARGS, "ERASE length"); break; }
            const t = this.takeSlot(k);
            if (t) { rc(t, "take_slot"); break; }
            for (let i = 0; i < USR_SIZE; i += 0x1000) if (this.kwErase(this.slotAt(k) + i)) { rc(EFLASH, "erase"); break; }
            this.endSession();
            break;
        }
        case 6:
            for (let i = 0; i < NUSR; i++) {
                const q = 1 + i * 18, used = this.usrData[i] ? 1 : 0, h = this.usrHdr[i];
                r[q] = used;
                r[q + 1] = used ? h.rate & 255 : 0; r[q + 2] = used ? (h.rate >> 8) & 255 : 0;
                r[q + 3] = used ? h.len & 255 : 0; r[q + 4] = used ? (h.len >> 8) & 255 : 0; r[q + 5] = used ? (h.len >> 16) & 255 : 0;
                for (let j = 0; j < 12; j++) r[q + 6 + j] = used ? h.name[j] : 0;
            }
            this.reply(cmd, r, 1 + NUSR * 18);
            return;
        default:
            rc(EARGS, "unknown command " + cmd);
            break;
        }
        this.reply(cmd, r, 2);
    }
    // ---- app_sysex: the bytes between F0 and F7
    appSysex(p) {
        if (p.length < 4 || p[0] !== 0x7D || p[1] !== 0x4B || p[2] !== 0x57) return 0;
        const d = cUnpack7(p.slice(4), 300);
        if (d.count <= 300) this.handle(p[3], d.buf, d.count);
        return 1;
    }
    // ---- usb.c sysex_byte + the frame buffer (640 bytes), then the main loop takes the frame
    byte(b) {
        if (b >= 0xF8) return;                          // realtime may occur anywhere in SysEx
        if ((b & 0x80) && b !== 0xF0 && b !== 0xF7) { this.sxOn = 0; this.sxCollect = 0; return; }   // another status aborts it
        if (b === 0xF0) { this.sxOn = 1; this.sxCollect = !this.sxReady; this.sxPos = 0; this.sxFrame = []; }
        if (!this.sxOn) return;
        if (this.sxCollect && b !== 0xF0 && b !== 0xF7) {
            if (this.sxPos < 640) { this.sxFrame.push(b); this.sxPos++; } else this.sxCollect = 0;   // too long: not ours
        }
        if (b === 0xF7) {
            if (this.sxCollect && this.sxPos) this.sxReady = 1;
            this.sxCollect = 0; this.sxOn = 0;
        }
    }
    /* the host sends one USB-MIDI message (Web MIDI: a whole SysEx) */
    receive(u8) {
        if (this.dead || this.unplugged) return;
        for (const b of u8) this.byte(b);
        if (!this.sxReady) return;
        const f = this.sxFrame;
        this.sxReady = 0;
        const cmd = f.length > 3 ? f[3] : 0;
        this.counts[cmd] = (this.counts[cmd] || 0) + 1;
        const nth = this.counts[cmd], fl = this.o.faults;
        if (this.o.silentToKawa && f[0] === 0x7D) return;                      // rescue mode: this firmware does not know KAWA's frames
        this.seen.push({ cmd, frame: Uint8Array.from([0xF0, ...f, 0xF7]) });
        if (fl.dropRequest && fl.dropRequest.some(d => d.cmd === cmd && d.nth === nth)) return;
        if (fl.corrupt && fl.corrupt.cmd === cmd && fl.corrupt.nth === nth) f[fl.corrupt.index] ^= 1;
        this.dropReplyNow = !!(fl.dropReply && fl.dropReply.some(d => d.cmd === cmd && d.nth === nth));
        this.appSysex(f);
        this.dropReplyNow = false;
        this.processed[cmd] = (this.processed[cmd] || 0) + 1;
        if (fl.muteAfter && fl.muteAfter.cmd === cmd && this.processed[cmd] >= fl.muteAfter.count) this.dead = true;
        if (fl.disconnectAfter && fl.disconnectAfter.cmd === cmd && this.processed[cmd] >= fl.disconnectAfter.count) {
            this.unplugged = true;
            if (this.onUnplug) this.onUnplug();
        }
    }
    framesOf(cmd) { return this.seen.filter(s => s.cmd === cmd); }
}

// ---- MockMidi
/* Web MIDI's shape: MIDIAccess {inputs, outputs, onstatechange}, ports {id, name, state, connection, open(), close(),
 * onmidimessage, addEventListener, send()}. Unplugging sets state "disconnected" and drops the ports from the maps;
 * send() on a disconnected port throws InvalidStateError; input events only come while the port is open. */
class MockPort {
    constructor(midi, id, name, type) {
        Object.assign(this, { midi, id, name, type, state: "connected", connection: "closed", manufacturer: "", onmidimessage: null });
        this.listeners = [];
    }
    async open() { if (this.state === "disconnected") throw new DOMException("gone", "InvalidStateError"); await sleep(0); this.connection = "open"; return this; }
    async close() { this.connection = "closed"; return this; }
    addEventListener(t, fn) { if (t === "midimessage") this.listeners.push(fn); }
    removeEventListener(t, fn) { this.listeners = this.listeners.filter(f => f !== fn); }
    deliver(data) {
        if (this.connection !== "open") return;
        const ev = { data, target: this };
        if (this.onmidimessage) this.onmidimessage(ev);
        for (const f of this.listeners) f(ev);
    }
}
class MockMidi {
    constructor(o = {}) {
        this.o = Object.assign({ reject: false, hang: false, latency: 0 }, o);
        this.inputs = new Map();
        this.outputs = new Map();
        this.onstatechange = null;
        this.devices = [];
    }
    request(opts) {
        if (this.o.reject) return Promise.reject(new DOMException("denied", "SecurityError"));
        this.sysexAllowed = !!(opts && opts.sysex);
        return Promise.resolve(this);
    }
    add(dev, name = "FM-1", extra = {}) {
        const n = this.devices.length + 1;
        const inp = new MockPort(this, "in" + n, name, "input"), out = new MockPort(this, "out" + n, name, "output");
        if (this.o.hang) { inp.open = out.open = () => new Promise(() => {}); }
        out.send = data => {
            if (out.state === "disconnected") throw new DOMException("port disconnected", "InvalidStateError");
            if (!(data instanceof Uint8Array)) throw new TypeError("send() wants a Uint8Array");
            if (data[0] === 0xF0 && !this.sysexAllowed) throw new DOMException("no sysex permission", "InvalidAccessError");
            const copy = Uint8Array.from(data);
            const run = () => dev.receive(copy);
            if (this.o.latency) setTimeout(run, this.o.latency); else defer(run);
        };
        dev.out = reply => this.sendToHost(dev, inp, reply);
        dev.onUnplug = () => this.unplug(dev);
        dev.ports = { inp, out };
        this.inputs.set(inp.id, inp);
        this.outputs.set(out.id, out);
        this.devices.push(dev);
        return Object.assign(dev, extra);
    }
    sendToHost(dev, inp, reply) {
        const noise = dev.o.noise, chunk = dev.o.chunk;
        const pieces = [];
        if (noise) pieces.push(Uint8Array.of(0xF8), Uint8Array.of(0xFE));
        if (chunk) for (let i = 0; i < reply.length; i += chunk) pieces.push(reply.subarray(i, Math.min(reply.length, i + chunk)));
        else pieces.push(reply);
        if (noise) pieces.splice(Math.min(2, pieces.length - 1), 0, Uint8Array.of(0xF8));   // a clock byte between the pieces
        pieces.forEach(p => defer(() => inp.deliver(p)));
    }
    unplug(dev) {
        for (const p of [dev.ports.inp, dev.ports.out]) {
            p.state = "disconnected"; p.connection = "pending";
            (p.type === "input" ? this.inputs : this.outputs).delete(p.id);
            if (this.onstatechange) this.onstatechange({ port: p });
        }
    }
}

return { MockFm1, MockMidi, NUSR, USR_SIZE, USR_DATA, USR_CAP, USR_MAGIC, OK, EARGS, EBUSY, EFLASH, ECRC, ESTATE, cPack7, cUnpack7, cCrc };
});

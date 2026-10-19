// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments: the protocol, the checks and the
// timings are those of install/fm1_install.py (Felucca's installer); the web port: Omnipotard
/* KAWA's web installer: install/fm1_install.py in JavaScript, over Web MIDI (with SysEx).
 *
 * The same update: step 1, the running firmware reads parts of the package and starts the update loader; step 2,
 * the loader reads the whole package and writes it. The device asks (SysEx read requests on the logical image),
 * we answer; then the FM-1 restarts and the installed identity is checked. The same checks, in the same order,
 * with the same delays and the same messages as the Python tool:
 *   - the whole package is checked (CRC16 of every section) BEFORE any MIDI port is opened;
 *   - the model, and the FM-1 being in another firmware's update mode, are refused before anything is written;
 *   - a device found in update mode (an install cut off) only gets the write finished;
 *   - after the restart the identity must be the package's.
 * Nothing here touches the page: index.html passes callbacks (say, step, confirm, busy). It is a UMD module so
 * the Node test (source/tests/webinstall_test.js) runs the very same code against a simulated FM-1.
 *
 *   KawaInstall.runInstall({nav: navigator, bytes, name, confirm, say, step, busy, port})
 *   KawaInstall.probe({nav: navigator, port})          what the connected FM-1 runs (fm1_install.py --info) */
(function (root, factory) {
    if (typeof module === "object" && module.exports) module.exports = factory();
    else root.KawaInstall = factory();
})(typeof self !== "undefined" ? self : this, function () {
"use strict";

const HS_QUERY = Uint8Array.of(0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7);
const UPGRADE = Uint8Array.of(0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7);
const FINISH_CHECK = 0xE0000000, FINISH_WRITE = 0xF0000000;
const MAXDATA = 512;
const BLOCKS = 20, BLK = 0x30, KEEP = 0x2F;
const LOADER_MARK = "FELUCCA-LOADER-1";
const PORT_RE = /fm-1|felucca|ota|composite|sinco|usb-midi/i;   // never probe other gear

// milliseconds, the Python tool's seconds x 1000 (the tests shorten them); the last two are the web's own
const DELAY = { open: 300, start: 2000, reply: 10, loader: 3000, reboot: 3000, retry: 1000,
                poll: 1000, hs: 1000, idle_check: 8000, idle_write: 180000,
                wait_loader: 30000, wait_reboot: 40000,
                portopen: 5000,       // a Web MIDI open() that never answers (port taken by another program)
                refresh: 10000 };     // waiting for the FM-1 to come back: no plug event for 10 s, ask the browser for its ports again

class InstallError extends Error {
    constructor(code, msg) {
        super(msg);
        this.name = "InstallError";
        this.code = code;     // usage badpkg notfound model lost stopped badreq noloader noreturn mismatch foreign nomidi download
    }
}

// ----------------------------------------------------------------- time ---

const now = () => (typeof performance !== "undefined" ? performance.now() : Date.now());

let yieldQ = [], yieldCh = null;
function yieldNow() {                    // one turn of the event loop without a timer (timers are 4 ms+ in a browser)
    if (typeof setImmediate === "function") return new Promise(r => setImmediate(r));
    if (typeof MessageChannel === "function") {
        if (!yieldCh) {
            yieldCh = new MessageChannel();
            yieldCh.port1.onmessage = () => { const q = yieldQ; yieldQ = []; q.forEach(f => f()); };
        }
        return new Promise(r => { yieldQ.push(r); yieldCh.port2.postMessage(0); });
    }
    return new Promise(r => setTimeout(r, 0));
}
const sleep = ms => (ms > 0 ? new Promise(r => setTimeout(r, ms)) : yieldNow());

function within(p, ms, what) {           // p, or an error when it takes more than ms
    let t;
    return Promise.race([p, new Promise((_, rej) => { t = setTimeout(() => rej(new Error(what)), ms); })])
        .finally(() => clearTimeout(t));
}

// ------------------------------------------------------------ wire format ---

function pack7(data) {                   // 8 bits -> 7, LSB first
    const out = new Uint8Array(Math.ceil(data.length * 8 / 7));
    let acc = 0, nb = 0, o = 0;
    for (let i = 0; i < data.length; i++) {
        acc |= data[i] << nb;
        nb += 8;
        while (nb >= 7) { out[o++] = acc & 0x7F; acc >>= 7; nb -= 7; }
    }
    if (nb) out[o++] = acc & 0x7F;
    return out;
}

function unpack7(s) {
    const out = new Uint8Array(Math.floor(s.length * 7 / 8));
    let acc = 0, nb = 0, o = 0;
    for (let i = 0; i < s.length; i++) {
        acc |= (s[i] & 0x7F) << nb;
        nb += 7;
        if (nb >= 8) { out[o++] = acc & 0xFF; acc >>= 8; nb -= 8; }
    }
    return out;
}

const le16 = (b, o) => b[o] | b[o + 1] << 8;
const le24 = (b, o) => b[o] | b[o + 1] << 8 | b[o + 2] << 16;
const le32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const sum = (b, from, to) => { let s = 0; for (let i = from; i < to; i++) s += b[i]; return s; };

function response(addr, data, fl = 0) {  // the host's answer to a read request (cmd 0x30)
    const n = data.length;
    const body = new Uint8Array(14 + n + 1);
    body.set([0x00, 0x59, 0x30, (n + 8) & 0xFF, ((n + 8) >> 8) & 0xFF, 0, fl,
              addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF,
              n & 0xFF, (n >> 8) & 0xFF, 0]);
    body.set(data, 14);
    body[14 + n] = ~sum(body, 6, 14 + n) & 0xFF;
    const p = pack7(body), out = new Uint8Array(p.length + 2);
    out[0] = 0xF0;
    out.set(p, 1);
    out[out.length - 1] = 0xF7;
    return out;
}

function parseRequest(pkt) {             // -> {fl, addr, len} or null
    if (pkt.length < 2 || pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
    const u = unpack7(pkt.subarray(1, pkt.length - 1));
    if (u.length !== 15 || u[0] !== 0x00 || u[1] !== 0x59 || u[2] !== 0x30 || (~sum(u, 6, 14) & 0xFF) !== u[14])
        return null;
    return { fl: u[6], addr: le32(u, 7), len: le24(u, 11) };
}

class Identity {
    constructor(text, model, version) { this.text = text; this.model = model; this.version = version; }
    get loader() { return this.model.toLowerCase().startsWith("ota-"); }
}

function parseIdentity(pkt) {
    if (pkt.length < 2 || pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
    const d = unpack7(pkt.subarray(1, pkt.length - 1));
    if (d.length !== 34 || d[0] !== 0x00 || d[1] !== 0x59 || d[2] !== 0x11) return null;
    let e = 33;
    while (e > 6 && d[e - 1] === 0) e--;                         // rstrip NULs; latin-1
    let txt = "";
    for (let i = 6; i < e; i++) txt += String.fromCharCode(d[i]);
    const m = /^([^_]+)_(\d+)$/.exec(txt);
    return m ? new Identity(txt, m[1], parseInt(m[2], 10)) : null;
}

// --------------------------------------------------------------- package ---

function pyRepr(s) {                     // Python's repr() of a str, for the same messages
    const q = s.includes("'") && !s.includes('"') ? '"' : "'";
    let o = "";
    for (const ch of s) {
        const c = ch.charCodeAt(0);
        if (ch === "\\" || ch === q) o += "\\" + ch;
        else if (ch === "\n") o += "\\n";
        else if (ch === "\r") o += "\\r";
        else if (ch === "\t") o += "\\t";
        else if (c < 0x20 || (c >= 0x7F && c <= 0xA0) || c === 0xAD) o += "\\x" + c.toString(16).padStart(2, "0");
        else o += ch;
    }
    return q + o + q;
}

function productOf(raw) {                // the package identity ("FM-1_906"): one marker byte after each of the first 20 blocks
    if (raw.length < BLOCKS * BLK) throw new InstallError("badpkg", "not an FM-1 package (too short)");
    let s = "";
    for (let i = 0; i < BLOCKS; i++) {
        const m = raw[i * BLK + KEEP];
        if (m !== 0x7D) s += String.fromCharCode((m - i - 1) & 0xFF);
    }
    return s;
}

function logicalImage(raw) {             // the image the device reads during an update: the package without the 20 marker bytes
    const out = new Uint8Array(Math.max(raw.length - BLOCKS, 0));
    let o = 0;
    for (let i = 0; i < BLOCKS; i++) {
        const part = raw.subarray(i * BLK, i * BLK + KEEP);
        out.set(part, o);
        o += part.length;
    }
    out.set(raw.subarray(BLOCKS * BLK), o);
    return out;
}

function modelOf(text) {
    const m = text.split("_")[0];
    return m.startsWith("ota-") ? m.slice(4) : m;
}

function hasBytes(raw, str) {
    const first = str.charCodeAt(0);
    for (let i = raw.indexOf(first); i >= 0 && i + str.length <= raw.length; i = raw.indexOf(first, i + 1)) {
        let k = 1;
        while (k < str.length && raw[i + k] === str.charCodeAt(k)) k++;
        if (k === str.length) return true;
    }
    return false;
}

const CRC_T = (() => {                   // CRC-16/XMODEM, as the package and the device use
    const t = new Uint16Array(256);
    for (let i = 0; i < 256; i++) {
        let c = i << 8;
        for (let k = 0; k < 8; k++) c = c & 0x8000 ? ((c << 1) ^ 0x1021) & 0xFFFF : (c << 1) & 0xFFFF;
        t[i] = c;
    }
    return t;
})();

function crc16(data, crc = 0) {
    for (let i = 0; i < data.length; i++) crc = ((crc << 8) & 0xFFFF) ^ CRC_T[((crc >> 8) ^ data[i]) & 0xFF];
    return crc;
}

function dec(b) {                        // JieLi ENC stream cipher (its own inverse)
    const out = Uint8Array.from(b);
    let key = 0xFFFF;
    for (let i = 0; i < out.length; i++) {
        out[i] ^= key & 0xFF;
        key = ((key << 1) ^ (key & 0x8000 ? 0x1021 : 0)) & 0xFFFF;
    }
    return out;
}

function packageDamage(image) {          // what is wrong with the update image (header, file list, each file's CRC), or null;
    if (image.length < 0x40) return "too short";   // the device only checks the loader before it rewrites the firmware
    const hdr = dec(image.subarray(0, 0x40));
    if (crc16(hdr.subarray(2, 0x40)) !== le16(hdr, 0)) return "header CRC";
    const count = le16(hdr, 8);
    if (count < 1 || count > 8 || image.length < 0x40 + 0x50 * count) return "file list";
    const lst = image.subarray(0x40, 0x40 + 0x50 * count);
    if (crc16(lst) !== le16(hdr, 2)) return "file list CRC";
    for (let i = 0; i < count; i++) {
        const e = dec(lst.subarray(i * 0x50, (i + 1) * 0x50));
        const dcrc = le16(e, 4), off = le32(e, 8), size = le32(e, 12);
        let name = "";
        for (let k = 0x40; k < 0x50 && e[k] !== 0; k++) name += e[k] < 0x80 ? String.fromCharCode(e[k]) : "�";
        if (off + size > image.length) return `${name}: cut short`;
        if (crc16(image.subarray(off, off + size)) !== dcrc) return `${name}: CRC`;
    }
    return null;
}

function loadPackage(raw, name, force) {  // -> {product, image}; nothing is touched for a bad package
    raw = raw instanceof Uint8Array ? raw : new Uint8Array(raw);
    const product = productOf(raw);
    if (!/^[^_]+_\d+$/.test(product))
        throw new InstallError("badpkg", `${name}: not an FM-1 package (identity ${pyRepr(product)})`);
    if (!hasBytes(raw, LOADER_MARK) && !force)
        throw new InstallError("badpkg", `${name}: no Felucca update loader in this package; ` +
                               "only Felucca's own packages are installed");
    const image = logicalImage(raw);
    const bad = packageDamage(image);
    if (bad) throw new InstallError("badpkg", `${name}: damaged package (${bad}); nothing was written. Download it again.`);
    return { product, image };
}

// ------------------------------------------------------------------ MIDI ---

function ports(backend, names) {         // the port names, or none when the MIDI system cannot be read (backend.problem says why)
    try {
        backend.problem = null;
        return Array.from(new Set(names()));
    } catch (e) {
        backend.problem = String((e && e.message) || "").trim() || (e && e.name) || "error";
        return [];
    }
}

/* one MIDI in/out pair with a SysEx queue (Web MIDI hands over whole messages, F0 .. F7; a driver that
 * cuts a long one in pieces is put back together) */
class WebMidiLink {
    constructor(backend, inp, out) {
        this.backend = backend;
        this.inp = inp;
        this.out = out;
        this.name = inp.name;
        this.q = [];
        this.waiter = null;
        this.buf = null;
    }

    static async open(backend, inp, out) {
        const link = new WebMidiLink(backend, inp, out);
        try {
            inp.onmidimessage = e => link._rx(e.data);
            await within(inp.open(), DELAY.portopen, `cannot open MIDI input ${inp.name}`);
            await within(out.open(), DELAY.portopen, `cannot open MIDI output ${out.name}`);
        } catch (e) {
            await link.close();
            throw e;
        }
        await sleep(DELAY.open);          // CoreMIDI may drop a message sent just after opening
        return link;
    }

    _rx(d) {
        if (!d || !d.length) return;
        if (d[0] === 0xF0) this.buf = [];                      // a new SysEx (an unfinished one is dropped)
        else if (!this.buf || d[0] >= 0x80) return;            // not in a SysEx: clock, active sensing, notes
        for (let i = 0; i < d.length; i++) this.buf.push(d[i]);
        if (d[d.length - 1] === 0xF7) {
            const pkt = Uint8Array.from(this.buf);
            this.buf = null;
            if (this.waiter) this.waiter(pkt);
            else this.q.push(pkt);
        }
    }

    get lost() {
        return this.inp.state === "disconnected" || !this.backend.inputNames().includes(this.name);
    }

    send(pkt) {
        try {
            this.out.send(pkt);
            return true;
        } catch (e) {
            return false;
        }
    }

    read(ms) {                            // the next SysEx, or null after ms
        if (this.q.length) return Promise.resolve(this.q.shift());
        return new Promise(res => {
            const t = setTimeout(() => { this.waiter = null; res(null); }, Math.max(ms, 0));
            this.waiter = p => { clearTimeout(t); this.waiter = null; res(p); };
        });
    }

    drain() { this.q.length = 0; }

    async close() {
        this.inp.onmidimessage = null;
        for (const p of [this.inp, this.out]) {
            try { await within(Promise.resolve(p.close()), DELAY.portopen, "close"); } catch (e) { /* gone already */ }
        }
    }
}

class WebMidiBackend {
    constructor(access, nav) {
        this.nav = nav;
        this.problem = null;
        this._set(access);
    }

    _set(access) {
        this.access = access;
        this.asked = now();                                    // (a plug event counts as news from the browser too)
        access.onstatechange = () => { this.asked = now(); };
    }

    _list(map) {
        return Array.from(map.values()).filter(p => p.state !== "disconnected");
    }

    inputNames() { return ports(this, () => this._list(this.access.inputs).map(p => p.name || p.id)); }
    outputNames() { return ports(this, () => this._list(this.access.outputs).map(p => p.name || p.id)); }

    // Chrome keeps the port maps up to date and tells (statechange). If it has said nothing for DELAY.refresh since a wait
    // began, ask for the access again: a new MIDIAccess has the current list (the permission is already granted, no prompt)
    async refresh(since) {
        if (now() - Math.max(this.asked, since) < DELAY.refresh) return;
        this.asked = now();
        try { this._set(await this.nav.requestMIDIAccess({ sysex: true })); } catch (e) { /* keep the old one */ }
    }

    async open(inName, outName) {         // (by name, as the Python tool: duplicates were merged by ports())
        const inp = this._list(this.access.inputs).find(p => (p.name || p.id) === inName);
        const out = this._list(this.access.outputs).find(p => (p.name || p.id) === outName);
        if (!inp || !out) throw new Error("port gone");
        return WebMidiLink.open(this, inp, out);
    }
}

async function connect(nav) {            // -> WebMidiBackend; the browser's MIDI permission (with SysEx) is asked here
    if (!nav || typeof nav.requestMIDIAccess !== "function") {
        const insecure = typeof isSecureContext !== "undefined" && isSecureContext === false;   // Web MIDI needs https
        throw new InstallError("nomidi", "this browser has no Web MIDI with SysEx, which the installer needs to talk to the FM-1. " +
                               "Use Chrome, Edge or Opera on a computer" + (insecure ? " (and open the page over https)" : "") +
                               ", or the INSTALL-Windows.bat / INSTALL-Mac.command / INSTALL-Linux.sh launcher of the repository");
    }
    let access;
    try {
        access = await nav.requestMIDIAccess({ sysex: true });
    } catch (e) {
        throw new InstallError("nomidi", "the browser refused MIDI access with SysEx" +
                               (e && e.name ? ` (${e.name})` : "") + ": allow it for this page (the permission prompt, or the " +
                               "padlock next to the address), then try again");
    }
    return new WebMidiBackend(access, nav);
}

function pairOutput(name, outs) {
    if (outs.includes(name)) return name;
    const strip = s => s.replace(/\s+\d+$/, "");     // Windows numbers each port
    const base = strip(name);
    const same = outs.filter(o => strip(o) === base);
    if (same.length === 1) return same[0];
    return outs.length === 1 ? outs[0] : null;
}

// --------------------------------------------------------------- updater ---

class Device {
    constructor(link, ident, name) { this.link = link; this.id = ident; this.name = name; }
}

async function handshake(link, tries = 3) {
    link.drain();
    for (let t = 0; t < tries; t++) {
        if (!link.send(HS_QUERY)) return null;
        const end = now() + DELAY.hs;
        let left;
        while ((left = end - now()) > 0) {
            const p = await link.read(left);
            if (p === null) break;
            const ident = parseIdentity(p);
            if (ident) return ident;
        }
    }
    return null;
}

class Updater {
    constructor(backend, port) {
        this.backend = backend;
        this.port = port || null;
    }

    candidates(onlyPort) {
        const names = this.backend.inputNames();
        if (this.port) {
            const mine = names.filter(n => n.toLowerCase().includes(this.port.toLowerCase()));
            if (onlyPort) return mine;
            return mine.concat(names.filter(n => !mine.includes(n) && PORT_RE.test(n)));
        }
        return names.filter(n => PORT_RE.test(n));
    }

    async find(want, onlyPort = true) {   // the first matching port that answers the handshake (and want(identity))
        const outs = this.backend.outputNames();
        for (const name of this.candidates(onlyPort)) {
            const out = pairOutput(name, outs);
            if (!out) continue;
            let link;
            try { link = await this.backend.open(name, out); } catch (e) { continue; }
            const ident = await handshake(link, 2);
            if (ident && (!want || want(ident))) return new Device(link, ident, name);
            await link.close();
        }
        return null;
    }

    async waitFor(want, ms) {             // (the device re-enumerates: its ports go and come back, maybe under another name)
        const start = now(), end = start + ms;
        while (now() < end) {
            if (this.backend.refresh) await this.backend.refresh(start);
            const dev = await this.find(want, false);
            if (dev) return dev;
            await sleep(DELAY.retry);
        }
        return null;
    }

    // answer read requests until the device asks for `finish`; -> {served, finished, lost}
    async serve(link, image, finish, idle, progress) {
        let served = 0, last = now();
        for (;;) {
            const pkt = await link.read(DELAY.poll);
            if (pkt === null) {
                if (link.lost) return { served, finished: false, lost: true };
                if (now() - last > idle) return { served, finished: false, lost: false };
                continue;
            }
            const r = parseRequest(pkt);
            if (!r) continue;
            last = now();
            if (r.addr === FINISH_CHECK || r.addr === FINISH_WRITE) {
                link.send(response(r.addr, Uint8Array.from([115, 117, 99, 99, 101, 115, 115, 0])));   // "success\0"
                if (r.addr === finish) return { served, finished: true, lost: false };
                continue;
            }
            if (r.len > MAXDATA || r.addr + r.len > image.length)
                throw new InstallError("badreq", `the device asked for 0x${r.addr.toString(16)}+${r.len}, outside the package`);
            await sleep(DELAY.reply);
            if (!link.send(response(r.addr, image.subarray(r.addr, r.addr + r.len), r.fl)))
                return { served, finished: false, lost: true };
            served++;
            progress(served, r.addr + r.len, r.len);
        }
    }

    async check(dev, image, step) {       // step 1: the running firmware checks the package and starts the loader
        step("start", dev.id.text);
        dev.link.send(UPGRADE);
        await sleep(DELAY.start);
        let s;
        try { s = await this.serve(dev.link, image, FINISH_CHECK, DELAY.idle_check, (k) => step("check", k)); }
        finally { await dev.link.close(); }       // (also on an error: no port is left open)
        if (!s.finished)
            throw new InstallError(s.lost ? "lost" : "stopped",
                `the FM-1 ${s.lost ? "was disconnected" : "stopped answering"} after ${s.served} requests; nothing was written`);
        step("loader");
        await sleep(DELAY.loader);
        const ota = await this.waitFor(i => i.loader, DELAY.wait_loader);
        if (!ota)
            throw new InstallError("noloader", "the update loader did not appear. Replug the USB cable and " +
                                   "run the install again: the FM-1 stays in update mode until it is done");
        return ota;
    }

    async write(ota, image, step) {       // step 2: the loader reads and writes the whole package, then restarts
        step("write", 0);
        ota.link.send(UPGRADE);
        await sleep(DELAY.start);
        // The loader reads the image twice: all of it (the check, nothing erased), then the app area (the write). The
        // Python tool shows the address (it goes to 99% and back to 1%); the bytes served over twice the image never go back.
        let bytes = 0, s;
        try {
            s = await this.serve(ota.link, image, FINISH_WRITE, DELAY.idle_write,
                                 (k, end, n) => step("write", Math.min(99, Math.floor((bytes += n) * 100 / (2 * image.length)))));
        } finally { await ota.link.close(); }
        if (!s.finished)
            throw new InstallError(s.lost ? "lost" : "stopped",
                `the loader ${s.lost ? "was disconnected" : "stopped answering"} after ${s.served} requests. ` +
                "Replug the USB cable and run the install again to finish");
        step("write", 100);
    }

    async verify(product, step) {
        step("reboot");
        await sleep(DELAY.reboot);
        const back = await this.waitFor(i => !i.loader, DELAY.wait_reboot);
        if (!back) throw new InstallError("noreturn", "the FM-1 did not come back: power-cycle it");
        await back.link.close();
        if (back.id.text !== product)
            throw new InstallError("mismatch", `written, but the FM-1 reports ${back.id.text} (expected ${product})`);
        step("done", back.id.text);
        return back.id.text;
    }

    // the two refusals of install(), alone: the page asks them before it asks the user
    precheck(dev, product) {
        if (modelOf(dev.id.text) !== modelOf(product))
            throw new InstallError("model", `the device is ${dev.id.text}, the package is for ${product}`);
        if (dev.id.loader && !/^ota-FM-1_9\d\d$/i.test(dev.id.text))     // another firmware's loader: its image layout may differ
            throw new InstallError("foreign", `the FM-1 is in the update mode of another firmware (${dev.id.text}): ` +
                                   "finish that update with its own updater (M-UPGRADE), then install again");
    }

    // running firmware -> loader -> new firmware; dev from find(). A device already in update mode only needs the write.
    async install(dev, image, product, step = () => {}) {
        try { this.precheck(dev, product); } catch (e) { await dev.link.close(); throw e; }
        const ota = dev.id.loader ? dev : await this.check(dev, image, step);
        await this.write(ota, image, step);
        return this.verify(product, step);
    }
}

// ------------------------------------------------------------------ flow ---

function notFound(up) {
    const names = up.backend.inputNames();
    const where = up.port ? `no MIDI port matching ${pyRepr(up.port)} answered` : "FM-1 not found";
    const problem = up.backend.problem;
    if (problem) return new InstallError("notfound", `${where}: the MIDI system could not be read\n  ${problem}`);
    return new InstallError("notfound", `${where} (USB data cable? another app using it?)` +
                            `\n  MIDI inputs: ${names.length ? names.join(", ") : "(none)"}`);
}

const STEP_TEXT = {                       // the Python tool's progress lines
    start: a => `starting the update on ${a}`, check: a => `checking the package (${a} reads)`,
    loader: () => "switching to update mode", write: a => `writing ${String(a).padStart(3)}%`,
    reboot: () => "restarting", done: a => `done: the FM-1 runs ${a}`,
};

/* the whole install, as fm1_install.py's run(): o = {nav, bytes, name, port, say(line), step(kind, arg),
 * confirm({product, device, port, resume, name}) -> Promise<bool>, busy(bool)}.
 * Resolves {done, id} or {cancelled}; throws InstallError. Without a confirm that says yes, nothing is written. */
async function runInstall(o) {
    const say = o.say || (() => {}), step = o.step || (() => {});
    const { product, image } = loadPackage(o.bytes, o.name, !!o.force);     // a bad package stops here, no MIDI touched
    say(`package: ${product}  (${o.name})`);
    const backend = await connect(o.nav);
    const up = new Updater(backend, o.port);
    const dev = await up.find();
    if (!dev) throw notFound(up);
    say(`device:  ${dev.id.text}  (${dev.name})` + (dev.id.loader ? "  in update mode: the write will be finished" : ""));
    let yes = false;
    try {
        up.precheck(dev, product);        // refused before the user is asked
        yes = !!(o.confirm && await o.confirm({ product, device: dev.id.text, port: dev.name, resume: dev.id.loader, name: o.name }));
    } catch (e) {
        await dev.link.close();
        throw e;
    }
    if (!yes) {
        await dev.link.close();
        say("cancelled");
        return { cancelled: true };
    }
    if (o.busy) o.busy(true);             // from here on the FM-1 is being updated: the page must not close
    try {
        return { done: true, id: await up.install(dev, image, product, step) };
    } finally {
        if (o.busy) o.busy(false);
    }
}

async function probe(o) {                 // fm1_install.py --info
    const up = new Updater(await connect(o.nav), o.port);
    const dev = await up.find();
    if (!dev) throw notFound(up);
    await dev.link.close();
    const mode = dev.id.loader ? "update loader (update not finished)" : "running";
    return { id: dev.id.text, loader: dev.id.loader, port: dev.name, text: `${dev.id.text}  [${mode}]  port: ${dev.name}` };
}

// ---------------------------------------------------------- the package ---

async function sha256hex(bytes) {         // null where the browser has no SubtleCrypto (not a secure context)
    const c = typeof globalThis !== "undefined" && globalThis.crypto;
    if (!c || !c.subtle) return null;
    const h = new Uint8Array(await c.subtle.digest("SHA-256", bytes));
    return Array.from(h, b => b.toString(16).padStart(2, "0")).join("");
}

function newerVersion(a, b) {             // [0, 10] > [0, 9]; a missing number is 0
    for (let i = 0; i < Math.max(a.length, b.length); i++)
        if ((a[i] || 0) !== (b[i] || 0)) return (a[i] || 0) > (b[i] || 0);
    return false;
}

function newestInSums(text) {             // install/SHA256SUMS -> the newest "kawa-X.Y.fwsc" there (not older/...), or null
    let best = null;
    for (const line of String(text).split(/\r?\n/)) {
        const m = /^([0-9a-f]{64}) [ *]?(kawa-(\d+(?:\.\d+)*)\.fwsc)$/i.exec(line.trim());
        if (!m) continue;
        const v = m[3].split(".").map(Number);
        if (!best || newerVersion(v, best.v)) best = { name: m[2], sha256: m[1].toLowerCase(), v };
    }
    return best && { name: best.name, sha256: best.sha256 };
}

/* the package of the site, by relative URLs: the newest kawa-*.fwsc that install/SHA256SUMS names (no version is written
 * anywhere else), checked against its SHA-256. -> {bytes, name, sha256}. Without the sums (a page opened from a file,
 * offline) there is no way to know which package is the newest: it says so, and the local file picker is the way. */
async function fetchLatest(fetchFn, dir = "install/") {
    let latest = null, why = "";
    try {
        const r = await fetchFn(dir + "SHA256SUMS", { cache: "no-cache" });
        if (r.ok) latest = newestInSums(await r.text());
        else why = `HTTP ${r.status}`;
        if (r.ok && !latest) why = "no kawa-*.fwsc in it";
    } catch (e) {
        why = (e && e.message) || "network error";
    }
    if (!latest)
        throw new InstallError("download", `cannot read ${dir}SHA256SUMS to find the newest package (${why}); ` +
                               "opened from a file, or offline? Choose the .fwsc file from the install folder instead");
    const name = latest.name, url = dir + name;
    let r;
    try {
        r = await fetchFn(url, { cache: "no-cache" });
    } catch (e) {
        throw new InstallError("download", `cannot download ${url} (${(e && e.message) || "network error"}); ` +
                               "choose the .fwsc file from the install folder instead");
    }
    if (!r.ok) throw new InstallError("download", `cannot download ${url} (HTTP ${r.status}); choose the .fwsc file yourself instead`);
    const bytes = new Uint8Array(await r.arrayBuffer());
    const got = await sha256hex(bytes);                   // (null where the browser has no SubtleCrypto: the package's CRCs still apply)
    if (got && got !== latest.sha256)
        throw new InstallError("download", `${name}: the download does not match install/SHA256SUMS; nothing was written. Download it again.`);
    return { bytes, name, sha256: got };
}

return { HS_QUERY, UPGRADE, FINISH_CHECK, FINISH_WRITE, MAXDATA, LOADER_MARK, PORT_RE, DELAY, STEP_TEXT,
         InstallError, Identity, Device, Updater, WebMidiBackend, WebMidiLink,
         pack7, unpack7, response, parseRequest, parseIdentity, productOf, logicalImage, modelOf, crc16, dec,
         packageDamage, loadPackage, pairOutput, handshake, connect, notFound, runInstall, probe,
         fetchLatest, newestInSums, sha256hex, pyRepr, sleep };
});

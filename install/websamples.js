// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Omnipotard
/* KAWA's web sample importer: install/kawa_samples.py in JavaScript, over Web MIDI (with SysEx).
 *
 * Two halves, both used by samples.html and run by the Node test (source/tests/websamples_test.js):
 *   - the conversion, a port of kawa_samples.py's prepare(): mono, resampled to 22050 Hz (or 11025), the silence
 *     before the sound gone, cut to the slot, normalised to 0.95, a fade out, 8-bit mu-law. For a WAV file the
 *     result is the Python tool's, byte for byte (the test checks it); other formats are decoded by the browser;
 *   - the protocol of firmware/src/kawa_sysex.c: F0 7D 4B 57 cmd pack7(args) F7, one request and one reply
 *     (INFO, BEGIN, WRITE, END, ERASE, LIST), 256 bytes a WRITE, a CRC-32 in the END header, the Python tool's
 *     timeouts (and one try again), the emulator's way of telling a late reply from the right one.
 * Safety: everything is checked BEFORE the first byte goes out (slot, size, rate, name). The firmware erases the
 * slot's header at BEGIN and writes it last, after the CRC of what reached the flash: a transfer that is cut
 * leaves the slot empty, never half written.
 *
 *   KawaSamples.readWav(arrayBuffer)                  -> source {rate, length, at(i)}   (throws KawaError "decode")
 *   KawaSamples.decodeWithBrowser(arrayBuffer, rate)  -> source                          (decodeAudioData)
 *   KawaSamples.prepare(source, {rate, cap})          -> {data, rate, silent, cut, ...}  (throws "short")
 *   KawaSamples.connect({nav})                        -> Fm1  (list(), send(), erase(), close())
 *   KawaSamples.sendAll(fm1, jobs, hooks)
 */
(function (root, factory) {
    if (typeof module === "object" && module.exports) module.exports = factory();
    else root.KawaSamples = factory();
})(typeof self !== "undefined" ? self : this, function () {
"use strict";

// ------------------------------------------------------------------ constants (kawa_core.c, kawa_sysex.c) ---

const USR_SIZE = 0x9000, USR_DATA = 0x100;
const CAP = USR_SIZE - USR_DATA;                      // 36608 B of sound: 1.66 s at 22050 Hz, 3.3 s at 11025 Hz
const MAGIC = 0x504D534B;                             // "KSMP" (usr_hdr_t.magic)
const RATE = 22050, RATES = [22050, 11025];
const NSLOTS = 4, NAME_MAX = 11, PACKET = 256, MIN_LEN = 64, HEADER_LEN = 36;
const SILENCE = 0.003, PEAK = 0.95, FADE = 64;        // kawa_samples.py prepare()
const CMD = { INFO: 1, BEGIN: 2, WRITE: 3, END: 4, ERASE: 5, LIST: 6 };
const CMD_NAME = ["", "INFO", "BEGIN", "WRITE", "END", "ERASE", "LIST"];
const RC = { 0: "ok", 1: "bad arguments", 2: "busy: stop KAWA first", 3: "flash error", 4: "CRC mismatch",
             5: "out of order / no session" };
// milliseconds: the Python tool's seconds x 1000 (ask(): INFO and LIST 2 s, BEGIN and END 3 s, WRITE 1.5 s,
// ERASE 5 s, two tries). open and portopen are the web's own. The tests shorten them.
const DELAY = { open: 300, portopen: 5000, tries: 2, info: 2000, list: 2000, begin: 3000, write: 1500, end: 3000,
                erase: 5000 };
const PORT_RE = /FM-1|Felucca|KAWA/i;                 // kawa_samples.py's pick(); the emulator ignores the case

// ------------------------------------------------------------------ errors ---

const slotName = k => "MY " + (k + 1);
function slotSentence(slot, state) {
    if (state === "unchanged") return "Nothing was changed on the FM-1.";
    if (state === "empty") return `${slotName(slot)} is empty now (the sound that was there is gone; nothing half written is kept).`;
    if (state === "unknown") return `${slotName(slot)} may be empty now: read the slots again to see.`;
    return "";
}
function messageOf(code, d) {
    switch (code) {
    case "nomidi":
        return "This browser cannot do Web MIDI. Use Chrome or Edge on a computer (Firefox, Safari and phones do not have it)" +
               (d.insecure ? "; and open this page over https:// or from localhost." : ".");
    case "denied":
        return "MIDI access with SysEx was refused. Allow it for this page (the permission prompt, or the site settings " +
               "next to the address), then try again.";
    case "noports":
        return "No MIDI device is connected. Plug the FM-1 into the computer with a USB data cable (a charge-only cable " +
               "does not work), switch it on, then try again.";
    case "noFm1":
        return "No MIDI port named FM-1, Felucca or KAWA was found. Choose the FM-1's ports in the list, then connect again.";
    case "portbusy":
        return "The FM-1's MIDI port could not be opened. Another program may be using it (a DAW, M-UPGRADE, another " +
               "browser tab): close it and try again.";
    case "noanswer":
        return "The FM-1 did not answer. KAWA 0.2 or later must be running on it: not the original M-VAVE firmware, not the " +
               "USB rescue screen (it says KAWA USB RESCUE: switch the FM-1 off and on again without holding OCT-). " +
               "Check that the cable carries data.";
    case "notkawa":
        return "Something answered, but it is not KAWA 0.2 or later. Samples only go to KAWA.";
    case "busy":
        return "KAWA is playing. Press PLAY on the FM-1 to stop it, then send again.";
    case "args":
        return "The FM-1 refused the request (bad arguments). Reload this page and try again.";
    case "flash":
        return "The FM-1 could not write its flash memory. Send again; if it happens again, switch the FM-1 off and on.";
    case "crc":
        return "The sound did not arrive intact (checksum mismatch). Send it again.";
    case "order":
        return "The transfer went out of order (a frame was lost). Send it again.";
    case "rcX":
        return `The FM-1 answered with an unknown code (${d.rc}).`;
    case "timeout":
        return "The FM-1 stopped answering. Check the cable and that KAWA is still running, then send again " +
               "(the FM-1 plays again by itself within 10 seconds).";
    case "lost":
        return "The FM-1 was disconnected. Plug it back in and send again " +
               "(the FM-1 plays again by itself within 10 seconds).";
    case "badreply":
        return "The FM-1 sent a reply this page does not understand. Reload the page and try again.";
    case "toobig":
        return `This sound is ${d.length} bytes and the FM-1's slot holds ${d.cap}.`;
    case "short":
        return "This sound is too short (less than 64 samples once the silence before it is cut).";
    case "silent":
        return "This sound is silent: there is nothing to send.";
    case "decode":
        return "This file could not be read as a sound" + (d.why ? " (" + d.why + ")" : "") + ".";
    case "badslot":
        return d.slot === undefined ? "There is no such slot on this FM-1." : `There is no slot ${slotName(d.slot)} on this FM-1.`;
    case "badargs":
        return "Refused before sending: " + (d.why || "invalid sample") + ".";
    case "cancelled":
        return "Cancelled. The FM-1 plays again by itself within 10 seconds.";
    default:
        return String(code);
    }
}
class KawaError extends Error {
    constructor(code, detail) {
        const d = detail || {};
        super(messageOf(code, d));
        this.name = "KawaError";
        this.code = code;
        this.base = this.message;
        Object.assign(this, d);
    }
    /* what the transfer left on the FM-1: "unchanged" (refused before anything was erased), "empty" (BEGIN went
     * through; the header is only written at the very end), "unknown" (the answer to BEGIN or END never came) */
    withSlot(slot, state) {
        this.slot = slot;
        this.slotState = state;
        const s = slotSentence(slot, state);
        this.message = s ? this.base + " " + s : this.base;
        return this;
    }
}

// ------------------------------------------------------------------ the wire (kawa_samples.py, kawa_sysex.c) ---

function pack7(b) {                                   // 7 bits at a time, the low ones first (ota.c, kawa_sysex.c)
    const out = [];
    let acc = 0, nb = 0;
    for (let i = 0; i < b.length; i++) {
        acc |= b[i] << nb;
        nb += 8;
        while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; }
    }
    if (nb) out.push(acc & 0x7F);
    return Uint8Array.from(out);
}
function unpack7(b) {
    const out = [];
    let acc = 0, nb = 0;
    for (let i = 0; i < b.length; i++) {
        acc |= (b[i] & 0x7F) << nb;
        nb += 7;
        if (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; }
    }
    return Uint8Array.from(out);
}
function frame(cmd, payload) {
    const c = typeof cmd === "string" ? CMD[cmd] : cmd;
    if (!(c >= 1 && c <= 6)) throw new KawaError("badargs", { why: "unknown command " + cmd });
    const p = pack7(payload || []);
    const f = new Uint8Array(6 + p.length);
    f[0] = 0xF0; f[1] = 0x7D; f[2] = 0x4B; f[3] = 0x57; f[4] = c;
    f.set(p, 5);
    f[f.length - 1] = 0xF7;
    return f;
}
function parseReply(msg) {                            // -> {cmd, body} or null (kawa_samples.py parse_reply)
    if (!msg || msg.length < 6 || msg[0] !== 0xF0 || msg[1] !== 0x7D || msg[2] !== 0x4B || msg[3] !== 0x57 ||
        msg[msg.length - 1] !== 0xF7) return null;
    const body = unpack7(msg.subarray(5, msg.length - 1));
    return body.length ? { cmd: msg[4], body } : null;
}

const CRC_T = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
        t[n] = c >>> 0;
    }
    return t;
})();
function crc32(u8) {                                  // zlib's
    let c = 0xFFFFFFFF;
    for (let i = 0; i < u8.length; i++) c = CRC_T[(c ^ u8[i]) & 0xFF] ^ (c >>> 8);
    return (c ^ 0xFFFFFFFF) >>> 0;
}

/* a name as the header stores it: upper case, ASCII, 11 characters (kawa_samples.py: upper(), encode("ascii",
 * "replace"), [:11], padded with zeros to 12). The page only lets names of cleanName() through. */
function headerName(name) {
    const out = new Uint8Array(12);
    const cps = Array.from(String(name === undefined || name === null ? "" : name).toUpperCase());
    for (let i = 0; i < NAME_MAX && i < cps.length; i++) {
        const c = cps[i].codePointAt(0);
        out[i] = c >= 32 && c < 127 ? c : 0x3F;       // (a control character or a non-ASCII one: "?")
    }
    return out;
}
function header(data, rate, name) {                   // usr_hdr_t: magic, len, rate, name[12], crc, rsv[2] (36 B, LE)
    const h = new Uint8Array(HEADER_LEN), dv = new DataView(h.buffer);
    dv.setUint32(0, MAGIC, true);
    dv.setUint32(4, data.length, true);
    dv.setUint32(8, rate, true);
    h.set(headerName(name), 12);
    dv.setUint32(24, crc32(data), true);
    return h;                                         // (rsv[2] stay 0)
}
function uploadFrames(slot, data, rate, name) {       // BEGIN, WRITE x n, END: kawa_samples.py upload_frames()
    const out = [{ name: "BEGIN", off: 0, frame: frame(CMD.BEGIN, [slot]) }];
    for (let off = 0; off < data.length; off += PACKET) {
        const chunk = data.subarray(off, Math.min(data.length, off + PACKET));
        const args = new Uint8Array(4 + chunk.length);
        args[0] = slot; args[1] = off & 0xFF; args[2] = (off >> 8) & 0xFF; args[3] = (off >> 16) & 0xFF;
        args.set(chunk, 4);
        out.push({ name: "WRITE", off, frame: frame(CMD.WRITE, args) });
    }
    const h = header(data, rate, name), a = new Uint8Array(1 + h.length);
    a[0] = slot;
    a.set(h, 1);
    out.push({ name: "END", off: data.length, frame: frame(CMD.END, a) });
    return out;
}

/* Everything the firmware would refuse is refused here first (kawa_sysex.c: slot < NUSR, len 64 .. USR_CAP,
 * rate 22050 or 11025, WRITE <= 256 B at multiples of 256). Throws KawaError "badslot" / "badargs" / "toobig". */
function validateUpload(slot, data, rate, name, limits) {
    const slots = (limits && limits.slots) || NSLOTS, cap = Math.min((limits && limits.cap) || CAP, CAP);
    if (!Number.isInteger(slot) || slot < 0 || slot >= slots) throw new KawaError("badslot", { slot });
    if (!(data instanceof Uint8Array)) throw new KawaError("badargs", { why: "the sound is not a byte array" });
    if (data.length < MIN_LEN) throw new KawaError("badargs", { why: "the sound is shorter than 64 bytes" });
    if (data.length > cap) throw new KawaError("toobig", { length: data.length, cap });
    if (!RATES.includes(rate)) throw new KawaError("badargs", { why: "the rate must be 22050 or 11025 Hz" });
    if (typeof name !== "string") throw new KawaError("badargs", { why: "no name" });
}

// ------------------------------------------------------------------ the sound: source -> mu-law ---

const rint = v => {                                   // numpy's round(): halves go to the even one
    const f = Math.floor(v), d = v - f;
    return d < 0.5 ? f : d > 0.5 ? f + 1 : (f % 2 === 0 ? f : f + 1);
};
const LOGMU = Math.log1p(255);
function mulawByte(x) {                               // kawa_samples.py mulaw() (as tools/gen_breaks.py)
    const y = Math.sign(x) * Math.log1p(255 * Math.abs(x)) / LOGMU;
    return Math.max(0, Math.min(255, rint((y + 1) * 127.5)));
}
const ULAW = (() => {                                 // BRK_ULAW (gen_breaks.py mulaw_table): what the firmware plays
    const t = new Int16Array(256);
    for (let i = 0; i < 256; i++) {
        const c = i / 127.5 - 1;
        const x = Math.sign(c) * (Math.pow(256, Math.abs(c)) - 1) / 255;
        t[i] = Math.max(-32767, Math.min(32767, rint(x * 32767)));
    }
    return t;
})();
function mulawToFloat(data) {                         // the converted sound, for the waveform and the preview
    const o = new Float32Array(data.length);
    for (let i = 0; i < data.length; i++) o[i] = ULAW[data[i]] / 32767;
    return o;
}

/* A source is {rate, length, at(i)}: the mono sample i (the mean of the channels) as a double. Nothing is copied. */
function fromChannels(chs, rate) {
    const C = chs.length, n = C ? chs[0].length : 0;
    const at = i => {
        let s = 0;
        for (let c = 0; c < C; c++) s += chs[c][i];
        const v = s / C;
        return v === v && v !== Infinity && v !== -Infinity ? v : 0;       // (nan_to_num)
    };
    return { rate, length: n, channels: C, at };
}

/* kawa_samples.py riff_wav(): PCM 8/16/24/32 bits, float 32/64 bits, WAVE_FORMAT_EXTENSIBLE; the channels' mean. */
function readWav(ab) {
    const d = new Uint8Array(ab instanceof ArrayBuffer ? ab : (ab.buffer || ab), ab.byteOffset || 0, ab.byteLength);
    const bad = why => new KawaError("decode", { why });
    if (d.length < 12 || d[0] !== 0x52 || d[1] !== 0x49 || d[2] !== 0x46 || d[3] !== 0x46 ||
        d[8] !== 0x57 || d[9] !== 0x41 || d[10] !== 0x56 || d[11] !== 0x45) throw bad("not a RIFF WAVE file");
    const dv = new DataView(d.buffer, d.byteOffset, d.byteLength);
    let fmt = null, data = null, i = 12;
    while (i + 8 <= d.length) {
        const size = dv.getUint32(i + 4, true);
        const body = d.subarray(i + 8, Math.min(d.length, i + 8 + size));
        const id = String.fromCharCode(d[i], d[i + 1], d[i + 2], d[i + 3]);
        if (id === "fmt ") fmt = body;
        else if (id === "data") data = body;
        i += 8 + size + (size & 1);
    }
    if (!fmt || !data || fmt.length < 16) throw bad("no fmt or data chunk");
    const fv = new DataView(fmt.buffer, fmt.byteOffset, fmt.byteLength);
    let tag = fv.getUint16(0, true);
    const ch = fv.getUint16(2, true), sr = fv.getUint32(4, true), bits = fv.getUint16(14, true);
    if (tag === 0xFFFE && fmt.length >= 26) tag = fv.getUint16(24, true);
    if (ch < 1) throw bad("no channel");
    if (!(sr >= 1)) throw bad("no sample rate");
    const bps = bits >> 3;
    let get;
    const pv = new DataView(data.buffer, data.byteOffset, data.byteLength);
    if (tag === 3 && bits === 32) get = o => pv.getFloat32(o, true);
    else if (tag === 3 && bits === 64) get = o => pv.getFloat64(o, true);
    else if (tag === 1 && bits === 8) get = o => (pv.getUint8(o) - 128) / 128;
    else if (tag === 1 && bits === 16) get = o => pv.getInt16(o, true) / 32768;
    else if (tag === 1 && bits === 24) get = o => ((data[o] | data[o + 1] << 8 | data[o + 2] << 16) << 8 >> 8) / 8388608;
    else if (tag === 1 && bits === 32) get = o => pv.getInt32(o, true) / 2147483648;
    else throw bad(`WAV format ${tag}, ${bits} bits`);
    const frames = Math.floor(Math.floor(data.length / bps) / ch);
    const at = f => {
        let s = 0;
        const o = f * ch * bps;
        for (let c = 0; c < ch; c++) s += get(o + c * bps);
        const v = s / ch;
        return v === v && v !== Infinity && v !== -Infinity ? v : 0;
    };
    return { rate: sr, length: frames, channels: ch, at, wav: true };
}

/* Non-WAV files (MP3, OGG, FLAC...): the browser decodes them, straight at the rate KAWA stores (as the emulator
 * does), so its own resampler does the work. env: where OfflineAudioContext lives (default: the global scope). */
async function decodeWithBrowser(ab, rate, env) {
    const g = env || (typeof self !== "undefined" ? self : globalThis);
    const OAC = g.OfflineAudioContext || g.webkitOfflineAudioContext;
    const AC = g.AudioContext || g.webkitAudioContext;
    const dec = (ctx, data) => new Promise((res, rej) => {
        try { const p = ctx.decodeAudioData(data, res, rej); if (p && p.then) p.then(res, rej); } catch (e) { rej(e); }
    });
    let buf = null, why = "";
    if (OAC) {
        try { buf = await dec(new OAC(1, 1, rate), ab.slice(0)); } catch (e) { why = (e && e.message) || ""; }
    }
    if (!buf && AC) {
        let ctx = null;
        try { ctx = new AC(); buf = await dec(ctx, ab.slice(0)); } catch (e) { why = why || (e && e.message) || ""; }
        finally { if (ctx && ctx.close) { try { ctx.close(); } catch (e) { /* closed already */ } } }
    }
    if (!buf) throw new KawaError("decode", { why: why || "the browser could not decode it" });
    const chs = [];
    for (let c = 0; c < buf.numberOfChannels; c++) chs.push(buf.getChannelData(c));
    return fromChannels(chs, buf.sampleRate);
}

/* The sound as the Python tool cuts it, sample i of the result on demand (so a 10 minute file costs no more than a
 * short one): the boxcar low-pass when going down (np.convolve(a, ones(k)/k, "same"), k = round(sr / rate)), then
 * the linear interpolation at t = i * sr / rate (np.interp). Same arithmetic, same order. */
function stream(src, rate) {
    const sr = src.rate, N = src.length, at = src.at;
    if (sr === rate) return { count: N, at };
    let base = at;
    if (sr > rate) {
        const k = Math.max(1, rint(sr / rate));
        if (k > 1) {
            const w = 1 / k, s = (k - 1) >> 1;
            base = j => {
                let acc = 0;
                for (let idx = j + s - (k - 1); idx <= j + s; idx++)
                    if (idx >= 0 && idx < N) acc += at(idx) * w;
                return acc;
            };
        }
    }
    return {
        count: Math.ceil((N * rate) / sr),
        at(i) {
            const t = (i * sr) / rate, j = Math.floor(t);
            if (j >= N - 1) return base(N - 1);
            if (j === t) return base(j);
            const a0 = base(j), a1 = base(j + 1);
            return (a1 - a0) * (t - j) + a0;
        },
    };
}

/* kawa_samples.py prepare(): -> {data (mu-law), rate, cut, silent, start, total, kept}.
 *   cut     the sound was longer than the slot (its end is lost)
 *   silent  nothing above 0.003 anywhere: the Python tool would send (and amplify) it all the same; the page refuses
 *   start / total / kept   in samples at `rate`: where the kept part begins in the converted whole, its length */
function prepare(src, o) {
    const rate = (o && o.rate) || RATE, cap = Math.min((o && o.cap) || CAP, CAP);
    if (!RATES.includes(rate)) throw new KawaError("badargs", { why: "the rate must be 22050 or 11025 Hz" });
    if (!src || !(src.rate >= 1)) throw new KawaError("decode", { why: "no sound in it" });
    if (!(src.length >= 1)) throw new KawaError("short");
    const st = stream(src, rate);
    let first = -1;
    for (let i = 0; i < st.count; i++) {
        if (Math.abs(st.at(i)) > SILENCE) { first = i; break; }
    }
    const silent = first < 0, start = silent ? 0 : first;
    const n = Math.min(cap, st.count - start);
    if (n < MIN_LEN) throw new KawaError("short");
    const a = new Float64Array(n);
    let peak = 0;
    for (let j = 0; j < n; j++) {
        const v = st.at(start + j);
        a[j] = v;
        const m = Math.abs(v);
        if (m > peak) peak = m;
    }
    if (peak > 0) {
        const g = PEAK / peak;
        for (let j = 0; j < n; j++) a[j] *= g;
    }
    const fade = Math.min(FADE, n >> 2);              // no click at the end (np.linspace(1, 0, fade))
    const step = -1 / (fade - 1);
    for (let j = 0; j < fade; j++) a[n - fade + j] *= j === fade - 1 ? 0 : j * step + 1;
    const data = new Uint8Array(n);
    for (let j = 0; j < n; j++) data[j] = mulawByte(a[j]);
    return { data, rate, cut: st.count - start > cap, silent, start, total: st.count, kept: n, peak };
}

/* what the page may send: a sound with nothing above 0.003 anywhere is refused (the Python tool would send it, and
 * amplify the noise floor to full scale); to empty a slot there is erase() */
function assertSendable(prep) {
    if (!prep || !(prep.data instanceof Uint8Array)) throw new KawaError("badargs", { why: "no converted sound" });
    if (prep.silent) throw new KawaError("silent");
    return prep;
}

// ------------------------------------------------------------------ names ---

/* a name the FM-1 can show: upper case, A-Z 0-9 space _ - . ! + #, 11 characters at most (may be empty) */
function cleanName(s) {
    return String(s === undefined || s === null ? "" : s).normalize("NFD").replace(/[\u0300-\u036f]/g, "").toUpperCase()
        .replace(/[^A-Z0-9 _\-.!+#]/g, " ").replace(/\s+/g, " ").trim().slice(0, NAME_MAX).trim();
}
function nameFromFile(fileName) { return cleanName(String(fileName || "").replace(/\.[^.]*$/, "")) || "SAMPLE"; }

// ------------------------------------------------------------------ Web MIDI ---

const within = (p, ms, code) => new Promise((res, rej) => {
    const t = setTimeout(() => rej(new KawaError(code)), ms);
    Promise.resolve(p).then(v => { clearTimeout(t); res(v); }, e => { clearTimeout(t); rej(e); });
});
const sleep = ms => new Promise(r => setTimeout(r, ms));

/* One MIDI in / out pair, one request and one reply at a time. Web MIDI hands over whole messages; a driver that cuts
 * a long SysEx in pieces is put back together; clock, active sensing and notes between replies are ignored. A reply is
 * only taken while a request waits for it, and only if it is the right command (and, for WRITE, the right slot and
 * offset: a late answer to an earlier try must not pass for the answer to the next frame). */
class Link {
    constructor(inp, out) {
        this.inp = inp; this.out = out;
        this.buf = null; this.wait = null;
        this.tries = 0;                                // how many times the last request went out
    }
    static async open(inp, out) {
        const link = new Link(inp, out);
        try {
            inp.onmidimessage = e => link._rx(e.data);
            await within(inp.open(), DELAY.portopen, "portbusy");
            await within(out.open(), DELAY.portopen, "portbusy");
        } catch (e) {
            await link.close();
            if (e instanceof KawaError) throw e;
            throw new KawaError("portbusy", { cause: e });
        }
        await sleep(DELAY.open);                       // CoreMIDI may drop a message sent just after the opening
        return link;
    }
    _rx(d) {
        if (!d || !d.length) return;
        if (d[0] === 0xF0) this.buf = [];              // a new SysEx (an unfinished one is dropped)
        else if (!this.buf || (d[0] >= 0x80 && d[0] !== 0xF7)) return;   // not inside a SysEx: clock, sensing, notes
        for (let i = 0; i < d.length; i++) {
            if (d[i] >= 0xF8) continue;                // (a realtime byte in the middle of a SysEx)
            this.buf.push(d[i]);
        }
        if (d[d.length - 1] === 0xF7) {
            const msg = Uint8Array.from(this.buf);
            this.buf = null;
            this._got(msg);
        }
    }
    _got(msg) {
        const w = this.wait, r = parseReply(msg);
        if (!w || !r || r.cmd !== w.cmd || (w.match && !w.match(r.body))) return;
        this.wait = null;
        clearTimeout(w.timer);
        w.res(r.body);
    }
    get gone() { return this.inp.state === "disconnected" || this.out.state === "disconnected"; }
    _once(cmd, f, ms, match, step) {
        return new Promise((res, rej) => {
            const w = { cmd, match, res, rej, step, timer: 0 };
            w.timer = setTimeout(() => {
                if (this.wait === w) this.wait = null;
                rej(new KawaError(this.gone ? "lost" : "timeout", { step }));
            }, ms);
            this.wait = w;
            try {
                this.out.send(f);
            } catch (e) {
                clearTimeout(w.timer);
                if (this.wait === w) this.wait = null;
                rej(new KawaError("lost", { step, cause: e }));
            }
        });
    }
    /* send, wait for the reply; after a timeout once more (kawa_samples.py ask(): tries = 2) */
    async ask(cmd, args, ms, match, signal) {
        const f = frame(cmd, args), step = CMD_NAME[cmd];
        this.tries = 0;
        let last = null;
        for (let t = 0; t < Math.max(1, DELAY.tries); t++) {
            if (signal && signal.aborted) throw new KawaError("cancelled");
            this.tries++;
            try {
                return await this._once(cmd, f, ms, match, step);
            } catch (e) {
                if (!(e instanceof KawaError) || e.code !== "timeout") throw e;
                last = e;
            }
        }
        throw last;
    }
    async close() {
        this.inp.onmidimessage = null;
        const w = this.wait;                               // a request still waiting fails now: it must never hang
        if (w) { clearTimeout(w.timer); this.wait = null; w.rej(new KawaError("lost", { step: w.step })); }
        for (const p of [this.inp, this.out]) {
            try { await within(Promise.resolve(p.close && p.close()), DELAY.portopen, "portbusy"); } catch (e) { /* gone */ }
        }
    }
}

function portList(map) {
    const a = [];
    if (map && typeof map.forEach === "function") map.forEach(p => { if (p && p.state !== "disconnected") a.push(p); });
    return a;
}
/* the FM-1's ports: by id when given (the page's pickers), else by name (FM-1, Felucca, KAWA) */
function findPorts(access, want) {
    const w = want || {};
    const outs = portList(access && access.outputs), ins = portList(access && access.inputs);
    const out = (w.outId && outs.find(p => p.id === w.outId)) || outs.find(p => PORT_RE.test(p.name || "")) || null;
    const inp = (w.inId && ins.find(p => p.id === w.inId)) || ins.find(p => PORT_RE.test(p.name || "")) || null;
    return { outs, ins, out, inp };
}

const ascii = b => Array.from(b, c => (c >= 32 && c < 127 ? String.fromCharCode(c) : "?")).join("");
const nameText = b => { const z = b.indexOf(0); return ascii(z < 0 ? b : b.subarray(0, z)); };   // (a LIST name: up to the NUL)
function parseInfo(b) {                               // rc, "KAWA", version (4 chars), slots, capacity (3 B LE)
    if (!b || b.length < 5 || b[0] !== 0 || ascii(b.subarray(1, 5)) !== "KAWA") return null;
    return {
        version: b.length >= 9 ? ascii(b.subarray(5, 9)).trim() : "",
        slots: b.length >= 10 ? b[9] : NSLOTS,
        cap: b.length >= 13 ? (b[10] | b[11] << 8 | b[12] << 16) : CAP,
    };
}
function parseList(b, slots) {                        // rc, then per slot: used, rate (2 B), len (3 B), name (12 B)
    const n = Math.min(slots, Math.floor((b.length - 1) / 18)), out = [];
    for (let k = 0; k < n; k++) {
        const q = b.subarray(1 + 18 * k, 19 + 18 * k);
        const nm = nameText(q.subarray(6, 18));
        const rate = q[1] | q[2] << 8, len = q[3] | q[4] << 8 | q[5] << 16;
        out.push({ slot: k, used: q[0] !== 0, rate, len, name: nm, seconds: q[0] && rate ? len / rate : 0 });
    }
    return out;
}
function rcError(rc, step) {
    const code = { 1: "args", 2: "busy", 3: "flash", 4: "crc", 5: "order" }[rc] || "rcX";
    return new KawaError(code, { rc, step });
}
const rcCheck = (body, step) => { if (body[0] !== 0) throw rcError(body[0], step); return body; };

/* the FM-1 as a session: connect() finds it, asks INFO, and answers for it */
class Fm1 {
    constructor(access, link, info, ports) {
        this.access = access; this.link = link; this.info = info; this.ports = ports;
        this.name = (ports.inp && ports.inp.name) || "FM-1";
        this.busy = false;
    }
    get limits() { return { slots: Math.min(this.info.slots, NSLOTS), cap: Math.min(this.info.cap, CAP) }; }
    get gone() { return this.link.gone; }
    async _exclusive(fn) {
        if (this.busy) throw new Error("the FM-1 is busy with another request");
        this.busy = true;
        try { return await fn(); } finally { this.busy = false; }
    }
    list() {
        return this._exclusive(async () => {
            const b = await this.link.ask(CMD.LIST, [], DELAY.list);
            rcCheck(b, "LIST");
            const l = parseList(b, this.info.slots);
            if (!l.length) throw new KawaError("badreply", { step: "LIST" });
            return l;
        });
    }
    erase(slot) {
        return this._exclusive(async () => {
            if (!Number.isInteger(slot) || slot < 0 || slot >= this.limits.slots) throw new KawaError("badslot", { slot });
            const b = await this.link.ask(CMD.ERASE, [slot], DELAY.erase, r => r.length >= 2 && r[1] === slot);
            rcCheck(b, "ERASE");
            return { slot };
        });
    }
    /* BEGIN, WRITE x n, END: kawa_samples.py's loop with the emulator's care. opts: onProgress(sent, total), signal. */
    send(slot, data, rate, name, opts) {
        const o = opts || {};
        return this._exclusive(async () => {
            validateUpload(slot, data, rate, name, this.limits);          // nothing goes out before this passes
            const link = this.link, n = data.length;
            let frames = 0, retries = 0, begun = false;
            const count = () => { frames++; retries += link.tries - 1; };
            try {
                rcCheck(await link.ask(CMD.BEGIN, [slot], DELAY.begin, b => b.length >= 2 && b[1] === slot, o.signal), "BEGIN");
                begun = true;
                count();
                if (o.onProgress) o.onProgress(0, n);
                for (let off = 0; off < n; off += PACKET) {
                    const chunk = data.subarray(off, Math.min(n, off + PACKET));
                    const args = new Uint8Array(4 + chunk.length);
                    args[0] = slot; args[1] = off & 0xFF; args[2] = (off >> 8) & 0xFF; args[3] = (off >> 16) & 0xFF;
                    args.set(chunk, 4);
                    const match = b => b.length >= 5 && b[1] === slot && b[2] === args[1] && b[3] === args[2] && b[4] === args[3];
                    rcCheck(await link.ask(CMD.WRITE, args, DELAY.write, match, o.signal), "WRITE");
                    count();
                    if (o.onProgress) o.onProgress(off + chunk.length, n);
                }
                const h = header(data, rate, name), a = new Uint8Array(1 + h.length);
                a[0] = slot;
                a.set(h, 1);
                if (o.signal && o.signal.aborted) throw new KawaError("cancelled");
                const end = await link.ask(CMD.END, a, DELAY.end, b => b.length >= 2 && b[1] === slot, o.signal);
                count();
                if (end[0] === 5 && link.tries > 1) {
                    /* the first END may have been taken and its answer lost: the second finds no session (code 5).
                     * The slot says what happened. */
                    const l = parseList(rcCheck(await link.ask(CMD.LIST, [], DELAY.list), "LIST"), this.info.slots)[slot];
                    if (!(l && l.used && l.len === n && l.rate === rate && l.name === nameText(headerName(name)))) rcCheck(end, "END");
                } else rcCheck(end, "END");
            } catch (e) {
                if (e instanceof KawaError) {
                    /* what is on the FM-1 now: before BEGIN went through, a refusal (busy, bad arguments) or a cancel
                     * changed nothing, no answer at all leaves it open; after BEGIN the header is gone (empty) until
                     * END is acknowledged, and an END whose answer never came may have gone through */
                    let state;
                    if (!begun) state = e.code === "cancelled" || e.rc === 1 || e.rc === 2 ? "unchanged" : "unknown";
                    else if (e.step === "END" && (e.code === "timeout" || e.code === "lost")) state = "unknown";
                    else state = "empty";
                    e.withSlot(slot, state);
                }
                throw e;
            }
            return { slot, bytes: n, rate, frames, retries };
        });
    }
    async close() { await this.link.close(); }
}

async function connect(opts) {
    const o = opts || {};
    const nav = o.nav !== undefined ? o.nav : (typeof navigator !== "undefined" ? navigator : null);
    if (!nav || typeof nav.requestMIDIAccess !== "function") {
        throw new KawaError("nomidi", { insecure: typeof isSecureContext !== "undefined" && isSecureContext === false });
    }
    let access = o.access || null;
    if (!access) {
        try { access = await nav.requestMIDIAccess({ sysex: true }); }
        catch (e) { throw new KawaError("denied", { cause: e }); }
    }
    const ports = findPorts(access, o);
    if (!ports.out || !ports.inp) {
        throw new KawaError(!ports.outs.length || !ports.ins.length ? "noports" : "noFm1", { ports, access });
    }
    const link = await Link.open(ports.inp, ports.out);
    let info;
    try {
        let body;
        try { body = await link.ask(CMD.INFO, [], DELAY.info); }
        catch (e) { throw e instanceof KawaError && e.code === "timeout" ? new KawaError("noanswer") : e; }
        info = parseInfo(body);
        if (!info) throw new KawaError("notkawa");
    } catch (e) {
        await link.close();
        throw e;
    }
    return new Fm1(access, link, info, ports);
}

/* jobs: [{slot, data, rate, name}] one after the other. hooks: start(i, job), progress(i, sent, total), done(i, res),
 * fail(i, err), signal. Stops at the first failure that says the FM-1 is not there or not ready (no answer, unplugged,
 * playing, cancelled); another refusal leaves the next ones to be tried. */
const STOPS = ["timeout", "lost", "busy", "cancelled", "portbusy", "noanswer"];
async function sendAll(fm1, jobs, hooks) {
    const h = hooks || {}, results = [];
    for (let i = 0; i < jobs.length; i++) {
        if (h.signal && h.signal.aborted) break;
        const j = jobs[i];
        if (h.start) h.start(i, j);
        try {
            const r = await fm1.send(j.slot, j.data, j.rate, j.name, {
                signal: h.signal, onProgress: (s, t) => { if (h.progress) h.progress(i, s, t); } });
            results.push({ ok: true, job: j, result: r });
            if (h.done) h.done(i, r);
        } catch (e) {
            results.push({ ok: false, job: j, error: e });
            if (h.fail) h.fail(i, e);
            if (!(e instanceof KawaError) || STOPS.includes(e.code)) break;
        }
    }
    return results;
}

return {
    USR_SIZE, USR_DATA, CAP, MAGIC, RATE, RATES, NSLOTS, NAME_MAX, PACKET, MIN_LEN, HEADER_LEN, SILENCE, PEAK, FADE,
    CMD, CMD_NAME, RC, DELAY, PORT_RE, KawaError, ULAW,
    pack7, unpack7, frame, parseReply, crc32, headerName, header, uploadFrames, validateUpload,
    mulawByte, mulawToFloat, fromChannels, readWav, decodeWithBrowser, prepare, assertSendable, cleanName, nameFromFile,
    Link, Fm1, findPorts, parseInfo, parseList, connect, sendAll,
};
});

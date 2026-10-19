#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Omnipotard
/* The sample importer page (samples.html + install/websamples.js) in a real Chromium, with the simulated FM-1 of
 * tests/kawa_sysex_mock.js standing in for Web MIDI (headless Chromium has no MIDI device): the drop zone, the real
 * decodeAudioData (MP3, OGG, FLAC made with ffmpeg when it is there), the canvas, the slots, the name field, the preview,
 * the confirm dialog, the progress, the errors, a phone width, file://. Needs Playwright and a Chromium:
 *
 *   NODE_PATH=$(npm root -g) PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers node source/tests/websamples_ui_test.js
 *
 * Without Playwright it says so and exits 0. KAWA_SHOTS=DIR also saves screenshots (desktop and phone width).
 * It does not prove Chrome's real Web MIDI (permission prompt, USB, port names): only a real FM-1 does. */
"use strict";
const assert = require("node:assert/strict");
const cp = require("node:child_process");
const fs = require("node:fs");
const http = require("node:http");
const os = require("node:os");
const path = require("node:path");

let chromium;
try { ({ chromium } = require("playwright")); } catch (e) {
    console.log("websamples_ui_test: SKIPPED, no Playwright (NODE_PATH=$(npm root -g) node source/tests/websamples_ui_test.js)");
    process.exit(0);
}
const ROOT = path.join(__dirname, "..", "..");
const K = require(path.join(ROOT, "install", "websamples.js"));
const MOCK = fs.readFileSync(path.join(__dirname, "kawa_sysex_mock.js"), "utf8");
const SHOTS = process.env.KAWA_SHOTS;
const TYPES = { ".html": "text/html; charset=utf-8", ".js": "text/javascript", ".png": "image/png" };
const FAST = { open: 2, portopen: 300, info: 150, list: 150, begin: 300, write: 150, end: 300, erase: 300 };

const server = http.createServer((req, res) => {                     // a static server of the repository root
    const u = new URL(req.url, "http://x");
    let f = path.join(ROOT, decodeURIComponent(u.pathname));
    if (u.pathname.endsWith("/")) f = path.join(f, "index.html");
    if (!f.startsWith(ROOT) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); return res.end("not found"); }
    res.writeHead(200, { "content-type": TYPES[path.extname(f)] || "text/plain" });
    res.end(fs.readFileSync(f));
});

// ---- sounds
const tone = (rate, secs, freq, amp = 0.6, decay = 1.5) => {
    const n = Math.round(rate * secs), x = new Float64Array(n);
    for (let i = 0; i < n; i++) x[i] = amp * Math.sin(2 * Math.PI * freq * i / rate) * Math.exp(-decay * i / rate);
    return x;
};
function wav(rate, chans) {
    const C = chans.length, n = chans[0].length, data = Buffer.alloc(n * C * 2);
    for (let i = 0; i < n; i++) for (let c = 0; c < C; c++) data.writeInt16LE(Math.round(Math.max(-1, Math.min(1, chans[c][i])) * 32767), (i * C + c) * 2);
    const h = Buffer.alloc(44);
    h.write("RIFF", 0); h.writeUInt32LE(36 + data.length, 4); h.write("WAVEfmt ", 8); h.writeUInt32LE(16, 16); h.writeUInt16LE(1, 20);
    h.writeUInt16LE(C, 22); h.writeUInt32LE(rate, 24); h.writeUInt32LE(rate * C * 2, 28); h.writeUInt16LE(C * 2, 32); h.writeUInt16LE(16, 34);
    h.write("data", 36); h.writeUInt32LE(data.length, 40);
    return Buffer.concat([h, data]);
}
const prepared = buf => K.prepare(K.readWav(new Uint8Array(buf))).data;       // what the page must send for this file
const F = {
    kick: wav(44100, [tone(44100, 0.5, 90, 0.9, 8)]),
    snare: wav(22050, [tone(22050, 0.7, 700, 0.7, 4)]),
    hat: wav(8000, [tone(8000, 0.4, 2500, 0.7, 6)]),
    stereo: wav(48000, [tone(48000, 0.8, 330, 0.8, 2), tone(48000, 0.8, 440, 0.4, 3)]),
    long: wav(44100, [Float64Array.from([...new Float64Array(11025), ...tone(44100, 4, 200, 0.7, 0.1)])]),     // 0.25 s of silence first
    silent: wav(22050, [new Float64Array(11025)]),
    tiny: wav(22050, [tone(22050, 20 / 22050, 2000, 0.8, 0)]),
};

const mockInit = o => MOCK + `;(function () {
    var M = window.KawaSysexMock, midi = new M.MockMidi(${JSON.stringify(o.midi || {})});
    window.__midi = midi;
    ${o.noDevice ? "" : `window.__dev = midi.add(new M.MockFm1(${JSON.stringify(o.dev || {})}), ${JSON.stringify(o.name || "FM-1")});`}
    ${o.noWebMidi ? `Object.defineProperty(Navigator.prototype, "requestMIDIAccess", { value: undefined, configurable: true });` :
      `Object.defineProperty(navigator, "requestMIDIAccess", { value: function (o) { return midi.request(o); }, configurable: true });`}
})();`;

let browser, base;
const tests = [];
const test = (name, fn) => tests.push([name, fn]);
const seenAll = [];

async function open(o = {}, url) {
    const ctx = await browser.newContext({ viewport: o.viewport || { width: 1100, height: 900 }, acceptDownloads: false });
    if (!o.real) await ctx.addInitScript({ content: mockInit(o) });
    const page = await ctx.newPage();
    const seen = { external: [], errors: [], requests: [] };
    page.on("request", r => { seen.requests.push(r.url()); if (!/^(http:\/\/127\.0\.0\.1|file:|data:|blob:|about:)/.test(r.url())) seen.external.push(r.url()); });
    page.on("pageerror", e => seen.errors.push("pageerror: " + e.message));
    page.on("console", m => { if (m.type() === "error") seen.errors.push("console: " + m.text()); });
    await page.goto(url || base + "/samples.html");
    await page.evaluate(d => { if (window.KawaSamples) Object.assign(window.KawaSamples.DELAY, d); }, FAST);
    seenAll.push(seen);
    return { ctx, page, seen };
}
const files = (page, list) => page.setInputFiles("#file", list.map(([name, buffer, mimeType]) => ({ name, mimeType: mimeType || "application/octet-stream", buffer })));
const settled = (page, n) => page.waitForFunction(k => { const it = window.__kawaSamples.items; return it.length === k && it.every(i => i.state !== "loading"); }, n, { timeout: 15000 });
const devSlot = (page, k) => page.evaluate(k => { const s = window.__dev.slotSound(k); return s ? Array.from(s) : null; }, k);
const devName = (page, k) => page.evaluate(k => (window.__dev.usrData[k] ? window.__dev.slotName(k) : null), k);
const devBad = page => page.evaluate(() => ({ violations: window.__dev.violations, refusals: window.__dev.refusals }));
const itemText = (page, i, sel) => page.locator(".item").nth(i).locator(sel).innerText();
const noOverflow = page => page.evaluate(() => document.documentElement.scrollWidth <= document.documentElement.clientWidth + 1);
async function shot(page, name) { if (SHOTS) { fs.mkdirSync(SHOTS, { recursive: true }); await page.screenshot({ path: path.join(SHOTS, name + ".png"), fullPage: true }); } }
const inkOf = (page, i) => page.locator(".item").nth(i).locator("canvas").evaluate(c => {   // pixels of the waveform colour
    const d = c.getContext("2d").getImageData(0, 0, c.width, c.height).data;
    let n = 0;
    for (let p = 0; p < d.length; p += 4) if (d[p] > 200 && d[p + 1] < 140 && d[p + 2] > 130) n++;
    return n;
});

// ------------------------------------------------------------------------------------------------------------

test("the page loads alone: nothing from the network, no error, Send waits for a sound; works from file://", async () => {
    const { ctx, page, seen } = await open();
    assert.equal(await page.title(), "KAWA Sample Importer");
    assert.equal(await page.locator("#send").isDisabled(), true);
    assert.match(await page.locator("#why").innerText(), /Add a sound first/);
    assert.equal(await page.locator("#slots li").count(), 4);
    assert.deepEqual(seen.external, []);
    assert.deepEqual(seen.errors, []);
    assert.ok(seen.requests.some(u => u.endsWith("/install/websamples.js")), "the script is the repository's own");
    assert.match(await page.locator(".warn").innerText(), /press PLAY so nothing is playing/i);
    assert.match(await page.locator(".warn").innerText(), /stays empty/i);
    await shot(page, "samples-empty");
    await ctx.close();
    const f = await open({}, "file://" + path.join(ROOT, "samples.html"));
    assert.equal(await f.page.evaluate(() => typeof window.KawaSamples), "object", "install/websamples.js loads from file://");
    assert.deepEqual(f.seen.errors, []);
    await f.ctx.close();
});

test("sounds are added by choosing or dropping: waveform, name from the file, the first free slots in order, 11 characters", async () => {
    const { ctx, page, seen } = await open();
    await files(page, [["Kick drum.wav", F.kick], ["snare_01_with_a_long_name.wav", F.snare]]);
    await settled(page, 2);
    assert.equal(await page.locator(".item").count(), 2);
    assert.equal(await page.locator(".item").nth(0).locator("input.name").inputValue(), "KICK DRUM");
    assert.equal(await page.locator(".item").nth(1).locator("input.name").inputValue(), "SNARE_01_WI");
    assert.equal(await page.locator(".item").nth(0).locator("select.slot").inputValue(), "0");
    assert.equal(await page.locator(".item").nth(1).locator("select.slot").inputValue(), "1");
    assert.ok(await inkOf(page, 0) > 200, "the waveform is drawn");
    assert.ok(await inkOf(page, 1) > 200);
    assert.match(await itemText(page, 0, ".info"), /0\.\d\d s/);
    assert.match(await itemText(page, 0, ".info"), /\d+ bytes/);
    assert.match(await itemText(page, 0, ".keptnote"), /The whole sound is used/, "one skipped zero sample is not 'silence removed'");
    assert.equal(await page.locator("#send").isDisabled(), false);
    assert.match(await page.locator("#why").innerText(), /KICK DRUM into MY 1, SNARE_01_WI into MY 2/);
    // name editing: upper case, forbidden characters dropped, 11 characters at most
    const inp = page.locator(".item").nth(0).locator("input.name");
    await inp.fill("");
    await inp.pressSequentially("my kick/é*1234567890");
    assert.equal(await inp.inputValue(), "MY KICK1234");
    assert.equal(await inp.evaluate(e => e.maxLength), 11);
    // a third file by drag and drop onto the page
    await page.evaluate(async b64 => {
        const bin = Uint8Array.from(atob(b64), c => c.charCodeAt(0));
        const dt = new DataTransfer();
        dt.items.add(new File([bin], "dropped hat.wav", { type: "audio/wav" }));
        window.dispatchEvent(new DragEvent("dragenter", { dataTransfer: dt, bubbles: true }));
        window.dispatchEvent(new DragEvent("dragover", { dataTransfer: dt, bubbles: true, cancelable: true }));
        window.dispatchEvent(new DragEvent("drop", { dataTransfer: dt, bubbles: true, cancelable: true }));
    }, F.hat.toString("base64"));
    await settled(page, 3);
    assert.equal(await page.locator(".item").nth(2).locator("input.name").inputValue(), "DROPPED HAT");
    assert.equal(await page.locator(".item").nth(2).locator("select.slot").inputValue(), "2");
    assert.equal(await page.evaluate(() => document.body.classList.contains("dragging")), false);
    // 8 kHz: converted by the page, 0.4 s
    assert.match(await itemText(page, 2, ".info"), /0\.4\d s/);
    // remove one
    await page.locator(".item").nth(1).locator(".rm").click();
    assert.equal(await page.locator(".item").count(), 2);
    assert.deepEqual(seen.errors, []);
    await shot(page, "samples-items");
    await ctx.close();
});

test("choosing a slot that another sound holds swaps them; 'Don't send' keeps a sound out of the batch", async () => {
    const { ctx, page } = await open();
    await files(page, [["a.wav", F.kick], ["b.wav", F.snare], ["c.wav", F.hat]]);
    await settled(page, 3);
    await page.locator(".item").nth(0).locator("select.slot").selectOption("2");
    assert.deepEqual(await page.evaluate(() => window.__kawaSamples.items.map(i => i.slot)), [2, 1, 0], "a took MY 3, c took a's MY 1");
    await page.locator(".item").nth(1).locator("select.slot").selectOption("-1");
    assert.match(await page.locator("#why").innerText(), /2 sounds to send/);
    assert.match(await itemText(page, 1, ".imsg"), /Choose a slot/);
    await ctx.close();
});

test("Send to my FM-1: connect, read the slots, send three sounds, the FM-1 holds exactly what the conversion makes", async () => {
    const { ctx, page, seen } = await open();
    await files(page, [["kick.wav", F.kick], ["stereo.wav", F.stereo], ["hat.wav", F.hat]]);
    await settled(page, 3);
    await page.locator("#connect").click();
    await page.waitForFunction(() => /Connected to FM-1/.test(document.getElementById("devtext").textContent));
    assert.match(await page.locator("#devtext").innerText(), /KAWA 0\.4, 4 slots of 36608 bytes/);
    assert.equal(await page.locator("#slots li.empty").count(), 4, "all four empty");
    await page.locator("#send").click();
    await page.waitForFunction(() => /^Done: 3 sounds sent/.test(document.getElementById("summary").textContent), null, { timeout: 15000 });
    assert.match(await page.locator("#summary").innerText(), /SOURCE = MY 1, MY 2, MY 3/);
    assert.deepEqual(await devSlot(page, 0), Array.from(prepared(F.kick)));
    assert.deepEqual(await devSlot(page, 1), Array.from(prepared(F.stereo)));
    assert.deepEqual(await devSlot(page, 2), Array.from(prepared(F.hat)));
    assert.deepEqual([await devName(page, 0), await devName(page, 1), await devName(page, 2), await devName(page, 3)], ["KICK", "STEREO", "HAT", null]);
    assert.deepEqual(await devBad(page), { violations: [], refusals: [] });
    assert.equal(await page.locator(".item.sent").count(), 3);
    assert.match(await itemText(page, 0, ".imsg"), /Done: in MY 1/);
    assert.equal(await page.locator("#slots li:not(.empty)").count(), 3, "the tiles show the three names now");
    assert.match(await page.locator("#slots li").nth(1).innerText(), /STEREO/);
    assert.equal(await page.locator("#send").isDisabled(), true, "nothing left to send");
    assert.deepEqual(seen.errors, []);
    await shot(page, "samples-sent");
    await ctx.close();
});

test("replacing a sound asks first; the 'busy' refusal keeps the old sound and says to press PLAY", async () => {
    const { ctx, page } = await open();
    await files(page, [["one.wav", F.kick]]);
    await settled(page, 1);
    await page.locator("#send").click();
    await page.waitForFunction(() => /^Done\./.test(document.getElementById("summary").textContent), null, { timeout: 15000 });
    const first = await devSlot(page, 0);
    // a second sound into the same slot: the dialog
    await files(page, [["two.wav", F.snare]]);
    await settled(page, 2);
    await page.locator(".item").nth(0).locator("select.slot").selectOption("-1");           // the first one is sent already
    await page.locator(".item").nth(1).locator("select.slot").selectOption("0");
    assert.match(await itemText(page, 1, ".imsg"), /Replaces ONE in MY 1/);
    await page.locator("#send").click();
    await page.waitForSelector("dialog[open]");
    assert.match(await page.locator("dialog").innerText(), /MY 1: ONE becomes TWO/);
    assert.match(await page.locator("dialog").innerText(), /KAWA must be stopped/);
    assert.match(await page.locator("dialog").innerText(), /slot stays empty/);
    await page.locator("#confirm-no").click();
    assert.deepEqual(await devSlot(page, 0), first, "cancelled: nothing changed");
    assert.equal(await page.evaluate(() => window.__dev.framesOf(2).length), 1, "no second BEGIN");
    // KAWA plays: the FM-1 refuses at BEGIN
    await page.evaluate(() => { window.__dev.sqPlay = 1; });
    await page.locator("#send").click();
    await page.waitForSelector("dialog[open]");
    await page.locator("#confirm-yes").click();
    await page.waitForFunction(() => /Nothing was sent/.test(document.getElementById("summary").textContent), null, { timeout: 15000 });
    assert.match(await page.locator("#summary").innerText(), /Press PLAY/);
    assert.match(await page.locator("#summary").innerText(), /Nothing was changed on the FM-1/);
    assert.deepEqual(await devSlot(page, 0), first, "the old sound is still there");
    assert.equal(await page.locator(".item.failed").count(), 1);
    // PLAY pressed to stop: send again, it goes through
    await page.evaluate(() => { window.__dev.sqPlay = 0; });
    await page.locator("#send").click();
    await page.waitForSelector("dialog[open]");
    await page.locator("#confirm-yes").click();
    await page.waitForFunction(() => /^Done\./.test(document.getElementById("summary").textContent), null, { timeout: 15000 });
    assert.deepEqual(await devSlot(page, 0), Array.from(prepared(F.snare)));
    assert.equal(await devName(page, 0), "TWO");
    await ctx.close();
});

test("a sound that is too long is cut (and says so), the slot is exactly full; the 11 kHz choice doubles the time", async () => {
    const { ctx, page } = await open();
    await files(page, [["long.wav", F.long]]);
    await settled(page, 1);
    assert.match(await itemText(page, 0, ".info"), /1\.66 s/);
    assert.match(await itemText(page, 0, ".info"), /36608 bytes/);
    assert.match(await itemText(page, 0, ".keptnote"), /Cut to fit the slot: the last 2\.\d\d s are left out/);
    assert.match(await itemText(page, 0, ".keptnote"), /Silence before the sound removed: 0\.2\d s/);
    await page.locator(".item").nth(0).locator("select.rate").selectOption("11025");
    await page.waitForFunction(() => window.__kawaSamples.items[0].prep && window.__kawaSamples.items[0].prep.rate === 11025);
    assert.match(await itemText(page, 0, ".info"), /3\.32 s/);
    await page.locator("#send").click();
    await page.waitForFunction(() => /^Done\./.test(document.getElementById("summary").textContent), null, { timeout: 15000 });
    const sent = await devSlot(page, 0);
    assert.equal(sent.length, K.CAP);
    assert.deepEqual(sent, Array.from(K.prepare(K.readWav(new Uint8Array(F.long)), { rate: 11025 }).data));
    assert.equal(await page.evaluate(() => window.__dev.usrHdr[0].rate), 11025);
    await ctx.close();
});

test("silence and a too-short file are refused with a clear message and cannot be sent", async () => {
    const { ctx, page } = await open();
    await files(page, [["silence.wav", F.silent]]);
    await settled(page, 1);
    assert.match(await itemText(page, 0, ".imsg"), /This sound is silent/);
    assert.equal(await page.locator(".item.error").count(), 1);
    assert.equal(await page.locator("#send").isDisabled(), true);
    await files(page, [["tiny.wav", F.tiny], ["notasound.wav", Buffer.from("this is not audio at all, just text")]]);
    await settled(page, 3);
    assert.match(await itemText(page, 1, ".imsg"), /too short/);
    assert.match(await itemText(page, 2, ".imsg"), /could not be read as a sound/);
    assert.equal(await page.locator("#send").isDisabled(), true);
    assert.match(await page.locator("#why").innerText(), /marked in red/);
    await ctx.close();
});

test("MP3, OGG and FLAC go through the browser's decoder (made with ffmpeg; skipped where Chromium lacks the codec)", async () => {
    const ff = cp.spawnSync("ffmpeg", ["-version"]);
    if (ff.status !== 0) { console.log("         (no ffmpeg: skipped)"); return; }
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "kawa-ui-"));
    try {
        fs.writeFileSync(path.join(dir, "src.wav"), wav(44100, [tone(44100, 0.8, 440, 0.7, 2), tone(44100, 0.8, 660, 0.5, 2)]));
        const made = [];
        for (const [ext, args] of [["mp3", ["-codec:a", "libmp3lame"]], ["ogg", ["-codec:a", "libvorbis"]], ["flac", ["-codec:a", "flac"]], ["m4a", ["-codec:a", "aac"]]]) {
            const r = cp.spawnSync("ffmpeg", ["-y", "-loglevel", "error", "-i", path.join(dir, "src.wav"), ...args, path.join(dir, "s." + ext)]);
            if (r.status === 0) made.push(ext);
        }
        const { ctx, page, seen } = await open();
        const can = await page.evaluate(() => { const a = new Audio(); return { mp3: a.canPlayType("audio/mpeg"), ogg: a.canPlayType('audio/ogg; codecs="vorbis"'), flac: a.canPlayType("audio/flac"), m4a: a.canPlayType('audio/mp4; codecs="mp4a.40.2"') }; });
        const results = {};
        for (const ext of made) {
            const before = await page.locator(".item").count();
            await files(page, [["sound." + ext, fs.readFileSync(path.join(dir, "s." + ext))]]);
            await settled(page, before + 1);
            const st = await page.evaluate(i => { const it = window.__kawaSamples.items[i]; return { state: it.state, secs: it.prep ? it.prep.data.length / it.prep.rate : 0, rate: it.src && it.src.rate, wav: !!(it.src && it.src.wav), err: it.error && it.error.message }; }, before);
            results[ext] = st.state;
            if (st.state === "ready") {
                assert.equal(st.wav, false, ext + ": not read as a WAV");
                assert.equal(st.rate, 22050, ext + ": decoded straight at the FM-1's rate");
                assert.ok(st.secs > 0.7 && st.secs < 0.9, `${ext}: ${st.secs} s`);
                assert.ok(await inkOf(page, before) > 200, ext + ": waveform");
            } else {
                assert.ok(!can[ext], `${ext}: Chromium says it can play it (${can[ext]}) but the page could not decode it: ${st.err}`);
                console.log(`         (${ext}: not decoded by this Chromium; canPlayType = '${can[ext]}')`);
            }
        }
        console.log("         decoded by this Chromium: " + Object.entries(results).map(([k, v]) => `${k} ${v}`).join(", "));
        assert.ok(Object.values(results).some(v => v === "ready"), "at least one compressed format should decode");
        assert.deepEqual(seen.errors.filter(e => !/Failed to load resource|decode/i.test(e)), []);
        await ctx.close();
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
});

test("the preview plays the converted sound and stops; the AudioContext gets the FM-1's rate", async () => {
    const { ctx, page, seen } = await open();
    await page.addInitScript(() => {
        const O = window.AudioContext;
        window.__played = [];
        const cb = O.prototype.createBuffer;
        O.prototype.createBuffer = function (c, n, r) { window.__played.push({ n, r }); return cb.call(this, c, n, r); };
    });
    await page.reload();
    await page.evaluate(d => Object.assign(window.KawaSamples.DELAY, d), FAST);
    await files(page, [["snare.wav", F.snare]]);
    await settled(page, 1);
    const btn = page.locator(".item").nth(0).locator(".play");
    assert.equal(await btn.innerText(), "Play");
    await btn.click();
    assert.equal(await btn.innerText(), "Stop");
    const rec = await page.evaluate(() => window.__played);
    assert.equal(rec.length, 1);
    assert.equal(rec[0].r, 22050);
    assert.ok(rec[0].n > 5000);
    await btn.click();
    assert.equal(await btn.innerText(), "Play");
    assert.deepEqual(seen.errors, []);
    await ctx.close();
});

test("the connection errors are shown in the page: no device, another device only (ports can be chosen), silent, not KAWA", async () => {
    // nothing plugged in
    let { ctx, page } = await open({ noDevice: true });
    await page.locator("#connect").click();
    await page.waitForSelector("#devmsg:not([hidden])");
    assert.match(await page.locator("#devmsg").innerText(), /No MIDI device is connected.*USB data cable/);
    assert.match(await page.locator("#devtext").innerText(), /Not connected/);
    await ctx.close();
    // only another synth: not a byte is sent to it, the user chooses the ports
    ({ ctx, page } = await open({ name: "Some Synth" }));
    await files(page, [["kick.wav", F.kick]]);
    await settled(page, 1);
    await page.locator("#connect").click();
    await page.waitForSelector("#devmsg:not([hidden])");
    assert.match(await page.locator("#devmsg").innerText(), /No MIDI port named FM-1, Felucca or KAWA/);
    assert.equal(await page.locator("#ports").evaluate(d => d.open), true);
    assert.deepEqual(await page.locator("#port-out option").allInnerTexts(), ["Some Synth"]);
    assert.equal(await page.evaluate(() => window.__dev.seen.length), 0, "nothing was sent to a device that is not an FM-1");
    await page.locator("#connect").click();
    await page.waitForFunction(() => /Connected to Some Synth/.test(document.getElementById("devtext").textContent));
    await ctx.close();
    // an FM-1 that does not answer (stock firmware, rescue screen)
    ({ ctx, page } = await open({ dev: { silentToKawa: true } }));
    await page.locator("#connect").click();
    await page.waitForSelector("#devmsg:not([hidden])");
    const t = await page.locator("#devmsg").innerText();
    assert.match(t, /did not answer/);
    assert.match(t, /KAWA 0\.2 or later/);
    assert.match(t, /KAWA USB RESCUE/);
    await shot(page, "samples-error");
    await ctx.close();
    // something that is not KAWA
    ({ ctx, page } = await open({ dev: { infoMagic: "ABCD" } }));
    await page.locator("#connect").click();
    await page.waitForSelector("#devmsg:not([hidden])");
    assert.match(await page.locator("#devmsg").innerText(), /not KAWA 0\.2 or later/);
    await ctx.close();
    // the permission is refused
    ({ ctx, page } = await open({ midi: { reject: true } }));
    await page.locator("#connect").click();
    await page.waitForSelector("#devmsg:not([hidden])");
    assert.match(await page.locator("#devmsg").innerText(), /MIDI access with SysEx was refused/);
    await ctx.close();
});

test("no Web MIDI in the browser: said at once; the sounds can still be prepared and heard; Connect and Send are off", async () => {
    const { ctx, page, seen } = await open({ noWebMidi: true });
    assert.match(await page.locator("#nomidi").innerText(), /cannot do Web MIDI.*Chrome or Edge/);
    assert.equal(await page.locator("#connect").isDisabled(), true);
    await files(page, [["kick.wav", F.kick]]);
    await settled(page, 1);
    assert.ok(await inkOf(page, 0) > 200);
    assert.equal(await page.locator("#send").isDisabled(), true);
    assert.match(await page.locator("#why").innerText(), /no Web MIDI/);
    assert.deepEqual(seen.errors, []);
    await ctx.close();
});

test("a cable pulled mid-transfer: the page says the slot is empty, the FM-1 holds nothing half written; a muted FM-1 too", async () => {
    let { ctx, page, seen } = await open({ dev: { faults: { disconnectAfter: { cmd: 3, count: 20 } } } });
    await files(page, [["long.wav", F.long]]);
    await settled(page, 1);
    await page.locator("#send").click();
    await page.waitForFunction(() => /Nothing was sent/.test(document.getElementById("summary").textContent), null, { timeout: 15000 });
    const t = await page.locator("#summary").innerText();
    assert.match(t, /The FM-1 was disconnected/);
    assert.match(t, /MY 1 is empty now/);
    assert.match(t, /plays again by itself within 10 seconds/);
    assert.match(await itemText(page, 0, ".imsg"), /MY 1 is empty now/);
    assert.equal(await page.locator(".item.failed").count(), 1);
    assert.equal(await devName(page, 0), null);
    assert.equal(await page.evaluate(() => window.__dev.violations.length), 0);
    assert.match(await page.locator("#devtext").innerText(), /unplugged|Not connected/);
    await ctx.close();
    ({ ctx, page, seen } = await open({ dev: { faults: { muteAfter: { cmd: 3, count: 10 } } } }));
    await files(page, [["long.wav", F.long]]);
    await settled(page, 1);
    await page.locator("#send").click();
    await page.waitForFunction(() => /Nothing was sent/.test(document.getElementById("summary").textContent), null, { timeout: 15000 });
    assert.match(await page.locator("#summary").innerText(), /stopped answering.*MY 1 is empty now/s);
    assert.equal(await devName(page, 0), null);
    await ctx.close();
});

test("Cancel stops the transfer between two frames; the slot stays empty; a batch that stops tells which ones were not sent", async () => {
    const { ctx, page } = await open({ midi: { latency: 3 } });
    await files(page, [["long.wav", F.long], ["kick.wav", F.kick]]);
    await settled(page, 2);
    await page.locator("#send").click();
    await page.waitForSelector("#cancel:not([hidden])");
    await page.waitForFunction(() => window.__dev.framesOf(3).length > 8);
    assert.equal(await page.locator("#send").isDisabled(), true, "Send is off while sending");
    assert.equal(await page.locator(".item").nth(0).locator("input.name").isDisabled(), true);
    await page.locator("#cancel").click();
    await page.waitForFunction(() => /Nothing was sent/.test(document.getElementById("summary").textContent), null, { timeout: 15000 });
    assert.match(await page.locator("#summary").innerText(), /Cancelled/);
    assert.equal(await devName(page, 0), null);
    assert.equal(await devName(page, 1), null, "the second sound was not started");
    assert.match(await itemText(page, 1, ".imsg"), /Not sent/);
    assert.equal(await page.locator("#cancel").isHidden(), true);
    assert.equal(await page.locator("#send").isDisabled(), false, "Send again is possible");
    await ctx.close();
});

test("Erase empties a slot after a question; the tiles follow", async () => {
    const { ctx, page } = await open();
    await files(page, [["kick.wav", F.kick]]);
    await settled(page, 1);
    await page.locator("#send").click();
    await page.waitForFunction(() => /^Done\./.test(document.getElementById("summary").textContent), null, { timeout: 15000 });
    const b = page.locator("#slots li").nth(0).locator("button");
    assert.equal(await b.innerText(), "Erase");
    await b.click();
    await page.waitForSelector("dialog[open]");
    assert.match(await page.locator("dialog").innerText(), /Erase MY 1\?/);
    await page.locator("#confirm-no").click();
    assert.equal(await devName(page, 0), "KICK");
    await b.click();
    await page.waitForSelector("dialog[open]");
    await page.locator("#confirm-yes").click();
    await page.waitForFunction(() => !window.__dev.usrData[0]);
    await page.waitForFunction(() => document.querySelector("#slots li").classList.contains("empty"));
    await ctx.close();
});

test("a phone: no sideways scrolling with four sounds, the slots in two columns, the buttons big enough to tap", async () => {
    const { ctx, page } = await open({ viewport: { width: 390, height: 800 } });
    await files(page, [["kick.wav", F.kick], ["snare.wav", F.snare], ["hat.wav", F.hat], ["stereo.wav", F.stereo]]);
    await settled(page, 4);
    await page.locator("#connect").click();
    await page.waitForFunction(() => /Connected/.test(document.getElementById("devtext").textContent));
    assert.equal(await noOverflow(page), true, "no horizontal scroll at 390 px");
    const cols = await page.locator("#slots").evaluate(e => getComputedStyle(e).gridTemplateColumns.split(" ").length);
    assert.equal(cols, 2);
    for (const sel of ["#connect", "#send", ".item .play", ".item .rm", ".item select.slot", ".item input.name"]) {
        const h = await page.locator(sel).first().evaluate(e => e.getBoundingClientRect().height);
        assert.ok(h >= 36, `${sel} is ${h}px high`);
    }
    await shot(page, "samples-phone");
    await ctx.close();
    const small = await open({ viewport: { width: 320, height: 700 } });
    await files(small.page, [["kick.wav", F.kick]]);
    await settled(small.page, 1);
    assert.equal(await noOverflow(small.page), true, "no horizontal scroll at 320 px");
    await small.ctx.close();
});

test("a desktop screenshot of a full session (when KAWA_SHOTS is set); no page ever asked the network for anything", async () => {
    const { ctx, page } = await open({ viewport: { width: 1180, height: 900 } });
    await files(page, [["kick.wav", F.kick], ["snare.wav", F.snare], ["hat.wav", F.hat], ["long.wav", F.long]]);
    await settled(page, 4);
    await page.locator("#connect").click();
    await page.waitForFunction(() => /Connected/.test(document.getElementById("devtext").textContent));
    await page.locator("#send").click();
    await page.waitForFunction(() => /^Done/.test(document.getElementById("summary").textContent), null, { timeout: 20000 });
    await shot(page, "samples-desktop");
    await ctx.close();
    assert.deepEqual(seenAll.flatMap(s => s.external), [], "no external request, ever");
});

(async () => {
    await new Promise(r => server.listen(0, "127.0.0.1", r));
    base = "http://127.0.0.1:" + server.address().port;
    browser = await chromium.launch();
    let pass = 0, fail = 0;
    const only = process.argv[2];
    for (const [name, fn] of tests) {
        if (only && !name.includes(only)) continue;
        const t0 = Date.now();
        try { await fn(); pass++; console.log(`ok       ${name}  (${Date.now() - t0} ms)`); }
        catch (e) { fail++; console.log(`FAILED   ${name}\n${String(e && e.stack || e).split("\n").slice(0, 14).map(l => "         " + l).join("\n")}`); }
    }
    await browser.close();
    server.close();
    console.log(`\n${pass} passed, ${fail} failed (Chromium ${browser.version ? "" : ""}headless)`);
    process.exitCode = fail ? 1 : 0;
})();

#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Omnipotard
/* The home page (index.html + install/webinstall.js) in a real Chromium, with the simulated FM-1 of tests/fm1_mock.js
 * standing in for Web MIDI (headless Chromium has no MIDI device): the page itself, the button, the confirm dialog, the
 * progress bar, the warnings while writing, the errors, file://. Needs Playwright and a Chromium:
 *
 *   NODE_PATH=$(npm root -g) node source/tests/webinstall_ui_test.js        (PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers here)
 *
 * Without Playwright it says so and exits 0. KAWA_SHOTS=DIR also saves screenshots (desktop and phone width).
 * It does not prove the real Web MIDI of Chrome, Edge or Opera: only a real FM-1 does. */
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const http = require("node:http");
const path = require("node:path");

let chromium;
try { ({ chromium } = require("playwright")); } catch (e) {
    console.log("webinstall_ui_test: SKIPPED, no Playwright (NODE_PATH=$(npm root -g) node source/tests/webinstall_ui_test.js)");
    process.exit(0);
}
const ROOT = path.join(__dirname, "..", "..");
const MOCK = fs.readFileSync(path.join(__dirname, "fm1_mock.js"), "utf8");
const SHOTS = process.env.KAWA_SHOTS;
const TYPES = { ".html": "text/html; charset=utf-8", ".js": "text/javascript", ".png": "image/png", ".fwsc": "application/octet-stream" };
const FAST = { open: 5, start: 20, reply: 0, loader: 20, reboot: 20, retry: 20, poll: 30, hs: 100, idle_check: 400, idle_write: 600, wait_loader: 3000, wait_reboot: 3000, portopen: 500, refresh: 50 };

const sums = fs.readFileSync(path.join(ROOT, "install", "SHA256SUMS"), "utf8");
const PKGNAME = require(path.join(ROOT, "install", "webinstall.js")).newestInSums(sums).name;      // the package the page installs
const PKG = fs.readFileSync(path.join(ROOT, "install", PKGNAME));

const server = http.createServer((req, res) => {                     // a static server of the repository root
    const u = new URL(req.url, "http://x");
    let f = path.join(ROOT, decodeURIComponent(u.pathname));
    if (u.pathname.endsWith("/")) f = path.join(f, "index.html");
    if (!f.startsWith(ROOT) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); return res.end("not found"); }
    res.writeHead(200, { "content-type": TYPES[path.extname(f)] || "text/plain" });
    res.end(fs.readFileSync(f));
});

// the simulated FM-1 on the page's Web MIDI, before the page's own scripts run
const mockInit = o => MOCK + `;(function () {
    var M = window.Fm1Mock, midi = new M.MockMidi(${JSON.stringify(o.midi || {})});
    window.__midi = midi;
    ${o.noDevice ? "" : `window.__dev = midi.add(new M.Fm1(${JSON.stringify(o.dev || {})}));`}
    ${o.noWebMidi ? `Object.defineProperty(Navigator.prototype, "requestMIDIAccess", { value: undefined, configurable: true });` :
      `Object.defineProperty(navigator, "requestMIDIAccess", { value: function (o) { return midi.requestMIDIAccess(o); }, configurable: true });`}
})();`;

let browser, base;
const tests = [];
const test = (name, fn) => tests.push([name, fn]);

async function open(o = {}, url) {
    const ctx = await browser.newContext({ viewport: o.viewport || { width: 1280, height: 900 }, acceptDownloads: false });
    if (!o.real) await ctx.addInitScript({ content: mockInit(o) });
    const page = await ctx.newPage();
    const seen = { external: [], errors: [], requests: [] };
    page.on("request", r => { seen.requests.push(r.url()); if (!/^(http:\/\/127\.0\.0\.1|file:|data:|blob:|about:)/.test(r.url())) seen.external.push(r.url()); });
    page.on("console", m => { if (m.type() === "error") seen.errors.push(m.text()); });
    page.on("pageerror", e => seen.errors.push(String(e)));
    if (o.before) await o.before(page);
    await page.goto(url || base + "/index.html");
    await page.evaluate(f => Object.assign(window.KawaInstall.DELAY, f), FAST);
    return { ctx, page, seen };
}
const text = (page, sel) => page.$eval(sel, e => e.textContent);
const visible = (page, sel) => page.$eval(sel, e => !e.hidden);
const appOk = page => page.evaluate(async name => {                  // the simulated chip's app area is the package's
    const raw = new Uint8Array(await (await fetch("install/" + name)).arrayBuffer());
    const exp = window.__dev.expectedApp(raw), a = window.__dev.appArea();
    return a.length === exp.length && a.every((x, i) => x === exp[i]) && window.__dev.untouched();
}, PKGNAME);
const stats = page => page.evaluate(() => JSON.parse(JSON.stringify(window.__dev.stats)));
async function shot(page, name) { if (SHOTS) { fs.mkdirSync(SHOTS, { recursive: true }); await page.screenshot({ path: path.join(SHOTS, name), fullPage: true }); } }

test("the page: content, image, links, no network, no console error", async () => {
    const { ctx, page, seen } = await open();
    assert.equal(await page.title(), "KAWA — Pocket Tracker for the M-VAVE FM-1");
    assert.equal(await text(page, "h1"), "KAWA — Pocket Tracker for the M-VAVE FM-1");
    assert.equal(await text(page, ".lead"), "A free tracker firmware for the M-VAVE FM-1. Write patterns on six tracks (FM, chiptune, drums, breaks, samples), chain them into a song, and play live effects. Beta, GPL-3.0.");
    const warn = await text(page, ".warn");
    assert.match(warn, /Not yet tried on a real FM-1/); assert.match(warn, /unofficial firmware/); assert.match(warn, /at your own risk/); assert.match(warn, /M-UPGRADE/);
    assert.match(await text(page, "#browsers"), /Chrome, Edge or Opera/); assert.match(await text(page, "#browsers"), /Firefox and Safari/);
    assert.match(await text(page, "#browsers"), /INSTALL-Windows\.bat/);
    assert.match(await text(page, "#official"), /M-UPGRADE/);
    assert.equal(await page.$eval("#official a", a => a.getAttribute("href")), "https://github.com/Guizmoh/Kawa-Tracker-Fm1#back-to-the-official-firmware");
    assert.equal(await page.$eval(".hero img", i => [i.getAttribute("src"), i.complete && i.naturalWidth]).then(x => x.join(" ")), "manual/img/kawa-preview.png 1280");
    const hrefs = await page.$$eval("a[href]", a => a.map(x => x.getAttribute("href")));
    for (const h of ["manual/index.html", "manual/fr.html", "manual/es.html", "emulator/index.html", "samples.html", "https://github.com/Guizmoh/Kawa-Tracker-Fm1", "https://github.com/Guizmoh/Kawa-Tracker-Fm1#readme"]) assert.ok(hrefs.includes(h), h);
    assert.equal(await page.$eval("#go", b => b.textContent.trim() + "|" + b.disabled), "Install KAWA|false");
    assert.equal(await text(page, "#status"), "Ready.");
    assert.match(await text(page, "#pkg"), /^Package: kawa-/);
    assert.deepEqual(seen.external, [], "something was loaded from the network");
    assert.deepEqual(seen.errors, []);
    // no horizontal scroll at desktop and phone width
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
    await shot(page, "desktop.png");
    await page.setViewportSize({ width: 390, height: 844 });
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), "horizontal scroll on a phone");
    await shot(page, "phone.png");
    await ctx.close();
});

test("a browser without Web MIDI: the message, the buttons off", async () => {
    const { ctx, page } = await open({ noWebMidi: true });
    assert.equal(await page.$eval("#go", b => b.disabled), true);
    assert.equal(await text(page, "#status"), "This browser cannot install KAWA.");
    assert.match(await text(page, "#error"), /^Error: this browser has no Web MIDI with SysEx, which the installer needs to talk to the FM-1\. Use Chrome, Edge or Opera on a computer/);
    assert.ok(await visible(page, "#error"));
    await ctx.close();
});

test("install: cancel in the dialog writes nothing; confirm writes; the guards while writing; done message", async () => {
    const { ctx, page, seen } = await open();
    await page.click("#go");
    await page.waitForFunction(() => document.getElementById("dlg").open);
    const info = await text(page, "#dlg");
    assert.match(info, /Write KAWA to the FM-1\?/); assert.match(info, /Do not unplug the FM-1/); assert.match(info, /do not close or reload this page/);
    assert.match(await text(page, "#dlg-info"), /^Package FM-1_956 \(kawa-[\d.]+\.fwsc\) onto the device FM-1_015 on the port "FM-1"\.$/);
    assert.equal(await page.evaluate(() => document.getElementById("dlg-no") === document.activeElement), true, "Cancel has the focus, not the write button");
    await shot(page, "dialog.png");
    await page.click("#dlg-no");
    await page.waitForFunction(() => document.getElementById("status").textContent.startsWith("Cancelled"));
    assert.equal(await text(page, "#status"), "Cancelled. Nothing was written.");
    assert.equal((await stats(page)).upgrades, 0);
    assert.equal(await page.$eval("#go", b => b.disabled), false);
    // again, and yes
    await page.click("#go");
    await page.waitForFunction(() => document.getElementById("dlg").open);
    assert.equal(await page.evaluate(() => { const e = new Event("beforeunload", { cancelable: true }); dispatchEvent(e); return e.defaultPrevented; }), false, "no warning before writing");
    await page.click("#dlg-yes");
    await page.waitForFunction(() => +(document.getElementById("bar").getAttribute("aria-valuenow") || 0) >= 25, null, { timeout: 60000 });
    const mid = await page.evaluate(() => {
        const e = new Event("beforeunload", { cancelable: true }); dispatchEvent(e);
        return { guard: e.defaultPrevented, keep: !document.getElementById("keep").hidden, go: document.getElementById("go").disabled,
                 pick: document.getElementById("pick").className, status: document.getElementById("status").textContent, writing: document.body.className };
    });
    assert.equal(mid.guard, true, "beforeunload must warn while writing");
    assert.equal(mid.keep, true); assert.equal(mid.go, true); assert.match(mid.pick, /off/); assert.match(mid.status, /^Writing \d+%$/); assert.equal(mid.writing, "writing");
    await shot(page, "writing.png");
    await page.waitForSelector("#done:not([hidden])", { timeout: 60000 });
    assert.equal(await text(page, "#done"), "Done: the FM-1 runs KAWA (FM-1_956).");
    assert.equal(await text(page, "#status"), "Done: the FM-1 runs KAWA (FM-1_956).");
    const end = await page.evaluate(() => {
        const e = new Event("beforeunload", { cancelable: true }); dispatchEvent(e);
        return { guard: e.defaultPrevented, keep: !document.getElementById("keep").hidden, go: document.getElementById("go").disabled,
                 bar: document.getElementById("bar").getAttribute("aria-valuenow"), log: document.getElementById("log").textContent };
    });
    assert.deepEqual([end.guard, end.keep, end.go, end.bar], [false, false, false, "100"], "guards released at the end");
    assert.match(end.log, /^package: FM-1_956 {2}\(kawa-/); assert.match(end.log, /device: {2}FM-1_015 {2}\(FM-1\)/);
    assert.match(end.log, /Starting the update on FM-1_015\nSwitching to update mode\nRestarting\ndone: the FM-1 runs FM-1_956$/);
    assert.equal(await appOk(page), true, "the simulated chip does not hold the package");
    const st = await stats(page);
    assert.equal(st.finishes, 1); assert.equal(st.upgrades, 2);
    assert.deepEqual(await page.evaluate(() => window.__dev.protocolErrors), []);
    assert.equal(await text(page, "#error"), ""); assert.deepEqual(seen.errors, []); assert.deepEqual(seen.external, []);
    assert.ok(seen.requests.some(u => u.endsWith("/install/SHA256SUMS")) && seen.requests.some(u => u.endsWith("/install/" + PKGNAME)), "the package is fetched by a relative URL");
    await shot(page, "done.png");
    await ctx.close();
});

test("closing the tab while writing: the browser asks first (the real beforeunload prompt); staying lets it finish", async () => {
    const { ctx, page } = await open();
    await page.evaluate(() => { window.KawaInstall.DELAY.reply = 4; });             // slow enough to be mid-write for a few seconds
    await page.click("#go");
    await page.waitForFunction(() => document.getElementById("dlg").open);
    await page.click("#dlg-yes");
    await page.waitForFunction(() => +(document.getElementById("bar").getAttribute("aria-valuenow") || 0) >= 8, null, { timeout: 60000 });
    const asked = new Promise(res => page.once("dialog", d => { res(d.type()); d.dismiss(); }));
    await page.close({ runBeforeUnload: true });
    assert.equal(await Promise.race([asked, new Promise(r => setTimeout(() => r("no prompt"), 10000))]), "beforeunload", "closing the tab while writing must ask first");
    assert.equal(page.isClosed(), false, "the tab stays when the user says stay");
    await page.evaluate(() => { window.KawaInstall.DELAY.reply = 0; });
    await page.waitForSelector("#done:not([hidden])", { timeout: 60000 });
    assert.equal(await appOk(page), true);
    await ctx.close();
});

test("a newer package named in install/SHA256SUMS (0.6) is the one the page shows, downloads and installs: no version in the page", async () => {
    const hash = require("node:crypto").createHash("sha256").update(PKG).digest("hex");
    const { ctx, page, seen } = await open({ before: async pg => {
        await pg.route("**/install/SHA256SUMS", r => r.fulfill({ contentType: "text/plain", body: `${hash}  kawa-0.6.fwsc\n${"c".repeat(64)}  kawa-0.4.fwsc\n${"d".repeat(64)}  older/kawa-0.3.fwsc\n` }));
        await pg.route("**/install/kawa-0.6.fwsc", r => r.fulfill({ contentType: "application/octet-stream", body: PKG }));
    } });
    assert.equal(await text(page, "#pkg"), "Package: kawa-0.6.fwsc");
    await page.click("#go");
    await page.waitForFunction(() => document.getElementById("dlg").open);
    assert.match(await text(page, "#dlg-info"), /\(kawa-0\.6\.fwsc\)/);
    await page.click("#dlg-yes");
    await page.waitForSelector("#done:not([hidden])", { timeout: 60000 });
    assert.equal(await text(page, "#done"), "Done: the FM-1 runs KAWA (FM-1_956).");
    assert.ok(seen.requests.some(u => u.endsWith("/install/kawa-0.6.fwsc")) && !seen.requests.some(u => u.endsWith("/install/kawa-0.4.fwsc")));
    await ctx.close();
});

test("a local damaged .fwsc: refused, nothing written, no MIDI asked; then a good local file installs", async () => {
    const { ctx, page } = await open();
    const bad = Buffer.from(PKG); bad[0x40000 + 20] ^= 1;
    await page.setInputFiles("#file", { name: "kawa-damaged.fwsc", mimeType: "application/octet-stream", buffer: bad });
    await page.waitForFunction(() => !document.getElementById("error").hidden);
    assert.match(await text(page, "#error"), /^Error: kawa-damaged\.fwsc: damaged package \(flash\.bin: CRC\); nothing was written\. Download it again\.$/);
    assert.equal(await text(page, "#status"), "Stopped. Nothing was written.");
    assert.equal(await page.evaluate(() => window.__midi.requests), 0);
    assert.equal(await page.$eval("#go", b => b.disabled), false);
    await page.setInputFiles("#file", { name: "kawa-local.fwsc", mimeType: "application/octet-stream", buffer: PKG });
    await page.waitForFunction(() => document.getElementById("dlg").open);
    assert.match(await text(page, "#dlg-info"), /\(kawa-local\.fwsc\)/);
    await page.click("#dlg-yes");
    await page.waitForSelector("#done:not([hidden])", { timeout: 60000 });
    assert.equal(await text(page, "#done"), "Done: the FM-1 runs FM-1_956.", "a local file does not claim to be KAWA");
    assert.equal(await appOk(page), true);
    assert.equal(await page.evaluate(() => document.getElementById("error").hidden), true, "the old error is gone");
    await ctx.close();
});

test("the cable is cut mid-write: the error shows, the guards go; replugged, a second click finishes", async () => {
    const { ctx, page } = await open({ dev: { cutAtSector: 60 } });
    await page.click("#go");
    await page.waitForFunction(() => document.getElementById("dlg").open);
    await page.click("#dlg-yes");
    await page.waitForFunction(() => !document.getElementById("error").hidden, null, { timeout: 60000 });
    assert.match(await text(page, "#error"), /^Error: the loader was disconnected after \d+ requests\. Replug the USB cable and run the install again to finish$/);
    assert.equal(await text(page, "#status"), "Stopped.");
    assert.equal(await page.evaluate(() => { const e = new Event("beforeunload", { cancelable: true }); dispatchEvent(e); return e.defaultPrevented; }), false);
    assert.equal(await page.$eval("#go", b => b.disabled), false);
    assert.equal(await page.evaluate(() => window.__dev.record), true);
    await page.evaluate(() => { window.__dev.o.cutAtSector = -1; window.__dev.replug(); });
    await page.click("#go");
    await page.waitForFunction(() => document.getElementById("dlg").open);
    assert.ok(await visible(page, "#dlg-resume"), "the dialog says the write will be finished");
    await page.click("#dlg-yes");
    await page.waitForSelector("#done:not([hidden])", { timeout: 60000 });
    assert.equal(await appOk(page), true);
    assert.ok((await stats(page)).sectorsSkipped >= 60);
    await ctx.close();
});

test("no FM-1 / wrong model: the message, no dialog, nothing written", async () => {
    let t = await open({ noDevice: true });
    await t.page.click("#go");
    await t.page.waitForFunction(() => !document.getElementById("error").hidden);
    assert.equal(await text(t.page, "#error"), "Error: FM-1 not found (USB data cable? another app using it?)\n  MIDI inputs: (none)");
    assert.equal(await t.page.evaluate(() => document.getElementById("dlg").open), false);
    await t.ctx.close();
    t = await open({ dev: { appId: "FM-2_015" } });
    await t.page.click("#go");
    await t.page.waitForFunction(() => !document.getElementById("error").hidden);
    assert.match(await text(t.page, "#error"), /^Error: the device is FM-2_015, the package is for FM-1_956$/);
    assert.equal(await t.page.evaluate(() => document.getElementById("dlg").open), false);
    assert.equal((await stats(t.page)).upgrades, 0);
    await t.ctx.close();
});

test("Advanced: check the connection, and a port filter", async () => {
    const { ctx, page } = await open();
    await page.click("summary");
    await page.click("#probe");
    await page.waitForFunction(() => document.getElementById("status").textContent.startsWith("FM-1 found"));
    assert.equal(await text(page, "#status"), 'FM-1 found on the port "FM-1": FM-1_015, running.');
    await page.fill("#port", "nothing-like-this");
    await page.click("#probe");
    await page.waitForFunction(() => !document.getElementById("error").hidden);
    assert.match(await text(page, "#error"), /^Error: no MIDI port matching 'nothing-like-this' answered/);
    await ctx.close();
});

test("from file://: the page works, the download cannot, a local file installs", async () => {
    const { ctx, page, seen } = await open({}, "file://" + path.join(ROOT, "index.html"));
    assert.match(await text(page, "#pkg"), /^Opened from a file: the page cannot download the package/);
    assert.equal(await page.$eval(".hero img", i => i.complete && i.naturalWidth), 1280);
    await page.click("#go");
    await page.waitForFunction(() => !document.getElementById("error").hidden);
    assert.match(await text(page, "#error"), /^Error: cannot read install\/SHA256SUMS to find the newest package \(.*\); opened from a file, or offline\? Choose the \.fwsc file from the install folder instead$/);
    assert.equal(await page.evaluate(() => window.__midi.requests), 0);
    await page.setInputFiles("#file", { name: PKGNAME, mimeType: "application/octet-stream", buffer: PKG });
    await page.waitForFunction(() => document.getElementById("dlg").open);
    await page.click("#dlg-yes");
    await page.waitForSelector("#done:not([hidden])", { timeout: 60000 });
    assert.equal(await text(page, "#done"), "Done: the FM-1 runs FM-1_956.");
    assert.deepEqual(seen.external, []);
    await ctx.close();
});

(async () => {
    await new Promise(r => server.listen(0, "127.0.0.1", r));
    base = "http://127.0.0.1:" + server.address().port;
    browser = await chromium.launch();
    console.log(`webinstall_ui_test: ${await browser.version()} (Chromium), package ${PKGNAME}, ${base}`);
    let pass = 0, fail = 0;
    for (const [name, fn] of tests) {
        const t0 = Date.now();
        try { await fn(); console.log(`ok   - ${name} (${Date.now() - t0} ms)`); pass++; }
        catch (e) { console.log(`FAIL - ${name}\n       ${String((e && e.stack) || e).split("\n").slice(0, 8).join("\n       ")}`); fail++; }
    }
    console.log(`\n${pass} passed, ${fail} failed`);
    await browser.close();
    server.close();
    process.exit(fail ? 1 : 0);
})().catch(e => { console.error(e); process.exit(1); });

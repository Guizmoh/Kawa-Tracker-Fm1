// every factory preset: two notes (G1, C3) held 1.2 s, released 0.6 s -> peak, RMS, clipped samples, DC
import fs from "fs";
const { instance } = await WebAssembly.instantiate(fs.readFileSync("build/web/ksub.wasm"), {});
const x = instance.exports;
x.ksub_init();
const blocks = s => Math.round(s * 44100 / 256);
function render(nb, acc) {
  for (let b = 0; b < nb; b++) {
    const f = new Float32Array(x.memory.buffer, x.ksub_audio(), 512);
    for (let i = 0; i < 512; i++) { const v = f[i]; acc.pk = Math.max(acc.pk, Math.abs(v)); acc.ss += v * v; acc.dc += v; acc.n++; if (Math.abs(v) > 0.999) acc.clip++; if (!isFinite(v)) acc.nan++; }
    x.ksub_step();
  }
}
const NAME = p => { const m = new Uint8Array(x.memory.buffer, p, 14); let s = ""; for (const c of m) { if (!c) break; s += String.fromCharCode(c); } return s; };
for (let k = 0; k < x.ksub_nfactory(); k++) {
  x.ksub_load(0, k);
  const acc = { pk: 0, ss: 0, dc: 0, n: 0, clip: 0, nan: 0 };
  for (const note of [31, 48]) {
    x.ksub_midi(0x90, note, 100); render(blocks(1.2), acc);
    x.ksub_midi(0x80, note, 0); render(blocks(0.6), acc);
  }
  x.ksub_midi(0xB0, 123, 0); render(blocks(1.5), { pk: 0, ss: 0, dc: 0, n: 0, clip: 0, nan: 0 });
  console.log(NAME(x.ksub_patch_name()).padEnd(14), "peak", acc.pk.toFixed(2), "rms", Math.sqrt(acc.ss / acc.n).toFixed(3), "clip", acc.clip, "dc", (acc.dc / acc.n).toFixed(4), acc.nan ? "NaN!" : "");
}

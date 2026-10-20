// K-WA SUB without a browser: runs build/web/ksub.wasm, plays a script, writes screens (raw RGB565) and a WAV.
//   node tests/shots.mjs [script.txt] -> build/shots/*.raw, build/shots/out.wav (tests/png.py makes the PNGs)
// Script lines: "ms btn ID" (tap), "ms down ID" / "ms up ID" (held), "ms on KEY" / "ms off KEY", "ms enc ID STEPS", "ms shot NAME", "ms end"
import fs from "fs";
const wasm = fs.readFileSync(process.env.WASM || new URL("../build/web/ksub.wasm", import.meta.url));
const { instance } = await WebAssembly.instantiate(wasm, {});
const x = instance.exports;
const script = fs.readFileSync(process.argv[2] || new URL("tour.txt", import.meta.url), "utf8")
  .split("\n").map(l => l.trim()).filter(l => l && !l.startsWith("#")).map(l => l.split(/\s+/));
fs.mkdirSync("build/shots", { recursive: true });
x.ksub_init();
let ms = 0, keys = 0, peak = 0, clips = 0;
const pcm = [];
function run(until) {
  while (ms < until) {
    const p = x.ksub_audio(), f = new Float32Array(x.memory.buffer, p, 512);
    for (let i = 0; i < 512; i++) { const v = f[i]; if (Math.abs(v) > peak) peak = Math.abs(v); if (Math.abs(v) > 0.999) clips++; pcm.push(v); }
    x.ksub_step(); x.ksub_step();
    ms += 256 / 44.1;
  }
}
for (const [t, cmd, a, b, ...rest] of script) {
  run(+t);
  if (cmd === "btn") { x.ksub_button(+a, 1); x.ksub_step(); x.ksub_button(+a, 0); }
  else if (cmd === "down") x.ksub_button(+a, 1);
  else if (cmd === "up") x.ksub_button(+a, 0);
  else if (cmd === "on") { keys |= 1 << +a; x.ksub_keys(keys); }
  else if (cmd === "off") { keys &= ~(1 << +a); x.ksub_keys(keys); }
  else if (cmd === "enc") x.ksub_turn(+a, +b);
  else if (cmd === "midi") x.ksub_midi(+a, +b, +rest[0]);
  else if (cmd === "shot") { run(ms + 80); fs.writeFileSync(`build/shots/${a}.raw`, Buffer.from(new Uint8Array(x.memory.buffer, x.ksub_screen(), 115200))); }
  else if (cmd === "end") break;
}
const n = pcm.length, buf = Buffer.alloc(44 + n * 2);
buf.write("RIFF", 0); buf.writeUInt32LE(36 + n * 2, 4); buf.write("WAVEfmt ", 8); buf.writeUInt32LE(16, 16); buf.writeUInt16LE(1, 20);
buf.writeUInt16LE(2, 22); buf.writeUInt32LE(44100, 24); buf.writeUInt32LE(176400, 28); buf.writeUInt16LE(4, 32); buf.writeUInt16LE(16, 34);
buf.write("data", 36); buf.writeUInt32LE(n * 2, 40);
for (let i = 0; i < n; i++) buf.writeInt16LE(Math.max(-32767, Math.min(32767, Math.round(pcm[i] * 32767))), 44 + i * 2);
fs.writeFileSync("build/shots/out.wav", buf);
console.log(`${(ms / 1000).toFixed(1)} s, peak ${peak.toFixed(3)}, clipped samples ${clips}`);

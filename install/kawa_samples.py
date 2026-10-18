#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Put your own sounds into KAWA's four sample slots, over USB (the FM-1 running KAWA 0.2 or later).

  python3 install/kawa_samples.py list
  python3 install/kawa_samples.py put 1 kick.wav [--name KICK] [--rate 11025]
  python3 install/kawa_samples.py erase 1
  python3 install/kawa_samples.py script 1 kick.wav out.txt     (frames for the simulator, source/tests/host.c)

The sound is mixed to mono, resampled to 22050 Hz (or 11025: twice as long), normalised and stored as 8-bit
mu-law, as KAWA's breaks: at most 36608 bytes, 1.66 s at 22050 Hz, 3.3 s at 11025 Hz (longer sounds are cut).
WAV files are read directly; other formats if the soundfile module is there. Then, on the FM-1: a track's
SOUND page, engine SAMPLE, SOURCE = MY 1 .. MY 4. KAWA must be stopped (not playing) while a sound goes in.
Needs rtmidi2 (pip install rtmidi2), or mido with python-rtmidi, to talk to the FM-1.

The protocol (firmware/src/kawa_sysex.c): F0 7D 4B 57 cmd pack7(args) F7, one request, one reply."""
import argparse
import queue
import struct
import sys
import time
import zlib
from pathlib import Path

import numpy as np

CMD = {"INFO": 1, "BEGIN": 2, "WRITE": 3, "END": 4, "ERASE": 5, "LIST": 6}
RC = {0: "ok", 1: "bad arguments", 2: "busy: stop KAWA first", 3: "flash error", 4: "CRC mismatch",
      5: "out of order / no session"}
CAP = 0x9000 - 0x100
MAGIC = 0x504D534B


def pack7(b):                                   # 7 bits at a time, the low ones first (ota.c, kawa_sysex.c)
    out, acc, nb = [], 0, 0
    for x in b:
        acc |= x << nb
        nb += 8
        while nb >= 7:
            out.append(acc & 0x7F)
            acc >>= 7
            nb -= 7
    if nb:
        out.append(acc & 0x7F)
    return bytes(out)


def unpack7(b):
    out, acc, nb = [], 0, 0
    for x in b:
        acc |= (x & 0x7F) << nb
        nb += 7
        if nb >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nb -= 8
    return bytes(out)


def frame(cmd, payload=b""):
    return bytes([0xF0, 0x7D, 0x4B, 0x57, CMD[cmd]]) + pack7(payload) + b"\xF7"


def parse_reply(msg):
    if len(msg) < 6 or list(msg[:4]) != [0xF0, 0x7D, 0x4B, 0x57] or msg[-1] != 0xF7:
        return None
    return msg[4], unpack7(msg[5:-1])


def mulaw(x):                                   # as tools/gen_breaks.py (KAWA decodes with BRK_ULAW)
    mu = 255.0
    y = np.sign(x) * np.log1p(mu * np.abs(x)) / np.log1p(mu)
    return np.clip(np.round((y + 1) * 127.5), 0, 255).astype(np.uint8)


def riff_wav(path):
    """a WAV file read here: PCM 8/16/24/32 bits, float 32/64 bits (a DAW's export), WAVE_FORMAT_EXTENSIBLE"""
    import struct
    d = Path(path).read_bytes()
    if len(d) < 12 or d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        raise ValueError("not a RIFF WAVE file")
    fmt = data = None
    i = 12
    while i + 8 <= len(d):
        cid, size = d[i:i + 4], struct.unpack("<I", d[i + 4:i + 8])[0]
        body = d[i + 8:i + 8 + size]
        if cid == b"fmt ":
            fmt = body
        elif cid == b"data":
            data = body
        i += 8 + size + (size & 1)
    if fmt is None or data is None or len(fmt) < 16:
        raise ValueError("no fmt or data chunk")
    tag, ch, sr, _, _, bits = struct.unpack("<HHIIHH", fmt[:16])
    if tag == 0xFFFE and len(fmt) >= 26:            # extensible: the sub-format's first two bytes
        tag = struct.unpack("<H", fmt[24:26])[0]
    if ch < 1:
        raise ValueError("no channel")
    if tag == 3 and bits in (32, 64):
        a = np.frombuffer(data[:len(data) // (bits // 8) * (bits // 8)], "<f4" if bits == 32 else "<f8").astype(np.float64)
    elif tag == 1 and bits == 8:
        a = (np.frombuffer(data, np.uint8).astype(np.float64) - 128) / 128
    elif tag == 1 and bits == 16:
        a = np.frombuffer(data[:len(data) // 2 * 2], "<i2").astype(np.float64) / 32768
    elif tag == 1 and bits == 24:
        b = np.frombuffer(data[:len(data) // 3 * 3], np.uint8).reshape(-1, 3).astype(np.int32)
        a = ((b[:, 0] | b[:, 1] << 8 | b[:, 2] << 16) << 8 >> 8).astype(np.float64) / 8388608
    elif tag == 1 and bits == 32:
        a = np.frombuffer(data[:len(data) // 4 * 4], "<i4").astype(np.float64) / 2147483648
    else:
        raise ValueError(f"WAV format {tag}, {bits} bits")
    a = a[:len(a) // ch * ch]
    return np.nan_to_num(a.reshape(-1, ch).mean(axis=1)), sr


def load(path):
    path = Path(path)
    err = None
    if path.suffix.lower() in (".wav", ".wave"):
        try:
            return riff_wav(path)
        except (ValueError, OSError) as e:          # (then soundfile, if it is there)
            err = e
    try:
        import soundfile
    except ImportError:
        why = f" ({err})" if err else ""
        raise SystemExit(f"{path}: cannot read it{why}; convert it to a WAV, or pip install soundfile")
    a, sr = soundfile.read(str(path), always_2d=True)
    return a.mean(axis=1), sr


def prepare(path, rate=22050):
    a, sr = load(path)
    if sr != rate:                              # linear resampling, after a gentle low-pass when going down
        if sr > rate:
            k = max(1, int(round(sr / rate)))
            a = np.convolve(a, np.ones(k) / k, mode="same")
        t = np.arange(0, len(a) * rate / sr) * sr / rate
        a = np.interp(t, np.arange(len(a)), a)
    nz = np.nonzero(np.abs(a) > 0.003)[0]       # the silence before the sound goes
    if len(nz):
        a = a[nz[0]:]
    a = a[:CAP]
    if len(a) < 64:
        raise SystemExit(f"{path}: too short")
    peak = np.max(np.abs(a))
    if peak > 0:
        a = a * (0.95 / peak)
    n = len(a)
    fade = min(64, n // 4)                      # no click at the end
    a[n - fade:] *= np.linspace(1, 0, fade)
    return mulaw(a).tobytes()


def header(data, rate, name):
    nm = name.upper().encode("ascii", "replace")[:11].ljust(12, b"\0")
    return struct.pack("<III12sIII", MAGIC, len(data), rate, nm, zlib.crc32(data) & 0xFFFFFFFF, 0, 0)


def upload_frames(slot, data, rate, name):
    yield "BEGIN", frame("BEGIN", bytes([slot]))
    for off in range(0, len(data), 256):
        yield "WRITE", frame("WRITE", bytes([slot, off & 0xFF, off >> 8 & 0xFF, off >> 16]) + data[off:off + 256])
    yield "END", frame("END", bytes([slot]) + header(data, rate, name))


class Device:
    """the FM-1's MIDI ports: mido with python-rtmidi (Python 3.8 .. 3.12), else rtmidi2 (3.10 .. 3.14)"""

    def __init__(self):
        pick = lambda names: next((n for n in names if "FM-1" in n or "Felucca" in n or "KAWA" in n), None)
        self.q = queue.Queue()
        try:
            import mido
            import rtmidi  # noqa: F401  (python-rtmidi)
            outs, ins = mido.get_output_names(), mido.get_input_names()
            o, i = pick(outs), pick(ins)
            if not o or not i:
                raise SystemExit("no FM-1 found among the MIDI ports: " + ", ".join(outs))
            out = mido.open_output(o)
            mido.open_input(i, callback=lambda m: m.type == "sysex" and self.q.put(bytes([0xF0, *m.data, 0xF7])))
            self.send = lambda msg: out.send(mido.Message("sysex", data=list(msg[1:-1])))
            return
        except ImportError:
            pass
        try:
            import rtmidi2
        except ImportError:
            raise SystemExit("needs rtmidi2 (pip install rtmidi2), or mido with python-rtmidi")
        outs, ins = rtmidi2.get_out_ports(), rtmidi2.get_in_ports()
        o, i = pick(outs), pick(ins)
        if not o or not i:
            raise SystemExit("no FM-1 found among the MIDI ports: " + ", ".join(outs))
        self.inp = rtmidi2.MidiIn("kawa_samples")
        self.inp.ignore_types(midi_sysex=False, midi_time=True, midi_sense=True)
        self.inp.open_port(ins.index(i))
        self.inp.callback = lambda m, _t: m and m[0] == 0xF0 and self.q.put(bytes(m))
        self.out = rtmidi2.MidiOut("kawa_samples")
        self.out.open_port(outs.index(o))
        self.send = lambda msg: self.out.send_raw(*msg)

    def ask(self, cmd, msg, timeout=2.0, tries=2):
        for _ in range(tries):
            self.send(msg)
            t0 = time.time()
            while time.time() - t0 < timeout:
                try:
                    r = parse_reply(self.q.get(timeout=0.05))
                except queue.Empty:
                    continue
                if r and r[0] == CMD[cmd]:
                    return r[1]
        raise SystemExit(f"{cmd}: no answer (is it KAWA 0.2 or later, and stopped?)")


def show_list(body):
    if body[0]:
        raise SystemExit("LIST: " + RC.get(body[0], str(body[0])))
    for k in range(4):
        q = body[1 + 18 * k:19 + 18 * k]
        if q[0]:
            rate, n = q[1] | q[2] << 8, q[3] | q[4] << 8 | q[5] << 16
            print(f"MY {k + 1}: {q[6:18].split(b'\0')[0].decode()}  {n} B, {rate} Hz, {n / rate:.2f} s")
        else:
            print(f"MY {k + 1}: empty")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    p = sub.add_parser("put")
    p.add_argument("slot", type=int, choices=range(1, 5))
    p.add_argument("file")
    p.add_argument("--name")
    p.add_argument("--rate", type=int, default=22050, choices=(22050, 11025))
    e = sub.add_parser("erase")
    e.add_argument("slot", type=int, choices=range(1, 5))
    s = sub.add_parser("script")
    s.add_argument("slot", type=int, choices=range(1, 5))
    s.add_argument("file")
    s.add_argument("out")
    s.add_argument("--name")
    s.add_argument("--rate", type=int, default=22050, choices=(22050, 11025))
    s.add_argument("--start", type=int, default=3000, help="ms of the first frame")
    a = ap.parse_args()
    if a.cmd == "script":
        data = prepare(a.file, a.rate)
        t = a.start
        with open(a.out, "w") as f:
            f.write(f"# {a.file} into MY {a.slot} ({len(data)} B)\n")
            for name, msg in upload_frames(a.slot - 1, data, a.rate, a.name or Path(a.file).stem):
                f.write(f"{t} sysex {msg.hex().upper()}\n")
                t += 3
        print(f"{a.out}: {len(data)} B, ends at {t} ms")
        return
    dev = Device()
    info = dev.ask("INFO", frame("INFO"))
    if info[:5] != b"\0KAWA":
        raise SystemExit("the FM-1 does not run KAWA 0.2 or later")
    if a.cmd == "list":
        show_list(dev.ask("LIST", frame("LIST")))
    elif a.cmd == "erase":
        r = dev.ask("ERASE", frame("ERASE", bytes([a.slot - 1])), timeout=5)
        print("erase:", RC.get(r[0], r[0]))
    else:
        data = prepare(a.file, a.rate)
        frames = list(upload_frames(a.slot - 1, data, a.rate, a.name or Path(a.file).stem))
        for i, (name, msg) in enumerate(frames):
            r = dev.ask(name, msg, timeout=3 if name != "WRITE" else 1.5)
            if r[0]:
                raise SystemExit(f"{name}: {RC.get(r[0], r[0])}")
            print(f"\r{i + 1}/{len(frames)}", end="", flush=True)
        print(f"\nMY {a.slot}: {len(data)} B at {a.rate} Hz ({len(data) / a.rate:.2f} s)")


if __name__ == "__main__":
    main()

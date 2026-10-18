#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check KAWA's breakbeats (firmware/gen/kawa_breaks.h, from tools/gen_breaks.py).

Decodes every break as the firmware does (BRK_ULAW), writes build/breaks/<name>.wav (four loops, to
listen), prints per break its level, tone, onsets per 16th and swing, then how far apart the breaks
are, as a whole and chop by chop. Fails if two breaks are too alike, if one clips or has DC, or if
BRK_TAG does not match what is heard (a kick on the one, snares on 2 and 4, tags on real hits).

  python3 tests/breaks_check.py [--header H] [--wav DIR] [--no-wav]   (needs numpy)
"""
import argparse
import re
import sys
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
STEPS = 16
FRAME, HOP = 512, 63                    # 33075 = 525 hops of 63: the frames wrap round the loop
BANDS = (("K", 40, 160), ("B", 160, 1200), ("S", 1200, 5000), ("H", 5000, 11025))   # kick, body, snare, hats
# too alike: below these, a pair fails (see distance())
MIN_SCORE = 1.0
MIN_CHOP = 1.25


def load(path):
    src = path.read_text()
    d = {k: int(v) for k, v in re.findall(r"#define (BRK_\w+) (\d+)u", src)}
    n, ln = d["BRK_N"], d["BRK_LEN"]

    def body(name):
        m = re.search(name + r"[^=]*=\s*\{", src)
        i, depth = m.end(), 1
        j = i
        while depth:
            depth += {"{": 1, "}": -1}.get(src[j], 0)
            j += 1
        return src[i:j - 1]
    ulaw = np.array([int(v) for v in re.findall(r"-?\d+", body("BRK_ULAW"))], dtype=np.int32)
    names = re.findall(r'"([^"]*)"', body("BRK_NAME"))
    tags = np.array([int(v) for v in re.findall(r"\d+", body("BRK_TAG"))]).reshape(n, STEPS)
    data = np.array(re.findall(r"\d+", body("BRK_DATA")), dtype=np.int32)
    if len(ulaw) != 256 or len(names) != n or data.size != n * ln:
        raise SystemExit(f"{path}: {len(ulaw)} table entries, {len(names)} names, {data.size} samples "
                         f"(want 256, {n}, {n * ln})")
    return d, ulaw, names, tags, data.reshape(n, ln)


def stft(x):
    """magnitude frames, centred on k * HOP, wrapping round the loop"""
    n = len(x)
    win = np.hanning(FRAME)
    idx = (np.arange(n // HOP)[:, None] * HOP + np.arange(FRAME)[None, :] - FRAME // 2) % n
    return np.abs(np.fft.rfft(x[idx] * win, axis=1))


def band_flux(mag, sr):
    """onset strength per band: the rise of the (square-rooted) spectrum over two frames, less the
    band's floor (hiss, crackle and room move a little all the time)"""
    f = np.fft.rfftfreq(FRAME, 1.0 / sr)
    m = np.sqrt(mag)
    rise = np.maximum(m - np.roll(m, 2, axis=0), 0.0)
    fl = np.array([rise[:, (f >= lo) & (f < hi)].sum(axis=1) for _, lo, hi in BANDS])
    return np.maximum(fl - np.median(fl, axis=1, keepdims=True), 0.0)


def per_step(env, step):
    """the strongest onset near each 16th (a little early to half a step late) and when it is"""
    n = len(env)
    s, at = np.zeros(STEPS), np.zeros(STEPS)
    for k in range(STEPS):
        c = k * step / HOP
        fr = np.arange(int(np.floor(c - 0.3 * step / HOP)), int(np.ceil(c + 0.55 * step / HOP)) + 1)
        v = env[fr % n]
        j = int(np.argmax(v))
        s[k], at[k] = v[j], (fr[j] * HOP - k * step) / step
    return s, at


def swing_of(flux, step):
    """how late the odd 16ths are against the even ones, as a swing % (50 straight, 66 triplet)"""
    s, at = per_step(flux[2] + flux[3], step)
    ev, od = s[0::2] > 0.2 * s.max(), s[1::2] > 0.2 * s.max()
    if od.sum() < 2 or ev.sum() < 2:
        return None
    return 50.0 * (1.0 + np.median(at[1::2][od]) - np.median(at[0::2][ev]))


def spectrum_db(x, sr, edges):
    p = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2 + 1e-12
    f = np.fft.rfftfreq(len(x), 1.0 / sr)
    e = np.array([p[(f >= lo) & (f < hi)].mean() for lo, hi in zip(edges[:-1], edges[1:])])
    return 10 * np.log10(e + 1e-12)


def chop_features(x, sr):
    """per slice (as the firmware cuts them): its spectrum and its envelope, in dB"""
    n = len(x)
    edges = np.geomspace(50, sr / 2, 13)
    out = []
    for k in range(STEPS):
        s = x[k * n // STEPS:(k + 1) * n // STEPS]
        sp = spectrum_db(s, sr, edges)
        env = np.array([np.sqrt(np.mean(c ** 2)) + 1e-6 for c in np.array_split(s, 8)])
        e = 20 * np.log10(env)
        out.append(np.concatenate([sp - sp.mean(), 0.7 * (e - e.max()), [0.5 * (e.max() + 60.0)]]))
    return np.array(out)


def analyse(x, sr):
    a = {}
    step = len(x) / STEPS
    a["rms"] = 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-9)
    a["peak"] = 20 * np.log10(np.max(np.abs(x)) + 1e-9)
    a["dc"] = float(np.mean(x))
    p = np.abs(np.fft.rfft(x)) ** 2
    f = np.fft.rfftfreq(len(x), 1.0 / sr)
    a["centroid"] = float((f * p).sum() / p.sum())
    c = np.cumsum(p) / p.sum()
    a["rolloff"] = float(f[np.searchsorted(c, 0.95)])
    a["sub"] = 10 * np.log10(p[(f > 20) & (f < 90)].sum() / p.sum())
    a["lf"] = 10 * np.log10(p[(f > 0) & (f < 15)].sum() / p.sum() + 1e-12)
    flux = band_flux(stft(x), sr)
    a["flux"] = flux
    a["steps"] = np.array([per_step(fb, step)[0] for fb in flux])
    a["swing"] = swing_of(flux, step)
    a["spec"] = spectrum_db(x, sr, np.geomspace(40, sr / 2, 25))
    a["chops"] = chop_features(x, sr)
    return a


def smooth(v, sig=2.0):
    k = np.exp(-0.5 * (np.arange(-6, 7) / sig) ** 2)
    return np.real(np.fft.ifft(np.fft.fft(v) * np.fft.fft(np.roll(np.pad(k, (0, len(v) - len(k))), -6))))


def distance(a, b):
    """onsets: 1 - cosine of the band onset envelopes (groove and micro-timing, band by band);
    spectrum: RMS of the level-free long-term spectra, in dB (the kit and the tone);
    chop: how far a slice of one is from the nearest slice of the other, in dB, against how far the
    slices of each break are from their own nearest (1: the other break has the same chops).
    score: the three against what 'clearly different' is (0.35, 4 dB, 1.6), averaged."""
    ea = np.concatenate([smooth(f) / (np.linalg.norm(f) + 1e-9) for f in a["flux"]])
    eb = np.concatenate([smooth(f) / (np.linalg.norm(f) + 1e-9) for f in b["flux"]])
    d_on = 1.0 - float(ea @ eb / (np.linalg.norm(ea) * np.linalg.norm(eb)))
    sa, sb = a["spec"] - a["spec"].mean(), b["spec"] - b["spec"].mean()
    d_sp = float(np.sqrt(np.mean((sa - sb) ** 2)))

    def dd(p, q):
        return np.sqrt(np.mean((p[:, None, :] - q[None, :, :]) ** 2, axis=2))

    def own(p):
        m = dd(p, p)
        np.fill_diagonal(m, np.inf)
        return m.min(axis=1).mean()
    cross = 0.5 * (dd(a["chops"], b["chops"]).min(axis=1).mean() + dd(b["chops"], a["chops"]).min(axis=1).mean())
    d_ch = float(cross / (0.5 * (own(a["chops"]) + own(b["chops"]))))
    score = (d_on / 0.35 + d_sp / 4.0 + d_ch / 1.6) / 3.0
    return d_on, d_sp, d_ch, score


def digits(v):
    m = v.max() + 1e-9
    return "".join("." if x < 0.1 * m else str(min(9, int(round(9 * x / m)))) for x in v)


def write_wav(path, pcm, sr):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(np.tile(pcm.astype("<i2"), 4).tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--header", type=Path, default=ROOT / "firmware" / "gen" / "kawa_breaks.h")
    ap.add_argument("--wav", type=Path, default=ROOT / "build" / "breaks")
    ap.add_argument("--no-wav", action="store_true")
    o = ap.parse_args()
    d, ulaw, names, tags, data = load(o.header)
    sr, n = d["BRK_SR"], d["BRK_N"]
    fails = []
    print(f"{o.header}: {n} breaks of {d['BRK_LEN']} samples, {sr} Hz, {d['BRK_BPM']} BPM")
    an = []
    for i in range(n):
        pcm = ulaw[data[i]]
        x = pcm / 32768.0
        if not o.no_wav:
            write_wav(o.wav / f"{names[i].lower()}.wav", pcm, sr)
        a = analyse(x, sr)
        an.append(a)
        sw = "  n/a" if a["swing"] is None else f"{a['swing']:4.1f}%"
        print(f"\n{i} {names[i]:8s} rms {a['rms']:6.1f} dBFS  peak {a['peak']:5.1f} dBFS  dc {a['dc'] * 100:+.3f}%  "
              f"centroid {a['centroid']:5.0f} Hz  rolloff95 {a['rolloff']:5.0f} Hz  sub {a['sub']:5.1f} dB  swing {sw}")
        print(f"  {'tag':5s} " + "".join("B" if t & 1 and t & 2 else "K" if t & 1 else "S" if t & 2 else
                                        "h" if t & 4 else "p" if t & 8 else "." for t in tags[i]) +
              "   (K kick, S snare, B both, h hat/cymbal, p percussion)")
        for (bn, lo, hi), sv in zip(BANDS, a["steps"]):
            print(f"  {bn} {lo:>5d}+ {digits(sv)}")
        # the checks
        codes = data[i]
        if (codes == 0).any() or (codes == 255).any() or a["peak"] > -0.3:
            fails.append(f"{names[i]}: clips (peak {a['peak']:.2f} dBFS, {(codes == 0).sum() + (codes == 255).sum()} "
                         "samples at full scale)")
        if abs(a["dc"]) > 0.001 or a["lf"] > -40:
            fails.append(f"{names[i]}: DC or rumble (mean {a['dc']:+.5f}, below 15 Hz {a['lf']:.1f} dB)")
        if a["rms"] < -24 or a["rms"] > -8:
            fails.append(f"{names[i]}: level {a['rms']:.1f} dBFS RMS, out of -24..-8")
        kick, snare = a["steps"][0] / a["steps"][0].max(), (a["steps"][2]) / a["steps"][2].max()
        if not tags[i][0] & 1:
            fails.append(f"{names[i]}: BRK_TAG has no kick on the one")
        if not (tags[i][4] & 2 and tags[i][12] & 2):
            fails.append(f"{names[i]}: BRK_TAG has no snare on 2 and 4 (steps 4, 12)")
        for k in range(STEPS):
            if tags[i][k] & 1 and kick[k] < 0.3:
                fails.append(f"{names[i]}: step {k} tagged kick, but little low onset there ({kick[k]:.2f})")
            if tags[i][k] & 2 and snare[k] < 0.3:
                fails.append(f"{names[i]}: step {k} tagged snare, but little snare onset there ({snare[k]:.2f})")
    # how far apart
    print("\ndistances (onsets 1-cos / spectrum dB / chop ratio / score; fail: score < "
          f"{MIN_SCORE} or chop < {MIN_CHOP})")
    print("        " + "".join(f"{nm:>22s}" for nm in names))
    score = np.zeros((n, n))
    for i in range(n):
        row = []
        for j in range(n):
            if i == j:
                row.append(f"{'-':>22s}")
                continue
            d_on, d_sp, d_ch, sc = distance(an[i], an[j])
            score[i, j] = sc
            row.append(f"{d_on:5.2f} {d_sp:4.1f} {d_ch:4.2f} {sc:4.2f}".rjust(22))
            if i < j and (sc < MIN_SCORE or d_ch < MIN_CHOP):
                fails.append(f"{names[i]} and {names[j]} are too alike (score {sc:.2f}, chop {d_ch:.2f})")
        print(f"{names[i]:8s}" + "".join(row))
    if n > 1:
        off = score[~np.eye(n, dtype=bool)]
        print(f"score: min {off.min():.2f}, mean {off.mean():.2f}")
    if not o.no_wav:
        print(f"wav: {o.wav}/")
    if fails:
        print("\nFAIL\n  " + "\n  ".join(fails))
        return 1
    print("\nok")
    return 0


if __name__ == "__main__":
    sys.exit(main())

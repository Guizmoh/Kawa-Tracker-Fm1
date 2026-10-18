#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate KAWA's breakbeats: firmware/gen/kawa_breaks.h.

Every sound here is synthesised: no sample from a record, and no known break copied note for note.
Each break is a drummer of its own, a kit of its own and a record of its own:
  - the groove: a 16th lane per instrument, with velocities, ghost notes, rolls, swing, and how far
    each player pushes or drags (micro-timing, in ms) and how loose he is;
  - the kit: kick, snare, hats or ride and crash, and its percussion (tambourine, shaker, congas,
    bongos, cowbell, claps, rim), each from its own parameters (tuning, decays, brightness);
  - the road to the sampler: the room, the desk (EQ, bus compressor), the tape (saturation, wow and
    flutter, hiss), the cut (a limiter), the disc (bandwidth, crackle, surface noise) and the sampler
    it was taken with (its rate and bits).
The drums are played at 44100 Hz and everything is worked round the loop (circular), so a bar loops
without a seam. Out comes one bar per break, 160 BPM, 22050 Hz, as 8-bit mu-law.

  python3 tools/gen_breaks.py            (needs numpy; the header is committed, so the build does not)
  python3 tools/gen_breaks.py --wav DIR  (also writes each break as a WAV, to listen)
  python3 tests/breaks_check.py          (levels, onsets, swing, and how far apart the breaks are)
"""
import argparse
import re
import wave
from pathlib import Path

import numpy as np

SR = 22050                              # the breaks' rate
FS = 2 * SR                             # the drums are played at 44100, then taken down
BPM = 160
STEPS = 16
STEP = FS * 60.0 / BPM / 4.0            # samples per 16th, at FS
BAR2 = int(round(STEP * STEPS))         # 66150 at FS
BAR = BAR2 // 2                         # 33075 at SR
LOOP = BAR2 / FS                        # 1.5 s
PRE = 1.0                               # ms: a hit lands just after its slice starts (the firmware fades in)
OUT = Path(__file__).resolve().parents[1] / "firmware" / "gen" / "kawa_breaks.h"


# ---- filters: magnitude responses, applied in the frequency domain
def lp(fc, o=2):
    return lambda f: 1.0 / np.sqrt(1.0 + (f / fc) ** (2 * o))


def hp(fc, o=2):
    return lambda f: 1.0 / np.sqrt(1.0 + (fc / np.maximum(f, 1e-3)) ** (2 * o))


def bp(fc, q):
    return lambda f: 1.0 / np.sqrt(1.0 + (q * (f / fc - fc / np.maximum(f, 1e-3))) ** 2)


def bell(fc, db, oc=1.0):
    return lambda f: 10.0 ** (db / 20.0 * np.exp(-0.5 * (np.log2(np.maximum(f, 1.0) / fc) / oc) ** 2))


def shelf(fc, db, high=True):
    def m(f):
        s = 1.0 / (1.0 + np.exp(-3.0 * np.log2(np.maximum(f, 1.0) / fc)))
        return 10.0 ** (db / 20.0 * (s if high else 1.0 - s))
    return m


def response(n, mags, rate=FS):
    f = np.fft.rfftfreq(n, 1.0 / rate)
    g = np.ones(len(f))
    for m in mags:
        g = g * m(f)
    return g


def colour(x, *mags):
    """zero-phase shaping, for noise and steady tones (the wrap does not matter)"""
    return np.fft.irfft(np.fft.rfft(x) * response(len(x), mags), len(x))


def minphase(g, n):
    """the minimum-phase response of magnitude g: causal, so the hits get no pre-echo"""
    full = np.concatenate([g, g[1:n - len(g) + 1][::-1]])
    c = np.fft.ifft(np.log(np.maximum(full, 1e-6))).real
    w = np.zeros(n)
    w[0] = 1.0
    w[1:(n + 1) // 2] = 2.0
    if n % 2 == 0:
        w[n // 2] = 1.0
    return np.exp(np.fft.fft(c * w))[:len(g)]


def eq(x, *mags, rate=FS):
    """a causal EQ, round the loop"""
    n = len(x)
    return np.fft.irfft(np.fft.rfft(x) * minphase(response(n, mags, rate), n), n)


# ---- helpers
def tt(dur):
    return np.arange(int(dur * FS)) / FS


def decay(t, d, a=0.0005):
    return (1.0 - np.exp(-t / a)) * np.exp(-t / d)


def osc(f, t, ph=0.0):
    """a sine; f a number or, for a pitch that moves, an array as long as t"""
    if np.ndim(f) == 0:
        return np.sin(2 * np.pi * f * t + ph)
    return np.sin(2 * np.pi * np.cumsum(f) / FS + ph)


def unit(x):
    return x / (np.max(np.abs(x)) + 1e-12)


def rms(x):
    return np.sqrt(np.mean(x * x)) + 1e-12


def cnoise(n, r, *mags):
    x = colour(r.standard_normal(n), *mags)
    return x / rms(x)


# ---- the instruments: (velocity 0..1, articulation, parameters, rng) -> one hit at FS, peak 1
KICK = dict(f=56.0, bend=1.5, bend_t=0.025, dec=0.2, box_f=110.0, box=0.3, box_d=0.05, click=0.4,
            click_f=3000.0, thud=0.3, drive=1.6, dur=0.6)


def kick(v, art, p, r):
    t = tt(max(p["dur"], 6.0 * p["dec"]))
    n = len(t)
    f = p["f"] * (1.0 + 0.008 * r.standard_normal())
    k = 1.0 + p["bend"] * (0.5 + 0.5 * v) * np.exp(-t / p["bend_t"])
    body = osc(f * k, t) * decay(t, p["dec"] * (0.8 + 0.2 * v), 0.0003)
    box = unit(osc(p["box_f"] * (1.0 + 0.06 * np.exp(-t / 0.01)), t) * decay(t, p["box_d"], 0.0005))
    click = unit(cnoise(n, r, bp(p["click_f"], 0.6)) * decay(t, 0.0025, 0.0001))
    thud = unit(cnoise(n, r, lp(260, 2)) * decay(t, 0.014, 0.0008))
    x = body + p["box"] * box + p["click"] * (0.3 + 0.7 * v) * click + p["thud"] * thud
    return unit(np.tanh(p["drive"] * x))


SNARE = dict(f=200.0, ring=0.8, tone_d=0.06, head=0.8, head_f=1400.0, head_d=0.03, wire=1.0, wire_d=0.12,
             wire_lo=900.0, wire_hi=9000.0, wire_pk=4500.0, crack=0.4, snap_f=2500.0, rim_f=900.0, drive=1.5,
             dur=0.5, buzz=0.04)


def wires(n, t, v, p, r):
    """the snare wires: a band of noise, darker and shorter when soft, that rattles as it dies"""
    w = cnoise(n, r, hp(p["wire_lo"], 1), lp(p["wire_hi"] * (0.5 + 0.5 * v), 2), bell(p["wire_pk"], 4, 1.0))
    rattle = 1.0 + 0.35 * unit(colour(r.standard_normal(n), lp(90, 1)))
    return unit(w * rattle * decay(t, p["wire_d"] * (0.6 + 0.4 * v), 0.0012))


def snare(v, art, p, r):
    t = tt(max(p["dur"], 5.0 * p["wire_d"], 6.0 * p["tone_d"]))
    n = len(t)
    f0 = p["f"] * (1.0 + 0.01 * r.standard_normal())
    bend = 1.0 + 0.08 * v * np.exp(-t / 0.006)
    shell = np.zeros(n)
    for q, a in zip((1.0, 1.59, 2.14, 2.30, 2.65, 2.92, 3.16), (1.0, 0.6, 0.45, 0.35, 0.3, 0.2, 0.15)):
        shell += a * r.uniform(0.6, 1.2) * osc(f0 * q * bend, t, r.uniform(0, 6.28)) * np.exp(-t / (p["tone_d"] / q ** 0.6))
    shell = unit(shell * (1.0 - np.exp(-t / 0.0004)))
    head = unit(cnoise(n, r, bp(p["head_f"], 0.8)) * decay(t, p["head_d"] * (0.7 + 0.3 * v), 0.0004))
    crack = unit(cnoise(n, r, hp(1200, 1), bell(p["snap_f"], 8, 0.6)) * decay(t, 0.003, 0.0002))
    x = (p["ring"] * (0.45 + 0.55 * v) * shell + p["head"] * head + p["wire"] * wires(n, t, v, p, r) +
         p["crack"] * v * crack)
    if art == "k":                              # a rimshot: the rim rings, the crack is louder
        rim = osc(p["rim_f"], t) * decay(t, 0.025, 0.0002) + 0.6 * osc(p["rim_f"] * 2.31, t) * decay(t, 0.012, 0.0002)
        x = x + 0.45 * unit(rim) + 0.5 * p["crack"] * crack
    return unit(np.tanh(p["drive"] * x))


HATS = dict(tune=1.0, metal=0.6, hp=7000.0, lp=16000.0, pk=9000.0, closed=0.035, half=0.16, open=0.42,
            tip=0.4, chick=0.5)
HAT_F = (205.3, 304.4, 369.6, 522.7, 540.0, 800.0)     # six square waves, out of tune with one another


def hats(v, art, p, r):
    d = {"x": p["closed"], "h": p["half"], "o": p["open"], "p": 0.018}[art or "x"]
    t = tt(min(6.0 * d, 1.4) + 0.02)
    n = len(t)
    m = sum(np.sign(np.sin(2 * np.pi * f * p["tune"] * (1.0 + 0.002 * r.standard_normal()) * t + r.uniform(0, 6.28)))
            for f in HAT_F)
    src = p["metal"] * m / rms(m) + (1.0 - p["metal"]) * r.standard_normal(n)
    x = colour(src, hp(p["hp"] * (0.9 + 0.1 * v), 3), lp(p["lp"] * (0.6 + 0.4 * v), 2), bell(p["pk"], 4, 0.6))
    env = decay(t, d, 0.0003)
    if art in ("h", "o"):                       # loose: the two cymbals rattle against each other
        env = env * (1.0 + 0.4 * unit(colour(r.standard_normal(n), lp(40, 1))))
    tip = unit(cnoise(n, r, hp(2500, 1), lp(12000, 1)) * decay(t, 0.0007, 0.0001))
    x = unit(x * env) + p["tip"] * v * tip
    if art == "p":                              # the foot: a chick, with some low
        x = 0.6 * x + p["chick"] * unit(cnoise(n, r, bp(800, 1.2)) * decay(t, 0.006, 0.0005))
    return unit(x)


RIDE = dict(lo=500.0, hi=14000.0, np=80, tilt=-0.25, dec=1.1, ddec=0.35, bell_f=740.0, bell=0.35, wash=0.45,
            wash_d=0.4, wash_hp=3000.0, tick=0.5, att=0.0003, dur=1.6, seed=1)
CRASH = dict(RIDE, lo=400.0, hi=15000.0, np=96, tilt=-0.3, dec=1.5, ddec=0.3, bell=0.0, wash=1.0, wash_d=0.9,
             wash_hp=2500.0, tick=0.3, att=0.002, dur=2.2, seed=2)


def cymbal_modes(p):
    """the cymbal itself: its modes, the same for every hit"""
    rr = np.random.default_rng(int(p["seed"]))
    f = np.exp(rr.uniform(np.log(p["lo"]), np.log(p["hi"]), p["np"]))
    a = rr.uniform(0.4, 1.0, p["np"]) * (f / 1000.0) ** -p["tilt"]
    d = p["dec"] * (f / 1000.0) ** -p["ddec"] * rr.uniform(0.6, 1.4, p["np"])
    return f, a, d


def cymbal(v, art, p, r):
    t = tt(max(p["dur"], 3.0 * p["dec"]))
    n = len(t)
    f, a, d = p["modes"]
    w = a * r.uniform(0.5, 1.5, len(f)) * (f / 1000.0) ** (0.3 * (v - 0.6))   # harder: brighter
    x = np.zeros(n)
    for fi, wi, di in zip(f, w, d):
        x += wi * np.sin(2 * np.pi * fi * t + r.uniform(0, 6.28)) * np.exp(-t / di)
    x = unit(x * (1.0 - np.exp(-t / p["att"])))
    if p["bell"]:                               # the bell: more of it on the cup ('b') or when hit hard
        b = sum(c * osc(p["bell_f"] * q, t, r.uniform(0, 6.28)) * np.exp(-t / (0.6 * p["dec"] / q ** 0.5))
                for q, c in ((1.0, 1.0), (2.02, 0.5), (2.76, 0.35), (3.91, 0.2)))
        x = x + p["bell"] * (1.0 if art == "b" else 0.3 * v) * unit(b)
    wash = unit(cnoise(n, r, hp(p["wash_hp"], 2), bell(7000, 3, 1.0)) * decay(t, p["wash_d"], p["att"] * 2))
    tick = unit(cnoise(n, r, hp(2000, 1)) * decay(t, 0.002, 0.0001))
    return unit(x + p["wash"] * v ** 1.5 * wash + p["tick"] * tick)


TAMB = dict(jingle=(6100.0, 7400.0, 8900.0, 10300.0, 11800.0, 13300.0), jd=0.03, spread=0.025, hits=26,
            hp=4500.0, skin=0.0)


def tamb(v, art, p, r):
    t = tt(0.3)
    n = len(t)
    sp = p["spread"] * (0.45 if art == "h" else 1.0)
    when = r.exponential(sp, p["hits"])         # the jingles strike one another, then less and less
    if art == "h":
        when[:5] = r.uniform(0, 0.002, 5)
    imp = np.zeros(n)
    for w_ in when:
        i = int(w_ * FS)
        if i < n:
            imp[i] += r.uniform(0.2, 1.0) * np.exp(-w_ / (2.5 * sp)) * r.choice((-1.0, 1.0))
    tj = tt(0.08)
    jir = sum(osc(f * r.uniform(0.98, 1.02), tj, r.uniform(0, 6.28)) * np.exp(-tj / (p["jd"] * r.uniform(0.6, 1.3)))
              for f in p["jingle"])
    ring = np.convolve(imp, jir)[:n]
    fizz = cnoise(n, r, hp(7000, 2), lp(15000, 1)) * np.convolve(np.abs(imp), np.exp(-tj / 0.012))[:n]
    x = colour(unit(ring) + 0.6 * unit(fizz), hp(p["hp"], 2))
    if art == "h" and p["skin"]:
        x = unit(x) + p["skin"] * unit(cnoise(n, r, bp(420, 1.0)) * decay(t, 0.02, 0.0005))
    return unit(x)


SHAKER = dict(hp=3500.0, pk=7000.0, lp=12000.0, att=0.012, dec=0.03, grain=0.6)


def shaker(v, art, p, r):
    t = tt(0.16)
    n = len(t)
    a = p["att"] * (0.5 if art == "b" else 1.0)  # 'b' the stroke back: sharper
    env = (1.0 - np.exp(-t / a)) ** 2 * np.exp(-t / p["dec"])
    beads = r.standard_normal(n) * (1.0 + p["grain"] * unit(colour(r.standard_normal(n), lp(250))))
    return unit(colour(beads, hp(p["hp"], 2), bell(p["pk"], 6, 0.7), lp(p["lp"], 2)) * env)


CONGA = dict(f=205.0, dec=0.22, bend=0.05, slap=1.0, low=0.72)


def skin(v, art, p, r):
    """a hand drum (conga, tumba, bongo) or a tom: a head's modes, the hand on it"""
    slap = art == "s"
    d = p["dec"] * {"s": 0.35, "b": 0.3, "t": 0.18}.get(art, 1.0)
    t = tt(5.0 * d + 0.05)
    n = len(t)
    f0 = p["f"] * (1.0 + 0.01 * r.standard_normal()) * (p["low"] if art == "h" else 1.0)
    bend = 1.0 + p["bend"] * (1.6 if slap else 1.0) * v * np.exp(-t / 0.02)
    tone = np.zeros(n)
    for q, a in zip((1.0, 1.50, 1.98, 2.44, 2.92, 3.40, 4.1), (1.0, 0.7, 0.5, 0.35, 0.25, 0.18, 0.1)):
        if art == "b" and q > 1.0:
            break
        tone += a * osc(f0 * q * bend, t, r.uniform(0, 6.28)) * np.exp(-t / (d / q ** 0.5))
    tone = unit(tone * (1.0 - np.exp(-t / 0.0005)))
    if slap:
        hand = unit(cnoise(n, r, hp(1200, 1), bell(3200, 6, 0.8), lp(9000, 2)) * decay(t, 0.012, 0.0003))
        x = 0.55 * tone + p["slap"] * hand
    elif art == "b":                            # the heel: a low thump
        x = colour(tone, lp(f0 * 1.5, 2)) + 0.3 * unit(cnoise(n, r, lp(400, 2)) * decay(t, 0.01, 0.0005))
    else:
        x = tone + 0.5 * unit(cnoise(n, r, bp(2000, 0.7)) * decay(t, 0.004, 0.0003))
    return unit(np.tanh(1.2 * unit(x)))


COWBELL = dict(f=560.0, dec=0.3, body=2200.0)


def cowbell(v, art, p, r):
    damp = art == "d"                           # the hand on the bell
    dd = p["dec"] * (0.15 if damp else 1.0)
    t = tt(5.0 * dd + 0.03)
    n = len(t)
    x = sum(a * osc(p["f"] * q, t, r.uniform(0, 6.28)) * np.exp(-t / (dd / q ** 0.4))
            for q, a in ((1.0, 1.0), (1.504, 0.9), (2.15, 0.35), (2.69, 0.3), (3.42, 0.15), (4.11, 0.1)))
    x = colour(unit(x * (1.0 - np.exp(-t / 0.0003))), bell(p["body"], 4, 1.0))
    return unit(unit(x) + 0.4 * unit(cnoise(n, r, bp(3000, 1.0)) * decay(t, 0.0015, 0.0001)))


CLAP = dict(f=1300.0, q=1.1, dec=0.08, hands=4, gap=0.0085)


def clap(v, art, p, r):
    t = tt(0.4)
    env = np.zeros(len(t))
    at = 0.0
    for _ in range(p["hands"]):                 # a few hands, not quite together
        env += np.where(t >= at, decay(np.maximum(t - at, 0.0), 0.0035, 0.0002), 0.0) * r.uniform(0.6, 1.0)
        at += p["gap"] * r.uniform(0.7, 1.3)
    env += np.where(t >= at, decay(np.maximum(t - at, 0.0), p["dec"], 0.0003), 0.0)
    return unit(cnoise(len(t), r, bp(p["f"], p["q"]), hp(500, 2)) * env)


RIM = dict(f=600.0, dec=0.03, wood=2000.0)


def rim(v, art, p, r):
    """a cross-stick, or ('k') a tick on the rim alone"""
    t = tt(0.15)
    n = len(t)
    f0 = p["f"] * (2.2 if art == "k" else 1.0)
    x = sum(a * osc(f0 * q, t, r.uniform(0, 6.28)) * np.exp(-t / (p["dec"] / q ** 0.5))
            for q, a in ((1.0, 1.0), (2.37, 0.7), (3.93, 0.5), (5.6, 0.3)))
    x = unit(x * (1.0 - np.exp(-t / 0.0002)))
    return unit(x + 1.0 * unit(cnoise(n, r, bp(p["wood"], 1.2)) * decay(t, 0.006, 0.0002)))


SUB = dict(f=44.0, bend=1.4, bend_t=0.04, dec=0.5, dur=1.0, drive=1.8)


def sub(v, art, p, r):
    """a long sine boom under the kick (a drum machine's, layered in)"""
    t = tt(p["dur"])
    x = osc(p["f"] * (1.0 + p["bend"] * np.exp(-t / p["bend_t"])), t) * decay(t, p["dec"], 0.002)
    x *= np.clip((t[-1] - t) / 0.05, 0.0, 1.0)
    return unit(np.tanh(p["drive"] * x))


# K kick, S snare, H hats (x closed, h half open, o open, p foot), R ride (b bell), Y crash, T tambourine
# (h hit), Z shaker (b back), C conga, U tumba (o open, s slap, b heel, t toe), B bongos (h hembra),
# M tom (h floor), W cowbell (d damped), P claps, J rim (cross-stick; k tick), X sub boom
SOUND = {"K": kick, "S": snare, "H": hats, "R": cymbal, "Y": cymbal, "T": tamb, "Z": shaker, "C": skin, "U": skin,
         "B": skin, "M": skin, "W": cowbell, "P": clap, "J": rim, "X": sub}
DEFAULT = {"K": KICK, "S": SNARE, "H": HATS, "R": RIDE, "Y": CRASH, "T": TAMB, "Z": SHAKER, "C": CONGA,
           "U": dict(CONGA, f=150.0, dec=0.3), "B": dict(CONGA, f=420.0, dec=0.12, bend=0.04),
           "M": dict(CONGA, f=120.0, dec=0.35, bend=0.12, low=0.7), "W": COWBELL, "P": CLAP, "J": RIM, "X": SUB}
LEVEL = {"K": 0, "S": 0, "H": -6, "R": -6, "Y": -5, "T": -8, "Z": -10, "C": -4, "U": -4, "B": -6, "M": -3,
         "W": -9, "P": -4, "J": -8, "X": -5}    # dB
SEND = {"K": 0.4, "S": 1.0, "H": 0.5, "R": 0.6, "Y": 0.8, "X": 0.0}                    # to the room (else 0.8)
TAG = {"K": 1, "X": 1, "S": 2, "P": 2, "H": 4, "R": 4, "Y": 4}                       # else 8: percussion
CHOKE = ("H", "X")                              # a hat closes the open one; a boom cuts the one before
DIRECT = ("X",)                                 # not through the bus compressor (it would pump)


# ---- the breaks. A lane: 16 steps, '.' or a velocity 1-9, then an articulation letter, then ':' a roll
# (two 32nds), ';' a drag (three), '^' a flam. swing: how late the odd 16ths are, in steps (0.33: a
# triplet shuffle). push: ms early (-) or late (+) per instrument. loose: ms of human spread.
BREAKS = [
    dict(name="SOLEIL", seed=101, swing=0.06, loose=2.5, push={"R": -2.0, "S": 2.0},
         # bright and open: a ride rolling in 8ths and 16ths, a crash on the one, a ringing high snare
         # (rimshots on 2 and 4) with ghost notes all round, the kick dancing round beat 3, a live room
         lanes={"K": "9 . . . . . . . 7 . 9 . . . . 4",
                "S": ". . 2 . 9k . 3 . . . 3 2 9k . . 5",
                "R": "7b . 5 . 6 . 5 4 7b . 5 . 6 . 5 4",
                "Y": "8 . . . . . . . . . . . . . . .",
                "H": ". . . . 4p . . . . . . . 4p . . ."},
         kit={"K": dict(f=57.0, bend=1.8, dec=0.2, box=0.3, box_f=105.0, click=0.6, click_f=3500.0, thud=0.2),
              "S": dict(f=235.0, ring=0.9, tone_d=0.07, wire=0.95, wire_d=0.15, wire_hi=11000.0, crack=0.55,
                        snap_f=3000.0, buzz=0.05),
              "R": dict(bell_f=760.0, bell=0.4, tick=0.6, seed=7, lvl=-4),
              "Y": dict(seed=8, lvl=-3),
              "H": dict(hp=6500.0, lvl=-12)},
         room=dict(rt=0.8, pre=6.0, er=0.5, er_ms=25.0, damp=7000.0, hf=0.7, build=10.0, lo=150.0, hi=12000.0,
                   wet=0.32, send={"K": 0.3, "S": 1.0, "R": 0.7, "Y": 0.8}),
         desk=dict(eq=(hp(35, 2), bell(2500, 2, 1.0), shelf(6000, 6)), thr=-12.0, ratio=3.0, att=0.002, rel=0.12),
         tape=dict(drive=1.4, bias=0.05, wow=0.0008, flutter=0.0004, hiss=-62.0, bump=80.0, hf=16000.0),
         disc=dict(hp=30.0, lp=15000.0, crackle=6.0, tick=-44.0, pops=0, surface=-66.0),
         sampler=dict(rate=0, bits=0), crest=10.5),
    dict(name="GRELOT", seed=202, swing=0.0, loose=1.5, push={"H": -3.0, "T": 3.0, "S": 1.0},
         # straight and busy: two hands of loud 16th hats opening into 1 and 3, a tambourine on the
         # backbeat and shaking the offbeats, kicks off the beat, few ghosts; a honky, thin, saturated
         # 70s funk record
         lanes={"K": "9 . . 7 . . 8 . . 6 . . . . 8 .",
                "S": ". . . . 9 . . 2 . 3 . . 9 . . 2",
                "H": "4 2 8 2 4 2 8 2 4 2 8 2 4 2 8o 2",
                "T": "6 . 3 . 6h . 3 . 6 . 3 . 6h . 3 ."},
         kit={"K": dict(f=62.0, bend=1.2, dec=0.16, box=0.35, box_f=120.0, click=0.4, thud=0.4),
              "S": dict(f=210.0, ring=0.7, tone_d=0.05, wire=1.1, wire_d=0.16, crack=0.4, buzz=0.06),
              "H": dict(tune=1.15, metal=0.55, hp=7000.0, pk=8500.0, closed=0.03, open=0.3, lvl=-4),
              "T": dict(skin=0.0, lvl=-6)},
         room=dict(rt=0.35, pre=3.0, er=0.6, er_ms=12.0, damp=4500.0, hf=0.5, build=6.0, lo=120.0, hi=10000.0,
                   wet=0.2, send={}),
         desk=dict(eq=(hp(120, 2), bell(1200, 6, 0.8), lp(8000, 1)), thr=-12.0, ratio=3.0, att=0.004, rel=0.12),
         tape=dict(drive=2.4, bias=0.08, wow=0.0012, flutter=0.0007, hiss=-54.0, bump=110.0, hf=11000.0),
         disc=dict(hp=60.0, lp=6500.0, crackle=14.0, tick=-38.0, pops=1, surface=-58.0),
         sampler=dict(rate=0, bits=0), crest=10.0),
    dict(name="TENDU", seed=303, swing=0.03, loose=1.0, push={"H": -2.0, "P": 5.0},
         # dry and tight: a dead room, a short muffled kick, a high tight snare doubled by claps, a lot
         # of ghost notes between the kicks, tight 8th hats; clean and full, crisp at the top
         lanes={"K": "9 . 4 . . . 7 . 8 . . 7 . . . .",
                "S": ". 2 . 3 9 . 2 3 . 3 . . 9 2 . 3",
                "P": ". . . . 7 . . . . . . . 7 . . .",
                "H": "7 . 4 . 7 . 4 3 7 . 4 . 7 . 5h ."},
         kit={"K": dict(f=64.0, bend=1.0, bend_t=0.015, dec=0.11, box=0.45, box_f=140.0, box_d=0.03, click=0.5,
                        click_f=2500.0, thud=0.6, drive=2.0),
              "S": dict(f=265.0, ring=0.6, tone_d=0.035, wire=1.0, wire_d=0.07, wire_lo=1500.0, crack=0.65,
                        snap_f=3500.0, drive=1.8, buzz=0.02),
              "P": dict(f=1400.0, dec=0.06, lvl=-5),
              "H": dict(tune=1.3, metal=0.4, hp=8000.0, closed=0.022, half=0.09, tip=0.6, lvl=-6)},
         room=dict(rt=0.15, pre=1.5, er=0.8, er_ms=6.0, damp=3000.0, hf=0.4, build=3.0, lo=200.0, hi=9000.0,
                   wet=0.07, send={}),
         desk=dict(eq=(hp(40, 2), bell(75, 3, 0.7), bell(400, -4, 1.0), shelf(8000, 4)), thr=-16.0, ratio=5.0,
                   att=0.001, rel=0.05),
         tape=dict(drive=1.1, bias=0.02, wow=0.0, flutter=0.0002, hiss=-68.0, bump=70.0, hf=18000.0),
         disc=dict(hp=30.0, lp=15000.0, crackle=1.0, tick=-52.0, pops=0, surface=-72.0),
         sampler=dict(rate=0, bits=0), crest=11.5),
    dict(name="ABYSSE", seed=404, swing=0.08, loose=2.0, push={"Z": -2.0, "S": 2.0, "J": 1.0},
         # ragga jungle: a two-step kick with a deep sub boom under it, a crisp rimshot snare in a long
         # plate, rolls into the one, 16th shaker, rim ticks, taken at a low sampler rate
         lanes={"K": "9 . . . . . . 5 . . 9 . . . . .",
                "X": "9 . . . . . . . . . 8 . . . . .",
                "S": ". . . . 9k . . 3 . 2 . . 9k . 8: 8:",
                "H": ". . 6 . . . 6 . . . 6 . . . 6 .",
                "Z": "3 2 5 2 3 2 5 2 3 2 5 2 3 2 5 3",
                "J": ". . . 5k . . . . . . . 5k . . . ."},
         kit={"K": dict(f=70.0, bend=1.5, dec=0.12, click=0.7, click_f=4000.0, thud=0.3),
              "X": dict(f=43.0, bend=1.5, bend_t=0.035, dec=0.55, dur=1.1, drive=1.6, lvl=-10),
              "S": dict(f=250.0, ring=0.8, tone_d=0.05, wire=0.9, wire_d=0.11, crack=0.75, snap_f=4200.0, buzz=0.03),
              "H": dict(tune=1.2, metal=0.5, hp=8500.0, closed=0.028, lvl=-6),
              "Z": dict(lvl=-9),
              "J": dict(lvl=-7)},
         room=dict(rt=1.3, pre=15.0, er=0.15, er_ms=20.0, damp=6000.0, hf=0.5, build=20.0, lo=300.0, hi=11000.0,
                   wet=0.4, send={"K": 0.05, "S": 1.3, "H": 0.2, "Z": 0.2, "J": 0.6}),
         desk=dict(eq=(hp(25, 2), shelf(70, 4, high=False), bell(7000, 2, 1.0)), thr=-12.0, ratio=4.0, att=0.003,
                   rel=0.1),
         tape=dict(drive=2.2, bias=0.04, wow=0.0, flutter=0.0002, hiss=-60.0, bump=60.0, hf=16000.0),
         disc=dict(hp=20.0, lp=11500.0, crackle=3.0, tick=-48.0, pops=0, surface=-66.0),
         sampler=dict(rate=15000, bits=12), crest=10.0),
    dict(name="MAMBO", seed=505, swing=0.12, loose=3.5, push={"C": 3.0, "U": 3.0, "B": -2.0, "W": -1.0, "S": 4.0},
         # latin: congas in a tumbao (heel, toe, slap, open tones, the low drum at the end), bongos on the
         # offbeat 16ths, a bell, a round soft kick, a high snare, all in a big warm room
         lanes={"K": "9 . . 5 . . 7 . 8 . . . . . 6 .",
                "S": ". . . . 8 . . . . . 3 . 8 . . 4",
                "H": ". . 3 . . . 3 . . . 3 . . . 3 .",
                "W": "8 . 4d . 7 . 4d . 8 . 4d . 7 . 4d .",
                "C": "3b . 3t . 7s . 7 6 3b . 3t . 7s . . .",
                "U": ". . . . . . . . . . . . . . 8 7",
                "B": ". 4 . 6 . 4 . 7h . 4 . 6 . 4 . 7h"},
         kit={"K": dict(f=52.0, bend=1.2, dec=0.25, box=0.2, click=0.25, thud=0.5, lvl=-2),
              "S": dict(f=300.0, ring=1.0, tone_d=0.09, wire=0.55, wire_d=0.09, crack=0.4, rim_f=1100.0, buzz=0.03),
              "H": dict(tune=0.9, metal=0.7, hp=5500.0, lvl=-10),
              "W": dict(f=590.0, dec=0.25, lvl=-7),
              "C": dict(f=200.0, dec=0.22, lvl=-6),
              "U": dict(f=150.0, dec=0.3, lvl=-7),
              "B": dict(f=420.0, low=0.72, dec=0.12, lvl=-6)},
         room=dict(rt=1.6, pre=22.0, er=0.35, er_ms=40.0, damp=4000.0, hf=0.45, build=25.0, lo=150.0, hi=9000.0,
                   wet=0.45, send={"K": 0.2, "S": 1.0, "C": 1.0, "U": 0.8, "B": 1.0, "W": 0.8, "H": 0.6}),
         desk=dict(eq=(hp(40, 2), bell(2500, 3, 1.0), lp(11000, 1)), thr=-14.0, ratio=3.0, att=0.006, rel=0.15),
         tape=dict(drive=1.8, bias=0.07, wow=0.0025, flutter=0.0008, hiss=-52.0, bump=90.0, hf=11000.0),
         disc=dict(hp=45.0, lp=8500.0, crackle=18.0, tick=-36.0, pops=1, surface=-56.0),
         sampler=dict(rate=0, bits=0), crest=11.0),
    dict(name="GRENIER", seed=606, swing=0.30, loose=5.0, push={"S": 12.0, "K": 3.0, "H": 5.0, "J": 9.0},
         # a slow, heavy swing (a hip-hop break, played here at 160: a double-time feel): a boxy kick, a
         # fat late snare, dark sloshy hats, cross-stick ghosts, a squashed small room, worn tape, a
         # dusty record and a 13 kHz 10-bit sampler
         lanes={"K": "9 . . . . . . 6 8 . . 5 . . . .",
                "S": ". . . . 9 . . . . 3 . . 9 . . 2",
                "H": "7 . 5 4 7 . 5 4 7 . 5 4 7 . 6h 4",
                "J": ". . . . . . 3 . . . . . . . 4 ."},
         kit={"K": dict(f=60.0, bend=0.9, dec=0.24, box=0.55, box_f=95.0, box_d=0.07, click=0.2, thud=0.6, drive=2.5),
              "S": dict(f=180.0, ring=1.0, tone_d=0.1, wire=0.85, wire_d=0.22, wire_hi=6500.0, crack=0.3, drive=2.0,
                        buzz=0.06),
              "H": dict(tune=0.8, metal=0.7, hp=3500.0, pk=6000.0, lp=9000.0, closed=0.05, half=0.2, lvl=-5),
              "J": dict(f=450.0, lvl=-8)},
         room=dict(rt=0.35, pre=3.0, er=0.9, er_ms=10.0, damp=2500.0, hf=0.35, build=4.0, lo=100.0, hi=7000.0,
                   wet=0.35, send={"K": 0.6, "S": 1.0, "H": 0.7, "J": 0.8}),
         desk=dict(eq=(hp(70, 2), bell(900, 4, 0.8), lp(8000, 2)), thr=-20.0, ratio=6.0, att=0.005, rel=0.2),
         tape=dict(drive=2.8, bias=0.12, wow=0.004, flutter=0.0015, hiss=-46.0, bump=100.0, hf=9000.0),
         disc=dict(hp=60.0, lp=7000.0, crackle=45.0, tick=-30.0, pops=2, surface=-50.0),
         sampler=dict(rate=13000, bits=10), crest=9.0),
]


# ---- playing a break
def place(bus, x, pos):
    """add a hit at pos, its tail wrapping round to the start: it loops"""
    i = int(round(pos)) % BAR2
    while len(x):
        k = min(len(x), BAR2 - i)
        bus[i:i + k] += x[:k]
        x = x[k:]
        i = 0


def events_of(b, r):
    ev, tags = [], [0] * STEPS
    for inst, lane in b["lanes"].items():
        toks = lane.split()
        assert len(toks) == STEPS, (b["name"], inst)
        for step, tok in enumerate(toks):
            if tok == ".":
                continue
            m = re.fullmatch(r"(\d)([a-z]?)([:;^]?)", tok)
            vel, art, mod = int(m[1]) / 9.0, m[2], m[3]
            ms = PRE + b["push"].get(inst, 0.0) + r.normal(0.0, b["loose"])
            if inst in "KSX" and vel >= 0.45:
                ms = max(ms, 0.3)               # the hits the chopper keeps start in their own slice
            pos = (step + (b["swing"] if step % 2 else 0.0)) * STEP + ms * FS / 1000.0
            hits = {":": ((0.0, 1.0), (0.5, 0.8)), ";": ((0.0, 0.75), (1 / 3, 0.85), (2 / 3, 1.0)),
                    "^": ((-0.022 * FS / STEP, 0.4), (0.0, 1.0))}.get(mod, ((0.0, 1.0),))
            for off, g in hits:
                ev.append((pos + off * STEP, inst, float(np.clip(vel * g * (1.0 + 0.07 * r.standard_normal()), 0.05, 1.0)), art))
            if vel >= 0.45:
                tags[step] |= TAG.get(inst, 8)
    ev.sort(key=lambda e: e[0])
    return ev, tags


def room_ir(p, r):
    n = int(min(p["rt"] * 1.3, 2.5) * FS)
    t = np.arange(n) / FS
    pre = int(p["pre"] * FS / 1000.0)
    ir = np.zeros(n)
    for _ in range(10):                         # early reflections: the walls, the ceiling, the booth
        ir[pre + int(r.uniform(0, p["er_ms"]) * FS / 1000.0)] += r.uniform(0.2, 1.0) * r.choice((-1.0, 1.0)) * p["er"]
    tail = r.standard_normal(n)
    lo = colour(tail, lp(p["damp"], 1))
    late = lo * np.exp(-6.91 * t / p["rt"]) + (tail - lo) * np.exp(-6.91 * t / (p["rt"] * p["hf"]))
    late *= np.clip((t * FS - pre) / (p["build"] * FS / 1000.0 + 1.0), 0.0, 1.0)
    ir += late / np.sqrt((late ** 2).sum())
    ir = eq(ir, hp(p["lo"], 1), lp(p["hi"], 1))
    return ir / np.sqrt((ir ** 2).sum())


def circ_conv(x, ir):
    fold = np.zeros(len(x))
    for k in range(0, len(ir), len(x)):
        seg = ir[k:k + len(x)]
        fold[:len(seg)] += seg
    return np.fft.irfft(np.fft.rfft(x) * np.fft.rfft(fold), len(x))


def compress(x, thr, ratio, att, rel, knee=6.0):
    """a bus compressor (peak sensing, soft knee), twice round the loop to settle"""
    ga, gr = np.exp(-1.0 / (att * FS)), np.exp(-1.0 / (rel * FS))
    lv = np.abs(x).tolist()
    env = [0.0] * len(lv)
    e = 0.0
    for lap in range(2):
        for i, v in enumerate(lv):
            e = v + (ga if v > e else gr) * (e - v)
            env[i] = e
    over = 20 * np.log10(np.array(env) + 1e-9) - thr
    s = 1.0 / ratio - 1.0
    g = np.where(over <= -knee / 2, 0.0, np.where(over >= knee / 2, s * over, s * (over + knee / 2) ** 2 / (2 * knee)))
    return x * 10 ** (g / 20.0)


def tape(x, p, r):
    d, b = p["drive"], p["bias"]
    y = (np.tanh(d * (x + b)) - np.tanh(d * b)) / np.tanh(d)   # a hot tape; the bias: even harmonics
    y = eq(y, bell(p["bump"], 2.0, 0.5), lp(p["hf"], 1))       # the head bump, the highs it loses
    t = np.arange(BAR2) / FS
    dl = np.zeros(BAR2)                         # wow and flutter: whole cycles per loop, it stays a loop
    wobble = [(1, 1.0), (2, 0.5), (3, 0.3)]
    for k, w in wobble:
        fk = k / LOOP
        dl += p["wow"] * w / (2 * np.pi * fk) * np.sin(2 * np.pi * fk * t + r.uniform(0, 6.28))
    for k, w in [(9, 1.0), (14, 0.6), (22, 0.4)] + [(int(k), 0.15) for k in r.integers(40, 90, 6)]:
        fk = k / LOOP
        dl += p["flutter"] * w / (2 * np.pi * fk) * np.sin(2 * np.pi * fk * t + r.uniform(0, 6.28))
    y = np.interp((np.arange(BAR2) - dl * FS) % BAR2, np.arange(BAR2 + 1), np.concatenate([y, y[:1]]))
    return y + cnoise(BAR2, r, hp(2000, 1), lp(16000, 1)) * 10 ** (p["hiss"] / 20.0)


def softlim(z, k=0.55):
    a = np.abs(z)
    return np.where(a <= k, z, np.sign(z) * (k + (1 - k) * np.tanh((a - k) / (1 - k))))


def cut(x, crest):
    """the master: a limiter, pushed until the peak is crest dB over the RMS"""
    def cr(g):
        y = softlim(g * x / np.max(np.abs(x)))
        return 20 * np.log10(np.max(np.abs(y)) / rms(y)), y
    lo, hi = 1.0, 8.0
    if cr(lo)[0] <= crest:
        return unit(cr(lo)[1])
    for _ in range(30):
        mid = 0.5 * (lo + hi)
        if cr(mid)[0] > crest:
            lo = mid
        else:
            hi = mid
    return unit(cr(hi)[1])


def disc(x, p, r):
    y = eq(x, hp(p["hp"], 2), lp(p["lp"], 2))   # the bandwidth of the cut
    t = np.arange(BAR2) / FS
    k = int(p["crackle"] * LOOP)                # dust: ticks, and a pop or two
    tick = np.zeros(BAR2)
    if k:
        np.add.at(tick, r.integers(0, BAR2, k), r.lognormal(0.0, 0.9, k) * r.choice((-1.0, 1.0), k))
        y += unit(colour(tick, hp(1500, 1), lp(8000, 1))) * 10 ** (p["tick"] / 20.0)
    for _ in range(p["pops"]):
        pop = np.zeros(BAR2)
        pop[r.integers(0, BAR2)] = 1.0
        y += unit(colour(pop, hp(300, 1), lp(2500, 1))) * 10 ** ((p["tick"] + 8.0) / 20.0) * r.choice((-1.0, 1.0))
    surf = cnoise(BAR2, r, hp(400, 1), lp(6000, 1)) * (1.0 + 0.3 * np.sin(2 * np.pi * t / LOOP))
    return y + surf * 10 ** (p["surface"] / 20.0)


def down(x):
    """44100 to 22050, round the loop"""
    f = np.fft.rfftfreq(BAR, 1.0 / SR)
    taper = 0.5 - 0.5 * np.cos(np.pi * np.clip((SR / 2 - f) / 900.0, 0.0, 1.0))
    return np.fft.irfft(np.fft.rfft(x)[:BAR // 2 + 1] * taper, BAR) * 0.5


def sampler(x, p):
    if p["rate"]:                               # held at its own rate, no interpolation: the grit
        k = int(round(p["rate"] * BAR / SR))
        x = x[(np.floor(np.arange(BAR) * k / BAR) * BAR / k).astype(int)]
        x = eq(x, lp(p["rate"] * 0.5, 1), rate=SR)
    if p["bits"]:
        q = 2 ** (p["bits"] - 1) / np.max(np.abs(x))
        x = np.round(x * q) / q
    return x


def render(b):
    r = np.random.default_rng(b["seed"])
    kit = {k: {**DEFAULT[k], "lvl": LEVEL[k], **b["kit"].get(k, {})} for k in set(b["lanes"]) | {"S"}}
    for k in ("R", "Y"):
        if k in kit:
            kit[k]["modes"] = cymbal_modes(kit[k])
    ev, tags = events_of(b, r)
    dry, send, direct = np.zeros(BAR2), np.zeros(BAR2), np.zeros(BAR2)
    room = b["room"]
    for i, (pos, inst, v, art) in enumerate(ev):
        p = kit[inst]
        x = SOUND[inst](v, art, p, r) * v ** 1.7 * 10 ** (p["lvl"] / 20.0)
        f = min(len(x) // 4, int(0.02 * FS))
        x[len(x) - f:] *= np.linspace(1.0, 0.0, f)              # no click where the hit is cut
        if inst in CHOKE:                       # until the next one of its group
            nxt = [e[0] for e in ev[i + 1:] + ev[:i] if e[1] == inst]
            if nxt:
                c = int(round((nxt[0] - pos) % BAR2))
                if c < len(x):
                    fade = min(int(0.006 * FS), len(x) - c)
                    x = x.copy()
                    x[c:c + fade] *= np.linspace(1.0, 0.0, fade)
                    x[c + fade:] = 0.0
        place(direct if inst in DIRECT else dry, x, pos)
        place(send, x * room["send"].get(inst, SEND.get(inst, 0.8)), pos)
        if inst in "KMX":                       # the snare wires buzz with the kick
            s = kit["S"]
            t = tt(0.3)
            z = wires(len(t), t, 0.4, s, r) * s["buzz"] * v ** 1.7 * 10 ** (s["lvl"] / 20.0)
            place(dry, z, pos + 0.002 * FS)
            place(send, z, pos + 0.002 * FS)
    x = eq(dry + circ_conv(send, room_ir(room, r)) * room["wet"], *b["desk"]["eq"])
    d, g = b["desk"], 1.0 / np.max(np.abs(x))
    x = compress(x * g, d["thr"], d["ratio"], d["att"], d["rel"]) + eq(direct, *d["eq"]) * g
    x = tape(unit(x), b["tape"], r)
    x = sampler(down(disc(x, b["disc"], r)), b["sampler"])
    x = cut(eq(x, hp(25, 3), rate=SR), b["crest"])
    x -= x.mean()
    return x * (0.9 / np.max(np.abs(x))), tags  # -0.9 dBFS


def mulaw_encode(x):
    mu = 255.0
    y = np.sign(x) * np.log1p(mu * np.abs(x)) / np.log1p(mu)
    return np.clip(np.round((y + 1) * 127.5), 0, 255).astype(np.uint8)


def mulaw_table():
    mu = 255.0
    c = np.arange(256) / 127.5 - 1
    x = np.sign(c) * (np.power(1 + mu, np.abs(c)) - 1) / mu
    return np.clip(np.round(x * 32767), -32767, 32767).astype(int)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wav", type=Path)
    a = ap.parse_args()
    lines = ["/* generated by tools/gen_breaks.py: synthesised breakbeats (no samples from records), GPL-3.0-only */",
             "#pragma once", "#include <stdint.h>",
             f"#define BRK_N {len(BREAKS)}u", f"#define BRK_LEN {BAR}u            /* one bar, 22050 Hz */",
             f"#define BRK_BPM {BPM}u", f"#define BRK_SR {SR}u"]
    tab = mulaw_table()
    lines.append("static const int16_t BRK_ULAW[256] = {" + ", ".join(map(str, tab)) + "};")
    names, tagrows, datas = [], [], []
    for b in BREAKS:
        name = b["name"]
        assert len(name) <= 7 and name == name.upper(), name
        bar, tags = render(b)
        code = mulaw_encode(bar)
        names.append(f'"{name}"')
        tagrows.append("{" + ", ".join(map(str, tags)) + "}")
        rows = [", ".join(map(str, code[i:i + 40])) for i in range(0, len(code), 40)]
        datas.append("{\n" + ",\n".join(rows) + "}")
        if a.wav:
            a.wav.mkdir(parents=True, exist_ok=True)
            with wave.open(str(a.wav / f"{name.lower()}.wav"), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(SR)
                dec = tab[code].astype(np.int16)
                w.writeframes(np.tile(dec, 4).tobytes())
        print(f"{name}: {len(code)} samples, tags {tags}")
    lines.append("static const char *const BRK_NAME[BRK_N] = {" + ", ".join(names) + "};")
    lines.append("static const uint8_t BRK_TAG[BRK_N][16] = {" + ", ".join(tagrows) +
                 "};   /* 1 kick 2 snare 4 hat or cymbal 8 percussion */")
    lines.append("static const uint8_t BRK_DATA[BRK_N][BRK_LEN] = {" + ",\n".join(datas) + "};")
    OUT.write_text("\n".join(lines) + "\n")
    print("->", OUT)


if __name__ == "__main__":
    main()

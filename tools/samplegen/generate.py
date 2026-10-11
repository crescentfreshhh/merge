#!/usr/bin/env python3
"""Merge Factory Library generator.

Synthesizes every factory sound that ships with Merge from scratch, so the
whole library is our own work and can be released as CC0 (public domain).

Pure Python standard library on purpose: no numpy, nothing to install.
Output is deterministic (fixed seeds), so the same script always produces
byte-identical files.

Usage:
    python3 tools/samplegen/generate.py --out build/Samples/Factory
"""

import argparse
import json
import math
import os
import random
import struct
import sys
import wave

SR = 44100
TWO_PI = 2.0 * math.pi
GENERATOR_VERSION = 4


# ---------------------------------------------------------------------------
# DSP primitives (operate on plain Python lists of floats)
# ---------------------------------------------------------------------------

def ns(seconds):
    return max(1, int(seconds * SR))


def silence(seconds):
    return [0.0] * ns(seconds)


def midi_hz(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


def sine_sweep(dur, f_start, f_end, tau, phase=0.0):
    """Sine whose frequency falls exponentially from f_start to f_end."""
    out = []
    ph = phase
    for i in range(ns(dur)):
        t = i / SR
        f = f_end + (f_start - f_end) * math.exp(-t / tau)
        ph += TWO_PI * f / SR
        out.append(math.sin(ph))
    return out


def osc(dur, freq, shape="sine", detune_cents=0.0, phase=0.0):
    f = freq * 2.0 ** (detune_cents / 1200.0)
    out = []
    ph = phase
    inc = f / SR
    for _ in range(ns(dur)):
        if shape == "sine":
            v = math.sin(TWO_PI * ph)
        elif shape == "saw":
            v = 2.0 * (ph - math.floor(ph + 0.5))
        elif shape == "square":
            v = 1.0 if (ph % 1.0) < 0.5 else -1.0
        elif shape == "tri":
            v = 4.0 * abs((ph % 1.0) - 0.5) - 1.0
        else:
            raise ValueError(shape)
        out.append(v)
        ph += inc
    return out


def noise(dur, rng):
    return [rng.uniform(-1.0, 1.0) for _ in range(ns(dur))]


def env_exp(length, tau, attack=0.0005, hold=0.0):
    out = []
    a = max(1, int(attack * SR))
    h = int(hold * SR)
    for i in range(length):
        if i < a:
            out.append(i / a)
        elif i < a + h:
            out.append(1.0)
        else:
            out.append(math.exp(-(i - a - h) / (tau * SR)))
    return out


def env_adsr(length, attack, decay, sustain, release_at, release):
    out = []
    a = max(1, int(attack * SR))
    d = max(1, int(decay * SR))
    r_start = int(release_at * SR)
    r = max(1, int(release * SR))
    level_at_release = None
    for i in range(length):
        if i < a:
            v = i / a
        elif i < a + d:
            v = 1.0 - (1.0 - sustain) * ((i - a) / d)
        else:
            v = sustain
        if i >= r_start:
            if level_at_release is None:
                level_at_release = v
            k = (i - r_start) / r
            v = level_at_release * max(0.0, 1.0 - k)
        out.append(v)
    return out


def mul(a, b):
    n = min(len(a), len(b))
    return [a[i] * b[i] for i in range(n)]


def gain(a, g):
    return [x * g for x in a]


def mix(*parts):
    """mix((signal, gain), (signal, gain), ...) -> padded sum."""
    n = max(len(p[0]) for p in parts)
    out = [0.0] * n
    for sig, g in parts:
        for i, x in enumerate(sig):
            out[i] += x * g
    return out


def delay_sig(a, seconds):
    return [0.0] * ns(seconds) + a


def pad_to(a, seconds):
    n = ns(seconds)
    return a + [0.0] * max(0, n - len(a))


def saturate(a, drive):
    if drive <= 0:
        return a
    norm = math.tanh(drive)
    return [math.tanh(x * drive) / norm for x in a]


def bitcrush(a, bits, downsample=1):
    q = 2.0 ** (bits - 1)
    out = []
    held = 0.0
    for i, x in enumerate(a):
        if i % downsample == 0:
            held = round(x * q) / q
        out.append(held)
    return out


class Biquad:
    """RBJ cookbook biquad."""

    def __init__(self, kind, freq, q=0.707, gain_db=0.0):
        w0 = TWO_PI * min(freq, SR * 0.49) / SR
        cw = math.cos(w0)
        sw = math.sin(w0)
        alpha = sw / (2.0 * q)
        A = 10.0 ** (gain_db / 40.0)
        if kind == "lp":
            b0, b1, b2 = (1 - cw) / 2, 1 - cw, (1 - cw) / 2
            a0, a1, a2 = 1 + alpha, -2 * cw, 1 - alpha
        elif kind == "hp":
            b0, b1, b2 = (1 + cw) / 2, -(1 + cw), (1 + cw) / 2
            a0, a1, a2 = 1 + alpha, -2 * cw, 1 - alpha
        elif kind == "bp":
            b0, b1, b2 = alpha, 0.0, -alpha
            a0, a1, a2 = 1 + alpha, -2 * cw, 1 - alpha
        elif kind == "peak":
            b0, b1, b2 = 1 + alpha * A, -2 * cw, 1 - alpha * A
            a0, a1, a2 = 1 + alpha / A, -2 * cw, 1 - alpha / A
        else:
            raise ValueError(kind)
        self.b = (b0 / a0, b1 / a0, b2 / a0)
        self.a = (a1 / a0, a2 / a0)

    def run(self, x):
        b0, b1, b2 = self.b
        a1, a2 = self.a
        x1 = x2 = y1 = y2 = 0.0
        out = []
        for v in x:
            y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
            x2, x1 = x1, v
            y2, y1 = y1, y
            out.append(y)
        return out


def lp(x, f, q=0.707):
    return Biquad("lp", f, q).run(x)


def hp(x, f, q=0.707):
    return Biquad("hp", f, q).run(x)


def bp(x, f, q=1.0):
    return Biquad("bp", f, q).run(x)


def peak(x, f, q, db):
    return Biquad("peak", f, q, db).run(x)


def reverb(x, size=0.5, damp=0.4, wet=0.25, tail=0.6):
    """Tiny Schroeder reverb (4 combs + 2 allpasses), mono."""
    x = x + [0.0] * ns(tail)
    combs = [1116, 1188, 1277, 1356]
    fb = 0.7 + 0.28 * size
    acc = [0.0] * len(x)
    for d in combs:
        buf = [0.0] * d
        idx = 0
        store = 0.0
        for i, v in enumerate(x):
            y = buf[idx]
            store = y * (1 - damp) + store * damp
            buf[idx] = v + store * fb
            idx = (idx + 1) % d
            acc[i] += y
    for d in (556, 441):
        buf = [0.0] * d
        idx = 0
        out = []
        for v in acc:
            b = buf[idx]
            y = -v + b
            buf[idx] = v + b * 0.5
            idx = (idx + 1) % d
            out.append(y)
        acc = out
    return [dry + w * wet * 0.25 for dry, w in zip(x, acc)]


def fade(x, fade_in_ms=0.0, fade_out_ms=6.0):
    x = list(x)
    fi = int(fade_in_ms * SR / 1000)
    fo = min(len(x), int(fade_out_ms * SR / 1000))
    for i in range(min(fi, len(x))):
        x[i] *= i / fi
    for i in range(fo):
        x[len(x) - 1 - i] *= i / fo
    return x


def trim_tail(x, threshold=10 ** (-70 / 20)):
    end = len(x)
    while end > 1 and abs(x[end - 1]) < threshold:
        end -= 1
    return x[: max(end, ns(0.02))]


def normalize(x, peak_db=-1.0):
    p = max((abs(v) for v in x), default=0.0)
    if p <= 0:
        return x
    g = (10 ** (peak_db / 20.0)) / p
    return [v * g for v in x]


def finish(x, peak_db=-1.0, fade_out_ms=6.0):
    return normalize(fade(trim_tail(x), 0.0, fade_out_ms), peak_db)


def reverse(x):
    return list(reversed(x))


# ---------------------------------------------------------------------------
# Drum & instrument voices
# ---------------------------------------------------------------------------

def kick(rng, f0=160.0, f1=48.0, pitch_tau=0.035, decay=0.32, click=0.35, drive=1.4,
         noise_amt=0.0, dur=0.9, lowpass=None):
    body = mul(sine_sweep(dur, f0, f1, pitch_tau), env_exp(ns(dur), decay, attack=0.0008))
    clk = mul(hp(noise(0.012, rng), 2500), env_exp(ns(0.012), 0.002))
    parts = [(body, 1.0), (clk, click)]
    if noise_amt > 0:
        thump = mul(lp(noise(0.08, rng), 900), env_exp(ns(0.08), 0.02))
        parts.append((thump, noise_amt))
    out = saturate(mix(*parts), drive)
    if lowpass:
        out = lp(out, lowpass)
    return finish(out)


def snare(rng, tone=190.0, tone_decay=0.08, noise_decay=0.16, noise_hp=1200.0,
          noise_amt=0.9, body=0.6, dur=0.6, verb=0.0, lowpass=None, drive=1.0):
    t1 = mul(sine_sweep(dur, tone * 1.6, tone, 0.012), env_exp(ns(dur), tone_decay))
    t2 = mul(osc(dur, tone * 1.47, "sine"), env_exp(ns(dur), tone_decay * 0.7))
    nz = mul(lp(hp(noise(dur, rng), noise_hp), 9000), env_exp(ns(dur), noise_decay, attack=0.0005))
    nz = peak(nz, 5000, 0.8, 4.0)
    out = mix((t1, body), (t2, body * 0.5), (nz, noise_amt))
    out = saturate(out, drive)
    if lowpass:
        out = lp(out, lowpass)
    if verb > 0:
        out = reverb(out, size=0.45, damp=0.5, wet=verb, tail=0.5)
    return finish(out)


def clap(rng, bursts=4, spacing=0.0105, tail=0.18, centre=1150.0, verb=0.25, dur=0.7):
    out = [0.0] * ns(dur)
    for b in range(bursts):
        last = b == bursts - 1
        length = tail if last else 0.012
        burst = mul(noise(length + 0.01, rng), env_exp(ns(length + 0.01), length / (1.0 if last else 3.0)))
        start = ns(b * spacing)
        for i, v in enumerate(burst):
            if start + i < len(out):
                out[start + i] += v
    out = bp(out, centre, 0.9)
    out = hp(out, 600)
    if verb > 0:
        out = reverb(out, size=0.55, damp=0.35, wet=verb, tail=0.5)
    return finish(out)


HAT_FREQS = [205.3, 304.4, 369.6, 522.7, 540.0, 800.0]


def metal(dur, scale=1.0):
    oscs = [osc(dur, f * scale, "square", phase=i * 0.13) for i, f in enumerate(HAT_FREQS)]
    return mix(*[(o, 1.0 / 6.0) for o in oscs])


def hat(rng, decay=0.045, dur=0.35, scale=1.0, tone=9000.0, noise_amt=0.35, lowpass=None):
    m = metal(dur, scale)
    nz = noise(dur, rng)
    src = mix((m, 1.0), (nz, noise_amt))
    src = bp(src, tone, 0.8)
    src = hp(src, 6500)
    out = mul(src, env_exp(ns(dur), decay, attack=0.0003))
    if lowpass:
        out = lp(out, lowpass)
    return finish(out)


def cymbal(rng, decay=0.9, dur=2.2, scale=1.6, tone=7000.0):
    m = metal(dur, scale)
    nz = noise(dur, rng)
    src = hp(mix((m, 0.6), (nz, 0.7)), 4000)
    src = bp(src, tone, 0.4)
    out = mul(src, env_exp(ns(dur), decay, attack=0.001))
    return finish(out)


def rim(rng, dur=0.12):
    a = mul(osc(dur, 1720, "sine"), env_exp(ns(dur), 0.012))
    b = mul(osc(dur, 485, "tri"), env_exp(ns(dur), 0.02))
    c = mul(bp(noise(dur, rng), 3000, 2.0), env_exp(ns(dur), 0.006))
    return finish(mix((a, 0.6), (b, 0.8), (c, 0.5)))


def tom(rng, f0, f1, decay=0.25, dur=0.8):
    body = mul(sine_sweep(dur, f0, f1, 0.06), env_exp(ns(dur), decay))
    nz = mul(lp(noise(dur, rng), 3000), env_exp(ns(dur), 0.015))
    return finish(saturate(mix((body, 1.0), (nz, 0.15)), 1.2))


def shaker(rng, dur=0.18, centre=6500.0, attack=0.012, decay=0.045):
    nz = bp(noise(dur, rng), centre, 1.2)
    nz = hp(nz, 3000)
    return finish(mul(nz, env_exp(ns(dur), decay, attack=attack)))


def cowbell(rng, dur=0.45):
    a = osc(dur, 540, "square")
    b = osc(dur, 800, "square")
    src = bp(mix((a, 0.5), (b, 0.5)), 900, 1.5)
    out = mul(src, env_exp(ns(dur), 0.09, attack=0.0005))
    return finish(out)


def conga(rng, f=310.0, dur=0.45, decay=0.14):
    body = mul(sine_sweep(dur, f * 1.25, f, 0.015), env_exp(ns(dur), decay))
    slap = mul(bp(noise(0.02, rng), 2200, 1.5), env_exp(ns(0.02), 0.004))
    return finish(mix((body, 1.0), (slap, 0.3)))


def woodblock(rng, f=1050.0, dur=0.15):
    a = mul(osc(dur, f, "sine"), env_exp(ns(dur), 0.022))
    b = mul(osc(dur, f * 2.76, "sine"), env_exp(ns(dur), 0.008))
    return finish(mix((a, 1.0), (b, 0.35)))


def snap(rng, dur=0.25):
    nz = bp(noise(dur, rng), 2500, 1.4)
    out = mul(nz, env_exp(ns(dur), 0.035, attack=0.0004))
    return finish(reverb(out, 0.3, 0.5, 0.2, 0.25))


def bass808(rng, note=29, dur=2.2, decay=0.9, glide_semis=0.0, drive=2.0, punch=0.3):
    f = midi_hz(note)
    f_start = f * (2 ** (glide_semis / 12.0)) if glide_semis else f * 1.9
    tau = 0.09 if glide_semis else 0.025
    body = sine_sweep(dur, f_start, f, tau)
    env = env_exp(ns(dur), decay, attack=0.002)
    out = mul(body, env)
    clk = mul(lp(noise(0.01, rng), 2000), env_exp(ns(0.01), 0.002))
    out = mix((out, 1.0), (clk, punch))
    out = saturate(out, drive)
    out = lp(out, 3500)
    return finish(out, fade_out_ms=20)


def log_drum(rng, note=41, dur=0.9):
    f = midi_hz(note)
    body = mul(sine_sweep(dur, f * 2.2, f, 0.03), env_exp(ns(dur), 0.22, attack=0.001))
    harm = mul(osc(dur, f * 2.0, "tri"), env_exp(ns(dur), 0.08))
    out = saturate(mix((body, 1.0), (harm, 0.3)), 2.4)
    return finish(lp(out, 2500))


def chord_stab(rng, notes, shape="saw", dur=1.0, decay=0.35, cutoff=2500.0, detune=7.0,
               verb=0.2, crush=None):
    voices = []
    for k, n in enumerate(notes):
        f = midi_hz(n)
        voices.append((osc(dur, f, shape, detune_cents=-detune, phase=0.11 * k), 0.5))
        voices.append((osc(dur, f, shape, detune_cents=detune, phase=0.37 * k), 0.5))
    src = mix(*voices)
    src = lp(src, cutoff, 0.9)
    out = mul(src, env_exp(ns(dur), decay, attack=0.003))
    if crush:
        out = bitcrush(out, *crush)
    if verb > 0:
        out = reverb(out, 0.6, 0.4, verb, 0.8)
    return finish(out)


def epiano(rng, note=60, dur=2.5):
    """2-operator FM electric piano."""
    f = midi_hz(note)
    out = []
    n = ns(dur)
    for i in range(n):
        t = i / SR
        mod_env = math.exp(-t / 0.35)
        amp_env = math.exp(-t / 0.9) * min(1.0, t / 0.002)
        m = math.sin(TWO_PI * f * t) * 1.8 * mod_env
        c = math.sin(TWO_PI * f * t + m)
        bell = math.sin(TWO_PI * f * 7.0 * t) * math.exp(-t / 0.05) * 0.15
        out.append((c + bell) * amp_env)
    return finish(out, fade_out_ms=30)


def pluck(rng, note=60, dur=2.0, damping=0.996, brightness=0.5):
    """Karplus-Strong plucked string."""
    period = int(round(SR / midi_hz(note)))
    buf = [rng.uniform(-1, 1) for _ in range(period)]
    buf = lp(buf, 2000 + 8000 * brightness)
    mean = sum(buf) / len(buf)
    buf = [v - mean for v in buf]
    out = []
    idx = 0
    for _ in range(ns(dur)):
        a = buf[idx]
        b = buf[(idx + 1) % period]
        v = damping * 0.5 * (a + b)
        out.append(a)
        buf[idx] = v
        idx = (idx + 1) % period
    return finish(hp(out, 30), fade_out_ms=30)


def soft_pad(rng, note=60, dur=3.0):
    f = midi_hz(note)
    parts = []
    for k, cents in enumerate((-9, -3, 4, 10)):
        parts.append((osc(dur, f, "saw", detune_cents=cents, phase=0.21 * k), 0.25))
        parts.append((osc(dur, f * 2.0, "tri", detune_cents=cents, phase=0.4 * k), 0.08))
    src = lp(mix(*parts), 1800, 0.6)
    env = env_adsr(ns(dur), 0.35, 0.5, 0.75, dur - 0.9, 0.9)
    return finish(reverb(mul(src, env), 0.7, 0.3, 0.25, 1.0), fade_out_ms=40)


def sub_bass(rng, note=36, dur=1.2):
    f = midi_hz(note)
    body = mul(osc(dur, f, "sine"), env_adsr(ns(dur), 0.004, 0.2, 0.7, dur - 0.25, 0.25))
    harm = mul(osc(dur, f * 2, "sine"), env_exp(ns(dur), 0.15))
    return finish(saturate(mix((body, 1.0), (harm, 0.2)), 1.3), fade_out_ms=20)


def house_bass(rng, note=36, dur=0.6):
    f = midi_hz(note)
    src = mix((osc(dur, f, "saw"), 0.6), (osc(dur, f, "square", detune_cents=5), 0.4))
    out = mul(lp(src, 700, 1.4), env_exp(ns(dur), 0.18, attack=0.002))
    return finish(saturate(out, 1.5))


def riser(rng, dur=2.0):
    n = ns(dur)
    nz = noise(dur, rng)
    out = []
    # Swept band-pass approximated by successive short filtered segments.
    seg = ns(0.05)
    for s in range(0, n, seg):
        frac = s / n
        centre = 400 + 9000 * frac ** 2
        chunk = bp(nz[s:s + seg], centre, 2.0)
        out.extend(chunk)
    env = [min(1.0, (i / n) ** 1.6) for i in range(len(out))]
    return finish(mul(out, env), fade_out_ms=15)


def impact(rng, dur=2.0):
    boom = mul(sine_sweep(dur, 120, 32, 0.15), env_exp(ns(dur), 0.6))
    nz = mul(lp(noise(dur, rng), 3000), env_exp(ns(dur), 0.25))
    out = reverb(saturate(mix((boom, 1.0), (nz, 0.4)), 1.6), 0.8, 0.3, 0.35, 1.2)
    return finish(out, fade_out_ms=40)


def vinyl(rng, dur=2.0):
    out = [rng.gauss(0, 0.02) for _ in range(ns(dur))]
    out = lp(out, 4000)
    for _ in range(int(dur * 18)):
        p = rng.randrange(0, len(out) - 40)
        amp = rng.uniform(0.3, 1.0) * (1 if rng.random() < 0.5 else -1)
        for k in range(30):
            out[p + k] += amp * math.exp(-k / 4.0)
    return finish(hp(out, 300))


def vox_chop(rng, note=64, dur=0.6, vowel="ah"):
    """Formant-filtered pulse train: a synthetic 'ah'/'oh' vocal chop."""
    formants = {"ah": [(800, 1.0), (1150, 0.5), (2900, 0.25)],
                "oh": [(450, 1.0), (800, 0.6), (2830, 0.15)],
                "ee": [(300, 1.0), (2300, 0.5), (3000, 0.3)]}[vowel]
    f = midi_hz(note)
    src = osc(dur, f, "saw", detune_cents=0)
    vib = 0
    parts = [(bp(src, fr, 6.0), g) for fr, g in formants]
    out = mix(*parts)
    env = env_adsr(len(out), 0.01, 0.1, 0.8, dur - 0.12, 0.12)
    out = mul(out, env)
    return finish(reverb(out, 0.4, 0.5, 0.15, 0.4))


def lofi(x, bits=12, ds=2, cutoff=6000):
    return finish(lp(bitcrush(x, bits, ds), cutoff))


# ---------------------------------------------------------------------------
# Kits
# ---------------------------------------------------------------------------
# Pad layout (MPC style, pad 1 = bottom-left):
#  13 14 15 16
#   9 10 11 12
#   5  6  7  8
#   1  2  3  4

def kit_trap(rng):
    return {
        "name": "Trap", "genre": "Trap", "bpm": 140, "swing": 50, "key": "F minor",
        "pads": [
            ("Kick", kick(rng, 170, 50, 0.03, 0.28, 0.45, 1.8), 0, 36),
            ("Kick Hard", kick(rng, 190, 46, 0.025, 0.22, 0.6, 2.6), 0, 36),
            ("Snare", snare(rng, 200, 0.07, 0.14, 1600, 1.0, 0.5, drive=1.4), 0, 38),
            ("Clap", clap(rng, 4, 0.01, 0.16, 1250, 0.2), 0, 39),
            ("Hat", hat(rng, 0.035, 0.25, 1.1, 10000), 1, 42),
            ("Open Hat", hat(rng, 0.28, 0.8, 1.1, 9500), 1, 46),
            ("Rim", rim(rng), 0, 37),
            ("Snap", snap(rng), 0, 40),
            ("Tom Low", tom(rng, 160, 95), 0, 41),
            ("Tom High", tom(rng, 240, 150), 0, 43),
            ("Shaker", shaker(rng), 0, 70),
            ("Crash", cymbal(rng), 0, 49),
            ("808", bass808(rng, 29, 2.4, 1.0, 0.0, 2.2), 0, 29),
            ("Perc", woodblock(rng, 900), 0, 76),
            ("Riser", riser(rng, 2.0), 0, 60),
            ("Dark Stab", chord_stab(rng, [53, 56, 60, 63], "saw", 1.2, 0.4, 1800, 9, 0.25), 0, 53),
        ],
        "pattern": {
            1: "x......x..x.....",
            3: "........x.......",
            4: "........x.......",
            5: "x.x.x.x.x.xxx.x.",
            6: "...............o",
            13: "x......x..x.....",
        },
    }


def kit_boombap(rng):
    return {
        "name": "Boom Bap", "genre": "Hip Hop", "bpm": 90, "swing": 58, "key": "D minor",
        "pads": [
            ("Kick", lofi(kick(rng, 120, 52, 0.05, 0.3, 0.2, 1.6, noise_amt=0.6), 14, 1, 7000), 0, 36),
            ("Kick Dusty", lofi(kick(rng, 110, 50, 0.06, 0.25, 0.1, 2.0, noise_amt=0.8), 10, 2, 5000), 0, 36),
            ("Snare", lofi(snare(rng, 180, 0.1, 0.2, 900, 1.0, 0.9, verb=0.25, drive=1.6), 12, 1, 8000), 0, 38),
            ("Clap", lofi(clap(rng, 3, 0.012, 0.2, 1000, 0.3), 12, 1, 7000), 0, 39),
            ("Hat", hat(rng, 0.04, 0.25, 0.9, 7500, lowpass=9000), 1, 42),
            ("Open Hat", hat(rng, 0.22, 0.7, 0.9, 7000, lowpass=9000), 1, 46),
            ("Rim", lofi(rim(rng), 10, 2, 6000), 0, 37),
            ("Tambourine", shaker(rng, 0.25, 8000, 0.002, 0.07), 0, 54),
            ("Tom Low", tom(rng, 140, 90, 0.3), 0, 41),
            ("Tom High", tom(rng, 210, 130, 0.25), 0, 43),
            ("Shaker", shaker(rng, 0.2, 5500), 0, 70),
            ("Ride", cymbal(rng, 0.7, 1.8, 2.1, 8000), 0, 51),
            ("Sub Bass", sub_bass(rng, 38, 1.0), 0, 38),
            ("Vinyl", vinyl(rng, 2.0), 0, 60),
            ("Jazz Chord", chord_stab(rng, [50, 53, 57, 60, 64], "tri", 1.6, 0.7, 1400, 5, 0.3, (10, 2)), 0, 50),
            ("Vox Oh", vox_chop(rng, 62, 0.7, "oh"), 0, 62),
        ],
        "pattern": {
            1: "x......x..x.....",
            3: "....x.......x...",
            5: "x.x.x.x.x.x.x.x.",
            11: "..x...x...x...x.",
        },
    }


def kit_house(rng):
    return {
        "name": "House", "genre": "House", "bpm": 124, "swing": 52, "key": "A minor",
        "pads": [
            ("Kick", kick(rng, 140, 52, 0.035, 0.24, 0.5, 1.5), 0, 36),
            ("Kick Deep", kick(rng, 120, 45, 0.045, 0.35, 0.3, 1.3), 0, 36),
            ("Snare", snare(rng, 210, 0.06, 0.15, 1800, 0.9, 0.4, verb=0.15), 0, 38),
            ("Clap", clap(rng, 4, 0.0095, 0.22, 1300, 0.35), 0, 39),
            ("Hat", hat(rng, 0.03, 0.2, 1.2, 11000, 0.6), 1, 42),
            ("Open Hat", hat(rng, 0.16, 0.5, 1.2, 10000, 0.6), 1, 46),
            ("Rim", rim(rng), 0, 37),
            ("Conga", conga(rng, 300), 0, 63),
            ("Conga Low", conga(rng, 210), 0, 64),
            ("Bongo", conga(rng, 420, 0.3, 0.09), 0, 60),
            ("Shaker", shaker(rng, 0.15, 7000, 0.008, 0.035), 0, 70),
            ("Ride", cymbal(rng, 0.6, 1.6, 2.4, 9000), 0, 51),
            ("Bass", house_bass(rng, 33), 0, 33),
            ("Cowbell", cowbell(rng), 0, 56),
            ("Impact", impact(rng), 0, 60),
            ("Organ Stab", chord_stab(rng, [57, 60, 64, 67], "square", 0.8, 0.25, 3000, 4, 0.3), 0, 57),
        ],
        "pattern": {
            1: "x...x...x...x...",
            4: "....x.......x...",
            5: "..x...x...x...x.",
            6: "..o...o...o...o.",
            11: "oooooooooooooooo",
        },
    }


def kit_drill(rng):
    return {
        "name": "Drill", "genre": "Drill", "bpm": 142, "swing": 50, "key": "C# minor",
        "pads": [
            ("Kick", kick(rng, 180, 48, 0.028, 0.25, 0.5, 2.2), 0, 36),
            ("Kick Short", kick(rng, 200, 55, 0.02, 0.15, 0.6, 2.4), 0, 36),
            ("Snare", snare(rng, 230, 0.05, 0.13, 2000, 1.0, 0.45, drive=1.8), 0, 38),
            ("Rim Snare", rim(rng, 0.15), 0, 37),
            ("Hat", hat(rng, 0.028, 0.2, 1.25, 10500), 1, 42),
            ("Open Hat", hat(rng, 0.2, 0.6, 1.25, 9500), 1, 46),
            ("Clap", clap(rng, 3, 0.009, 0.12, 1400, 0.1), 0, 39),
            ("Woodblock", woodblock(rng, 1200), 0, 76),
            ("Tom Low", tom(rng, 150, 90), 0, 41),
            ("Tom High", tom(rng, 230, 140), 0, 43),
            ("Shaker", shaker(rng, 0.14), 0, 70),
            ("Crash", cymbal(rng, 1.0, 2.2, 1.7), 0, 49),
            ("808 Slide", bass808(rng, 25, 2.6, 1.2, 7.0, 2.8), 0, 25),
            ("Bell", woodblock(rng, 1800), 0, 77),
            ("Riser", riser(rng, 1.6), 0, 60),
            ("Dark Keys", chord_stab(rng, [49, 52, 56, 61], "tri", 1.4, 0.6, 2200, 3, 0.35), 0, 49),
        ],
        "pattern": {
            1: "x..........x....",
            3: "......x.......x.",
            5: "x..x..x.x..x..x.",
            13: "x..........x....",
            8: "...........o....",
        },
    }


def kit_lofi(rng):
    def crush(x):
        return lofi(x, 10, 2, 5500)
    return {
        "name": "Lo-Fi", "genre": "Lo-Fi", "bpm": 82, "swing": 60, "key": "E minor",
        "pads": [
            ("Kick", crush(kick(rng, 110, 55, 0.05, 0.25, 0.1, 1.3, noise_amt=0.4)), 0, 36),
            ("Kick Soft", crush(kick(rng, 100, 55, 0.06, 0.22, 0.05, 1.1, noise_amt=0.3)), 0, 36),
            ("Snare", crush(snare(rng, 190, 0.09, 0.18, 800, 0.8, 0.8, verb=0.3)), 0, 38),
            ("Rim", crush(rim(rng)), 0, 37),
            ("Hat", crush(hat(rng, 0.035, 0.25, 0.85, 7000)), 1, 42),
            ("Open Hat", crush(hat(rng, 0.2, 0.6, 0.85, 6500)), 1, 46),
            ("Snap", crush(snap(rng)), 0, 40),
            ("Shaker", crush(shaker(rng, 0.2, 5000)), 0, 70),
            ("Tom Low", crush(tom(rng, 130, 85)), 0, 41),
            ("Tom High", crush(tom(rng, 190, 120)), 0, 43),
            ("Vinyl", vinyl(rng, 2.0), 0, 60),
            ("Ride", crush(cymbal(rng, 0.6, 1.6, 2.0, 7000)), 0, 51),
            ("Sub Bass", sub_bass(rng, 40, 1.0), 0, 40),
            ("Rhodes", crush(epiano(rng, 64, 2.0)), 0, 64),
            ("Pad", crush(soft_pad(rng, 52, 2.5)), 0, 52),
            ("Vox Ah", crush(vox_chop(rng, 64, 0.8, "ah")), 0, 64),
        ],
        "pattern": {
            1: "x........x......",
            3: "....x.......x...",
            5: "x.o.x.o.x.o.x.o.",
            4: "...............o",
        },
    }


def kit_afro(rng):
    return {
        "name": "Afro", "genre": "Afrobeats / Amapiano", "bpm": 108, "swing": 54, "key": "G minor",
        "pads": [
            ("Kick", kick(rng, 130, 50, 0.04, 0.26, 0.3, 1.4), 0, 36),
            ("Kick Round", kick(rng, 115, 48, 0.05, 0.32, 0.2, 1.2), 0, 36),
            ("Snare", snare(rng, 220, 0.06, 0.12, 1800, 0.8, 0.5, verb=0.1), 0, 38),
            ("Clap", clap(rng, 4, 0.01, 0.18, 1200, 0.25), 0, 39),
            ("Hat", hat(rng, 0.03, 0.2, 1.15, 10500), 1, 42),
            ("Open Hat", hat(rng, 0.18, 0.5, 1.15, 9500), 1, 46),
            ("Rim", rim(rng), 0, 37),
            ("Shaker", shaker(rng, 0.16, 6500, 0.01, 0.04), 0, 70),
            ("Conga", conga(rng, 330), 0, 63),
            ("Conga Low", conga(rng, 230), 0, 64),
            ("Bongo", conga(rng, 450, 0.3, 0.08), 0, 60),
            ("Agogo", woodblock(rng, 1400), 0, 67),
            ("Log Drum", log_drum(rng, 43), 0, 43),
            ("Cowbell", cowbell(rng), 0, 56),
            ("Pluck", pluck(rng, 67, 1.5, 0.995, 0.6), 0, 67),
            ("Piano Chord", chord_stab(rng, [55, 58, 62, 65], "tri", 1.2, 0.45, 2600, 3, 0.2), 0, 55),
        ],
        "pattern": {
            1: "x.....x.x.......",
            4: "....x.......x...",
            8: "xxxxxxxxxxxxxxxx".replace("x", "o"),
            7: "x..x..x...x.x...",
            9: "..x.....x.x...x.",
            13: "x.....x.....x...",
        },
    }


KITS = [kit_trap, kit_boombap, kit_house, kit_drill, kit_lofi, kit_afro]


def instruments(rng):
    return [
        ("E-Piano", epiano(rng, 60, 2.5), 60),
        ("Pluck", pluck(rng, 60, 2.0), 60),
        ("Soft Pad", soft_pad(rng, 60, 3.0), 60),
        ("Sub Bass", sub_bass(rng, 36, 1.4), 36),
        ("808 Long", bass808(rng, 36, 3.0, 1.4, 0.0, 1.8), 36),
        ("Vox Ah", vox_chop(rng, 60, 1.0, "ah"), 60),
        ("Vox Ee", vox_chop(rng, 60, 1.0, "ee"), 60),
    ]


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def write_wav(path, samples, bits=24):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    scale = (2 ** (bits - 1)) - 1
    frames = bytearray()
    for v in samples:
        v = max(-1.0, min(1.0, v))
        iv = int(round(v * scale))
        if bits == 24:
            frames += struct.pack("<i", iv)[:3]
        else:
            frames += struct.pack("<h", iv)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(bits // 8)
        w.setframerate(SR)
        w.writeframes(bytes(frames))


def safe_name(name):
    return "".join(c for c in name if c.isalnum() or c in " -_").strip()


def build(out_dir, only=None):
    os.makedirs(out_dir, exist_ok=True)
    pack = {
        "name": "Merge Factory",
        "author": "Merge contributors (synthesized by tools/samplegen/generate.py)",
        "license": "CC0-1.0",
        "redistribution_allowed": True,
        "generator_version": GENERATOR_VERSION,
        "description": "Original drum kits and instruments synthesized from scratch for Merge.",
    }
    with open(os.path.join(out_dir, "pack.json"), "w") as f:
        json.dump(pack, f, indent=2)

    for idx, make in enumerate(KITS):
        rng = random.Random(1000 + idx)
        kit = make(rng)
        if only and kit["name"] not in only:
            continue
        kit_dir = os.path.join(out_dir, "Kits", kit["name"])
        pads = []
        for pad_index, (name, samples, choke, root) in enumerate(kit["pads"], start=1):
            fname = "%02d %s.wav" % (pad_index, safe_name(name))
            write_wav(os.path.join(kit_dir, fname), samples)
            pads.append({"pad": pad_index, "name": name, "file": fname,
                         "choke": choke, "root": root})
        manifest = {
            "name": kit["name"],
            "order": idx,
            "genre": kit["genre"],
            "bpm": kit["bpm"],
            "swing": kit["swing"],
            "key": kit["key"],
            "license": "CC0-1.0",
            "author": "Merge contributors",
            "pads": pads,
            "pattern": {"length": 16,
                        "steps": {str(k): v for k, v in sorted(kit["pattern"].items())}},
        }
        with open(os.path.join(kit_dir, "kit.json"), "w") as f:
            json.dump(manifest, f, indent=2)
        print("kit %-10s %2d sounds" % (kit["name"], len(pads)))

    if not only:
        rng = random.Random(7)
        inst_dir = os.path.join(out_dir, "Instruments")
        entries = []
        for name, samples, root in instruments(rng):
            fname = safe_name(name) + ".wav"
            write_wav(os.path.join(inst_dir, fname), samples)
            entries.append({"name": name, "file": fname, "root": root})
        with open(os.path.join(inst_dir, "instruments.json"), "w") as f:
            json.dump({"license": "CC0-1.0", "instruments": entries}, f, indent=2)
        print("instruments: %d" % len(entries))

    with open(os.path.join(out_dir, ".stamp"), "w") as f:
        f.write(str(GENERATOR_VERSION))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True, help="output folder (e.g. build/Samples/Factory)")
    ap.add_argument("--kit", action="append", help="only build the named kit(s)")
    args = ap.parse_args()
    build(args.out, args.kit)
    return 0


if __name__ == "__main__":
    sys.exit(main())

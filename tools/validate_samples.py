#!/usr/bin/env python3
"""Validate sample packs before they are bundled into Merge.

Every pack root must contain a pack.json that states its license and confirms
that the samples may be redistributed inside an application. Most commercial
sample packs only allow use *in your music*, not redistribution of the sounds
themselves, so packaging is blocked unless the pack explicitly says it's allowed.

Checks per pack:
  * pack.json present with name, author, license and redistribution_allowed = true
  * every kit folder has a valid kit.json whose files exist
  * every WAV is readable PCM, not silent and not clipping (peak <= -0.3 dBFS)

Usage:
    python3 tools/validate_samples.py <pack-or-folder-of-packs> [...]
Exit code 0 = everything OK, 1 = problems found.
"""

import json
import math
import os
import struct
import sys
import wave

AUDIO_EXT = {".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3"}
PEAK_LIMIT_DB = -0.3
SILENCE_DB = -60.0
MAX_SECONDS = 120.0
REQUIRED_PACK_KEYS = ("name", "author", "license", "redistribution_allowed")


def wav_stats(path):
    with wave.open(path, "rb") as w:
        ch, width, rate, frames = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        data = w.readframes(frames)
    if width not in (2, 3, 4):
        raise ValueError("unsupported sample width %d bytes" % width)
    full = float(2 ** (8 * width - 1))
    peak = 0
    step = width
    if width == 2:
        vals = struct.unpack("<%dh" % (len(data) // 2), data)
        peak = max((abs(v) for v in vals), default=0)
    elif width == 4:
        vals = struct.unpack("<%di" % (len(data) // 4), data)
        peak = max((abs(v) for v in vals), default=0)
    else:
        for i in range(0, len(data) - 2, step):
            v = data[i] | (data[i + 1] << 8) | (data[i + 2] << 16)
            if v & 0x800000:
                v -= 0x1000000
            if abs(v) > peak:
                peak = abs(v)
    peak_db = 20 * math.log10(peak / full) if peak > 0 else -math.inf
    return {"channels": ch, "rate": rate, "seconds": frames / float(rate), "peak_db": peak_db}


def find_pack_roots(paths):
    roots = []
    for p in paths:
        if os.path.isfile(os.path.join(p, "pack.json")):
            roots.append(p)
            continue
        if not os.path.isdir(p):
            continue
        for name in sorted(os.listdir(p)):
            sub = os.path.join(p, name)
            if os.path.isdir(sub) and not name.startswith("."):
                roots.append(sub)
    return roots


def validate_pack(root):
    problems = []
    manifest_path = os.path.join(root, "pack.json")
    if not os.path.isfile(manifest_path):
        return ["%s: missing pack.json (see samples/user-packs/README.md)" % root], 0
    try:
        with open(manifest_path) as f:
            manifest = json.load(f)
    except ValueError as e:
        return ["%s: pack.json is not valid JSON (%s)" % (root, e)], 0
    for key in REQUIRED_PACK_KEYS:
        if key not in manifest or manifest[key] in ("", None):
            problems.append("%s: pack.json missing '%s'" % (root, key))
    if manifest.get("redistribution_allowed") is not True:
        problems.append("%s: redistribution_allowed must be true to bundle this pack "
                        "(check the pack's license - most packs forbid redistributing samples)" % root)

    count = 0
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if not d.startswith("."))
        if "kit.json" in filenames:
            try:
                with open(os.path.join(dirpath, "kit.json")) as f:
                    kit = json.load(f)
                for pad in kit.get("pads", []):
                    if not os.path.isfile(os.path.join(dirpath, pad.get("file", ""))):
                        problems.append("%s: kit.json references missing file %r" % (dirpath, pad.get("file")))
            except ValueError as e:
                problems.append("%s: kit.json invalid (%s)" % (dirpath, e))
        for fn in sorted(filenames):
            ext = os.path.splitext(fn)[1].lower()
            if ext not in AUDIO_EXT:
                continue
            count += 1
            path = os.path.join(dirpath, fn)
            if ext != ".wav":
                continue  # other formats are checked by the app's decoder at load time
            try:
                st = wav_stats(path)
            except (wave.Error, ValueError, EOFError, struct.error) as e:
                problems.append("%s: unreadable WAV (%s)" % (path, e))
                continue
            if st["peak_db"] > PEAK_LIMIT_DB:
                problems.append("%s: peak %.2f dBFS is above %.1f dBFS (clipping risk)" % (path, st["peak_db"], PEAK_LIMIT_DB))
            if st["peak_db"] < SILENCE_DB:
                problems.append("%s: silent (peak %.1f dBFS)" % (path, st["peak_db"]))
            if st["seconds"] > MAX_SECONDS:
                problems.append("%s: %.0f s long, one-shots/loops should be under %d s" % (path, st["seconds"], MAX_SECONDS))
    if count == 0:
        problems.append("%s: no audio files found" % root)
    return problems, count


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    roots = find_pack_roots(argv[1:])
    if not roots:
        print("No packs found in: %s" % ", ".join(argv[1:]))
        return 0
    all_problems = []
    for root in roots:
        problems, count = validate_pack(root)
        status = "OK" if not problems else "FAILED"
        print("[%s] %s (%d audio files)" % (status, root, count))
        all_problems.extend(problems)
    for p in all_problems:
        print("  - " + p)
    return 1 if all_problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

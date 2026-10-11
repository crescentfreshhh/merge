# Merge

**The beatmaker and the DJ app in one.** Make beats like FL Studio, an MPC or Maschine, then
mix them, and any other track, like you would in Serato, Rekordbox or Traktor. One window,
one engine, one library.

- **Make**: 4x4 pads, an FL-style step sequencer, piano roll, song arrangement, mixer with
  insert effects, VST3 instruments/effects, 6 original genre kits (Trap, Boom Bap, House,
  Drill, Lo-Fi, Afro), note repeat, swing, live recording, one-click pattern generator.
- **Mix**: 2 or 4 decks with keylock, beat-sync, hot cues, loops, beat jump, scratching,
  3-band isolator EQ, filter, FX, crossfader, headphone cue, a music library with BPM /
  key (Camelot) analysis, stem separation, Automix and set recording.
- **Merge's own tricks**: *Flip* a track's stems straight onto the pads, send a finished
  beat to the DJ library already analysed, and play your patterns live as remix decks
  locked to the master deck.

Merge is free and open source (GPL-3.0-or-later). See [`docs/`](docs) for the roadmap and
the feature research behind it.

## Download

Every push builds Windows, macOS and Linux versions with GitHub Actions. Open the latest
run under **Actions -> Build**, then download:

- `Merge-Windows` -> `Merge-Setup-x.y.z.exe` (installer) or the portable zip
- `Merge-macOS` -> `Merge-x.y.z-macos.zip`

Tagged versions (`v0.6.0`, ...) are published as GitHub Releases.

> The Windows build isn't code-signed yet, so SmartScreen may warn the first time:
> click **More info -> Run anyway**.

## Build it yourself

Requirements: CMake 3.22+, a C++20 compiler (Visual Studio 2022+, Xcode 15+, or GCC 12+ /
Clang 15+) and Python 3 (for the factory sample generator).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The app is in `build/Merge_artefacts/Release/`. Dependencies (JUCE, Signalsmith Stretch,
SQLite) are downloaded automatically by CMake.

On Linux, install the JUCE dependencies first:

```bash
sudo apt-get install libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
  libxext-dev libxi-dev libxcomposite-dev libxrender-dev libfreetype-dev libfontconfig1-dev \
  libgl1-mesa-dev libglu1-mesa-dev
```

## Samples

- **Factory library**: generated from scratch by `tools/samplegen/generate.py` at build time.
  Every sound is original and CC0, so it's safe to ship.
- **Your packs**: see [`samples/user-packs/README.md`](samples/user-packs/README.md). Each pack
  needs a `pack.json` confirming it may be redistributed. CI blocks packs that don't.
- **Personal sounds**: drop anything into `Documents/Merge/Samples`; Merge browses it.

## Project layout

```
src/core        real-time primitives (lock-free queues, snapshots, sample buffers, DSP)
src/engine      beatmaker engine: sequencer, instruments, effects, project model, plugins
src/dj          DJ engine: decks (keylock, sync, cues, loops), mixer, recorder
src/analysis    BPM / beatgrid, key, waveform, loudness, transient detection
src/stems       stem separation (built-in DSP + optional Demucs/ONNX)
src/library     SQLite music library, Rekordbox XML import
src/controllers MIDI learn and controller mappings
src/ui, src/app the interface and app wiring
tests           unit tests (run with ctest)
tools           sample generator and pack validator
installer       Inno Setup script for the Windows installer
```

# Merge

**The beatmaker and the DJ app in one.** Make beats like FL Studio, an MPC or Maschine, then
mix them, and any other track, like you would in Serato, Rekordbox or Traktor. One window,
one engine, one library.

- **Make**: 4x4 pads (mouse, computer keyboard or MIDI), an FL-style step sequencer with
  accents, MPC swing and note repeat, piano roll with in-key chord drawing, 16 patterns, song
  arrangement with audio clips, mixer with 11 built-in effects plus VST3 instruments and effects,
  6 original genre kits (Trap, Boom Bap, House, Drill, Lo-Fi, Afro) plus melodic instruments,
  live recording, a one-click **Generate** pattern button, undo/redo, autosave, and export to
  WAV/FLAC/OGG.
- **Mix**: 2 or 4 decks with keylock, beat-sync, CDJ cue, 8 hot cues, loops, rolls, beat jump,
  scratch/nudge jog, a 3-band isolator EQ with kills, filter, FX, crossfader with scratch curve,
  headphone cue, and a music library with BPM, beat-grid, key (Camelot), loudness and colour
  waveform analysis. It also has crates, compatible-key filtering, Rekordbox XML import, Automix
  and set recording.
- **Merge's own tricks**:
  - **Stems** split any track into drums, bass, vocals and melody.
  - **Flip** chops a loop or a single stem onto the pads at the track's tempo.
  - **Send to Mix** puts a finished beat in the DJ library, already analysed.
  - The **remix deck** plays your patterns live, locked to the master deck.
  - **Set to stems**: open a recorded DJ set in the arrangement, one lane per deck.
- **MIDI learn** for every control, with shareable mapping files.

Merge is free and open source (GPL-3.0-or-later). Docs:
[user guide](docs/USER_GUIDE.md), [feature research](docs/FEATURE_FUSION.md),
[architecture](docs/ARCHITECTURE.md), [samples](docs/SAMPLES.md),
[controllers](docs/CONTROLLERS.md), [roadmap](docs/ROADMAP.md).

## Download

Every push builds and tests Merge on Windows, macOS and Linux with GitHub Actions. Open the
latest run under **Actions -> Build**, then download:

- `Merge-Windows` -> `Merge-Setup-x.y.z.exe` (installer) or the portable zip
- `Merge-macOS` -> `Merge-x.y.z-macos.zip`

Push a tag such as `v0.9.0` and the same workflow publishes a GitHub Release with the installer
and zips attached.

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

# Roadmap

Each milestone ships a working build: CI produces `Merge.exe` plus the installer on every push.

| Milestone | Scope | Status |
|---|---|---|
| M0 Foundation | CMake + JUCE app, Make/Mix modes, audio/MIDI settings, factory sample generator + license validator, CI, installer | Done |
| M1 Pad beatmaker | 4x4 pads (mouse, keyboard, MIDI), step grid, MPC swing, live record, kits, Generate, save/open, export | Done |
| M2 Full beatmaker | 16 patterns, piano roll with chords, 808 / synth / keys, song playlist with audio clips, mixer + inserts, VST3, note repeat | Done |
| M3 Mix | SQLite library, BPM/grid/key/loudness/waveform analysis, 2/4 decks, keylock, sync, cues, loops, rolls, isolator EQ, filter, FX, crossfader, headphone cue, Rekordbox XML | Done |
| M4 Stems & AI | Quick Stems (DSP) with cache, stem pads, Flip, Automix, Send to Mix | Done (HQ Demucs: see below) |
| M5 Hardware & pro | MIDI learn + mapping files, 4 decks, FX per deck, set recorder + open-in-Make, remix deck, ASIO (when the SDK is available in CI) | Done |
| M6 Ship | Update checker, first-run tour, autosave/recovery, release-on-tag workflow, docs | Done |

## Next

- **HQ stems (Demucs v4 / ONNX)**:
  - Needs a model-conversion step (PyTorch, then ONNX) that can't run in the environment this
    version was built in.
  - Plan: add a manual GitHub Actions workflow that converts HT-Demucs with `sevagh/demucs.onnx`
    (MIT) and publishes the model as a release asset, then run it through ONNX Runtime in
    `src/stems` behind the existing stem service (download on first use).
- **Real-time stems**: separate on the fly instead of in the background.
- **Controller presets**: ready-made mappings for popular controllers (Pioneer DDJ, Numark, Hercules,
  Traktor S-series, Akai MPC/Force pads), and import of Mixxx mappings.
- **Plugins**: out-of-process plugin scanning and crash isolation.
- **Signing**: code signing for Windows (e.g. SignPath for open source) and notarisation for macOS.
- **Memory on long files**: streamed decoding for very long tracks; decks currently decode whole
  files into memory.
- **More instruments and effects**: a wavetable synth, a sampler slicer view with transient
  markers, sidechain compression.
- **MP3 export**, stem export from Make, and cloud project sharing.

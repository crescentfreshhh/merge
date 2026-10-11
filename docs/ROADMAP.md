# Roadmap

Each milestone ships a working build (CI produces `Merge.exe` + installer on every push).

| Milestone | Scope | Status |
|---|---|---|
| M0 Foundation | CMake + JUCE app, Make/Mix modes, audio/MIDI settings, factory sample generator + license validator, CI, installer | Done |
| M1 Pad beatmaker | 4x4 pads (mouse, keyboard, MIDI), step grid, MPC swing, live record, kits, Generate, save/open, export | Done |
| M2 Full beatmaker | 16 patterns, piano roll, 808 / synth / keys instruments, song playlist with audio clips, mixer + inserts, VST3, note repeat | Done |
| M3 Mix | SQLite library, BPM/key/waveform analysis, 2/4 decks, keylock, sync, cues, loops, isolator EQ, filter, crossfader, headphone cue, Rekordbox XML | In progress |
| M4 Stems & AI | Quick Stems (DSP), HQ Stems (Demucs/ONNX), Flip, Automix, Send to Mix | Planned |
| M5 Hardware & pro | MIDI learn + mapping files, FX per deck, set recorder, remix decks, ASIO | Planned |
| M6 Ship | Update checker, onboarding, autosave/recovery, release workflow, docs | Planned |

## After the first release

- Real-time (rather than background) stem separation and better stem quality.
- A larger library of ready-made controller mappings (import of Mixxx mappings).
- Out-of-process plugin scanning and crash sandboxing.
- Code signing for Windows and notarisation for macOS.
- Faster loading on very large libraries (streamed decoding instead of whole-file).

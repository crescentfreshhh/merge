# Architecture

```
            message thread (UI, project model, library, controllers)
                 |  publish snapshots / post commands (lock-free)
                 v
   +----------------------------- audio thread -----------------------------+
   |  AudioEngine::audioDeviceIOCallback                                      |
   |    DjCore.process()  -> decks (keylock/sync/loops/stems) -> DJ mixer      |
   |        -> master bus + headphone (cue) bus; publishes the master clock   |
   |    BeatCore.process() -> sequencer -> channels (instruments + inserts)    |
   |        -> mixer -> beat bus  (optionally following the master clock)     |
   |    preview voice, soft clip, recorder taps (ThreadedWriter)              |
   |    outputs 1-2 master, 3-4 headphones (or split cue)                     |
   +--------------------------------------------------------------------------+
         background threads: analysis, stem separation, track loading, export, plugin scan
```

## Real-time rules
- The audio thread never locks or allocates. State crosses threads in three ways:
  - **Atomics** carry continuous controls: faders, EQ, pitch, transport flags.
  - **`SpscQueue`** (wait-free ring buffers) carries events: pad hits, deck commands, recorded
    notes, MIDI for the UI.
  - **`RtShared<T>`** publishes immutable snapshots. The message thread builds a `BeatSnapshot`
    (compiled patterns, song clips, channel list) and publishes it; the audio thread picks up the
    newest one at the start of each block and hands the old one back to be freed on the message
    thread.
- **`ReleasePool`** holds a reference to everything the audio thread can touch (sample buffers,
  instruments, effects, tracks). The audio thread may take and drop references, but the count
  never reaches zero there. A message-thread timer frees objects after two idle passes.

## Beatmaker (`src/engine`)
- **Project**: the document (channels, 16 patterns of steps and notes, song clips, lanes, master
  inserts). It serialises to JSON (`.merge`) and keeps an undo/redo stack.
- **BeatCore**: the sequencer.
  - Positions are tracked in beats with double precision, and events are scheduled at exact sample
    offsets, including across loop wraps.
  - It applies MPC swing, choke groups, note repeat, live record (with double-trigger suppression),
    pattern launch on the next bar, a metronome, and audio clips in song mode.
- **Instruments**: one-shot/gated sampler (cubic interpolation, glide), 808 bass, 2-osc PolyBLEP
  synth, and VST3 instruments through `PluginHost`.
- **Effects**: 11 built-ins and VST3 effects; all are `Effect` objects with realtime `Param`s.
- **OfflineRenderer**: renders with an independent clone of every instrument and effect, so export
  never disturbs live playback.

## DJ (`src/dj`)
- **Deck**: plays a `TrackData` (decoded audio at the device rate, plus optional stems).
  - Keylock uses Signalsmith Stretch, re-primed with `seek()` after jumps; vinyl mode uses cubic
    resampling.
  - Sync is tempo matching plus a gentle phase-lock loop on *audible* positions (latency
    compensated).
  - Also handles CDJ cue logic, hot cues, loops and scratch.
- **DjCore**: up to 4 decks plus the mixer.
  - Each channel has trim + auto gain and a Linkwitz-Riley 4th-order isolator EQ; the low band is
    all-pass compensated, so a flat EQ is flat.
  - Also: SVF filter, FX unit, faders, crossfader curves, headphone bus.
  - It computes the master clock that the beatmaker and synced decks follow.
- **Recorder**: master plus per-deck WAV via JUCE `ThreadedWriter`, guarded by a try-lock that the
  audio thread never blocks on.

## Analysis (`src/analysis`)
- **Tempo**:
  - A log spectral-flux onset envelope feeds an autocorrelation with a comb and a 120 BPM prior.
  - The period is refined to a precise BPM by a full-track comb, and the octave is decided by a
    backbeat test (kicks on 1/3, snares on 2/4).
  - The downbeat is picked from the four beat phases.
- **Key**: a weighted chromagram (55 Hz-2 kHz) correlated with Krumhansl-Kessler profiles; shown in
  Camelot.
- **Loudness**: ITU-R BS.1770 K-weighting with absolute and relative gating, used for auto gain.
- **Waveform**: low/mid/high peaks for the colour waveforms; transient detection for slicing.

## Stems (`src/stems`)
- **Quick Stems**:
  - STFT (4096/1024) feeds harmonic/percussive median filtering; the results are combined with
    frequency weights and a centre (mid) measure into four soft masks that always sum to 1.
  - The stems therefore add back up to the mix exactly.
  - It runs in the background with progress, and results are cached as FLAC per track.

## Library (`src/library`)
- **SQLite**: tracks, crates, crate tracks, cues and folders.
- **Tags**: ID3v2 plus RIFF/Vorbis tags, with a filename fallback.
- **Rekordbox XML import**.
- **`TrackLoader`**: decodes for analysis (integer-decimated mono, low memory) and for playback
  (stereo at the device rate).

## App wiring (`src/app`) and UI (`src/ui`)
- **AppContext** owns everything: engine, project, kits, plugins, DJ core, recorder, library
  service, stem service, deck controller, controller manager and Automix.
- **UI**: JUCE components with a custom LookAndFeel and the embedded Inter font. Views poll
  atomics on timers.

## Tests (`tests`)
`MergeTests` (run by `ctest` in CI on Windows, macOS and Linux) covers:
- sequencer timing and swing;
- song mode, mute/solo, choke groups, pan and sampler pitch;
- project save/load, undo, legato steps, recording quantise and the factory kits;
- tempo, key, loudness, transients and waveforms;
- keylock pitch, sync phase, loops, cues, EQ kills and crossfader curves;
- SQLite and Rekordbox import;
- stem reconstruction, the recorder and MIDI mappings.

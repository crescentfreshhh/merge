# Feature fusion: what Merge takes from the leaders

Research summary (October 2026) of what each leading program is known for, and what Merge
adopts. Sources are listed at the bottom.

## Beatmaking

| Program | Known for | In Merge |
|---|---|---|
| FL Studio 2025 | Channel Rack step sequencer, best-in-class piano roll, pattern -> playlist workflow, Loop Starter, stem separation (since 21.2) | Step grid + piano roll on the same pattern, PAT/SONG modes, kits that load with a groove, stems |
| Ableton Live 12.3 | Session View clip launching, warping, stems, Splice in the browser | Pattern launching on the next bar (remix decks), stems, a browser with preview |
| Akai MPC 3 / MPC Beats | 4x4 pads, MPC swing, note repeat, chop | Pads with velocity, MPC swing %, note repeat, slicer |
| NI Maschine | Hardware-style sampling, tag browser | Drag a sound onto a pad, kit browser |
| Logic Pro | Drummer / Session Players | "Generate" pattern button per genre |
| Serato Studio | Samples auto-match key and BPM | Project key, Camelot everywhere, stem Flip at the track's BPM |

## DJing

| Program | Known for | In Merge |
|---|---|---|
| Serato DJ Pro | Simple, stable 2-deck layout, cue pads, stems | Clean deck layout, hot cue / loop / roll / stem pads |
| Rekordbox | Library management, CDJ ecosystem | SQLite library, crates, compatible-key filter, Rekordbox XML import |
| Traktor Pro 4 | Remix decks, FX, beatgrids | FX unit per deck, remix decks driven by the beatmaker |
| VirtualDJ | Real-time stems, broad controller support | Stem faders per deck, MIDI learn + mapping files |
| djay Pro | Neural Mix, Automix AI | Automix with beat-matched, bass-swapped transitions |
| Mixxx (GPL) | Open-source analysis and controller mappings | Same license family; its approach informs our analysers |

## Only possible because it's one app

1. **Flip**: drag a deck's loop or bar onto the pads; each stem gets chopped into 16 slices at the track's tempo.
2. **Make -> Mix**: "Send to Mix" renders the beat into the DJ library, already analysed.
3. **Remix decks**: the beatmaker's pads and patterns play in sync with the master deck.
4. **Set -> project**: record a DJ set (master plus each deck) and open it as an arrangement.

## Simplicity rules

- One window, two modes (**Tab** switches).
- Sound within seconds: a kit and its groove are loaded at start, Space plays.
- Drag and drop everywhere: files onto pads, rows, lanes and decks.
- The computer keyboard plays the pads (Z X C V / A S D F / Q W E R / 1 2 3 4).
- No modal dialogs while playing.

## Sources

- Beatmaking: [Melodics 2026](https://melodics.com/blog/10-best-beatmaking-apps-in-2026), [iTechGuides](https://www.itechguides.com/best/beat-making-software/), [FL Studio 2025](https://www.image-line.com/fl-studio-news/fl-studio-2025-whats-new-2), [FL stems](https://djmag.com/tech/fl-studio-adds-stem-separation-and-ai-mastering-new-beta), [Live 12.3](https://mixdownmag.com.au/news/ableton-live-12-3-drops-overnight-with-stem-separation-and-splice-integration/), [MPC Studio](https://www.musicradar.com/news/akai-mpc-studio)
- DJ: [DJ.Studio comparison](https://dj.studio/blog/best-dj-software), [VirtualDJ vs Serato vs Rekordbox](https://virtualdj.com/articles/virtualdj-vs-serato-vs-rekordbox-which-dj-software), [Beatportal stems 2026](https://www.beatportal.com/videos/1377201-who-does-stem-separation-for-djs-best-all-dj-platforms-compared-2026-edition), [Mixxx KeyFinder](https://mixxx.org/news/2021-04-08-new-in-2-3-keyfinder), [Mixxx Demucs to ONNX](https://mixxx.org/news/2025-10-27-gsoc2025-demucs-to-onnx-dhunstack/)
- Licensing / tech: [VST3 MIT, ASIO GPLv3](https://librearts.org/2025/11/steinberg-relicenses-vst3-and-asio/), [JUCE 9](https://www.kvraudio.com/news/juce-9-now-available-67802), [Signalsmith Stretch](https://docs.rs/signalsmith-stretch), [HT-Demucs ONNX](https://huggingface.co/adowu/htdemucs-onnx)

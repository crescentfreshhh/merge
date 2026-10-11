# Merge user guide

Merge is two apps in one window: **MAKE** (beatmaker) and **MIX** (DJ). Switch with the
buttons at the top left or the **Tab** key. Both run all the time: a beat can keep playing
while you DJ, and a deck can keep playing while you make beats.

## First minute

1. Launch Merge. The Trap kit and its groove are loaded.
2. Press **Space** to play. Click steps in the grid to change the groove.
3. Play the pads with your computer keyboard. The layout matches an MPC, with pad 1 at bottom-left:

   | Row | Keys | Pads |
   |---|---|---|
   | top | 1 2 3 4 | 13 14 15 16 |
   |  | Q W E R | 9 10 11 12 |
   |  | A S D F | 5 6 7 8 |
   | bottom | Z X C V | 1 2 3 4 |

4. Click another kit on the left, or press **Generate** for a new pattern in the kit's style.

## MAKE: the beatmaker

### Pads and kits
- **Kits tab** loads 16 sounds plus the kit's groove, tempo, swing and key. Untick *Load the
  kit's groove* to keep your own pattern.
- **Sounds tab** browses the factory library, your packs and `Documents/Merge/Samples`.
  Click to preview, then drag a sound onto a pad or a channel row (double-click loads it onto
  the selected pad).
- **Right-click a pad** for: load sample, clear, choke group (closed hat cuts open hat),
  reverse, gate (stop on release).
- **Pad velocity**: clicking higher on a pad plays louder; Shift + key = accent.
- **Inspector** (right of the pads): sample waveform, volume, pan, tune, attack, release,
  start/end, velocity sensitivity, mono + glide, and 3 insert effects.

### Step grid (channel rack)
- Click a step to toggle it; **Shift+click** = accent; **right-click** = clear; drag to paint;
  mouse wheel = velocity.
- Each row has Mute, Solo, a volume bar and a pan bar (drag them; double-click pan to centre).
- **16/32/48/64 steps** per pattern; 16 patterns (A-P) in the top bar.
- **+ Channel** adds an 808 Bass, a Poly Synth, a Keys sampler (E-Piano, Pluck, Pad, Sub...)
  or an empty sampler. VST3 instruments are under *Instruments*.
- **Note repeat** (bottom right): hold a pad and it retriggers at 1/4 to 1/32 (or triplets).
- **Swing** (top bar): MPC-style, 50% = straight, ~58-62% = classic hip-hop.
- **Rec** (or Ctrl+R): while playing, pads and keys you play are written into the pattern,
  quantised to the nearest step (instrument notes keep their length).

### Piano roll
- Double-click a channel name (instruments), or open the *Piano Roll* tab.
- Click to draw, drag to move, drag the right edge to resize, right-click to delete,
  **Delete** removes the selection, **Ctrl+A** selects all.
- **Snap** from 1/4 to 1/32 and triplets; **Chords** draws triads or 7ths that stay in the
  project key; **Scale** highlights the key; the bottom lane edits velocity.

### Song
- The *Song* tab switches to song mode. Pick a pattern on the left, then click a lane to place it.
  Drag clips to move them, or drag their right edge to resize.
- Drag audio files in to make audio clips. Mute lanes with **M**.
- Drag in the ruler to set a loop region. Click the ruler to move the playhead.

### Mixer and effects
- A strip per channel plus master: 3 insert slots, pan, fader, meters, mute/solo.
- Built-in effects: EQ, Filter, Compressor, Delay (tempo-synced, ping-pong), Reverb, Distortion,
  Bitcrush, Flanger, Phaser, Beat Roll and Limiter. VST3 effects show up after *Menu > Scan VST3 plugins*.

### Saving and exporting
- **Ctrl+S** saves a `.merge` project (it references samples by path; factory sounds are portable).
- Autosave runs every 2 minutes; after a crash Merge reopens the recovered session.
- *Menu > Export*: pattern loop or the whole song as WAV, FLAC or OGG.
- **Send to Mix** renders the beat into the DJ library (*My Beats*) with its tempo and key, so it's
  ready to play on a deck straight away.

## MIX: the DJ side

### Library
- **+ Folder** adds a music folder (rescanned at every start). You can also drag files or folders
  onto the library.
- Every track is analysed in the background: BPM and beat grid, key (shown in Camelot, e.g. 8A),
  loudness (for auto gain) and a colour waveform.
- **Search** matches title, artist, genre, BPM or key. **Compatible keys** shows only tracks that
  mix harmonically with the master deck; **Near BPM** keeps tracks within 6%.
- **Crates** organise tracks. Right-click a track to add it to a crate or the Automix queue,
  re-analyse it, pre-split its stems, reveal it, or remove it.
- **Import Rekordbox**: in Rekordbox use *File > Export Collection in xml format*, then import that
  file. Merge brings in tracks, BPM/grids, keys, ratings, hot cues and playlists (as crates).
- **BPM range** (top right): set 88-175 if you play drum & bass, so tracks read as 174 rather than 87.

### Decks
- Drag a track onto a deck or waveform, or double-click it to load the first free deck.
- **PLAY**, **CUE** (CDJ style: when stopped, sets the cue and previews while held; when playing,
  returns to the cue), **SYNC** (matches tempo and beat phase to the master deck), **MASTER** (picks
  the tempo master; automatic otherwise), **KEY** (keylock), **Q** (quantise cues and loops to the
  grid).
- **x2 / /2** fix a half/double BPM, and **Grid** sets the downbeat at the playhead.
- **Pitch fader** with ±8 / ±16 / ±50% range (double-click to reset).
- **Jog wheel / waveform**: drag to scratch; Shift+drag the jog to nudge (pitch bend).
- **Pads**:
  - *Cues*: 8 hot cues (click to set or jump, Shift-click to delete; saved in the library).
  - *Loop*: auto loops from 1/4 to 32 beats, plus In/Out and halve/double.
  - *Roll*: momentary loop rolls that slip back into place.
  - *Stems*: see below.
- **FX**: pick an effect (filter, delay, reverb, flanger, phaser, bitcrush, beat roll, distortion)
  and set the amount. It is tempo-synced to the deck.

### Stems
- On the *Stems* pads, press **Split stems**. Merge separates the track into Drums, Bass, Vocals
  and Melody in the background, then caches them so the next load is instant.
- Tap a stem to mute or unmute it. *Acapella*, *Instrumental*, *Drums only* and *All* are one-tap presets.
- **FLIP** chops the deck's active loop (or the 4 bars starting at the current bar) onto the 16
  beatmaker pads. You can flip the full mix or a single stem. Merge sets the beat to the track's
  tempo and writes a pattern that replays the slices. The slices are saved in
  `Documents/Merge/Samples/Flips`.

### Mixer
- Each channel has trim, a 3-band isolator EQ (right-click a knob to kill the band), a filter
  (left = low-pass, right = high-pass), headphone **CUE**, a fader and a crossfader assign (A / - / B).
- Crossfader with a **SCRATCH** (sharp) curve, master volume, headphone *cue mix* and *phones* level.
  Outputs 1-2 are master and 3-4 are headphones. With a stereo-only interface, turn on *split cue* in
  *Audio & MIDI settings* (cue on the left, master on the right).
- **BEAT (remix deck)**: your beatmaker's level in the mix, with its own CUE, **SYNC** (locks the beat's
  tempo and phase to the master deck) and PLAY.
- **REC SET** records the master plus each deck. When you stop, *Open in Make* puts each deck on its own
  Song lane so you can edit your set.

### Remix deck
The *REMIX DECK* tab shows the beatmaker's pads and pattern launchers. Patterns launch on the next
bar, so you can play your own drums, chops and loops over the decks in time.

### Automix
Add tracks to the *Automix Queue* (right-click a track), open *AUTOMIX* and press **Start**. Merge
plays the queue on decks A/B with beat-matched crossfades, swaps the bass halfway through each
transition, and takes played tracks off the queue. Choose 8 to 64 beat transitions; **Mix now**
starts the next transition immediately.

## MIDI controllers
- Pads respond to MIDI notes 36-51 (the MPC / Maschine default) on any channel. A MIDI keyboard
  plays the selected instrument channel.
- *Menu > MIDI controller mapping* lists every action (deck transport, cues, loops, pitch, jog, EQ,
  filter, faders, crossfader, FX, beatmaker transport, pads, patterns, mode switch). Click **Learn**,
  then move the control on your hardware. Mappings save to `Documents/Merge/Mappings` and can be shared
  (see [CONTROLLERS.md](CONTROLLERS.md)).

## Keyboard shortcuts

| Keys | Action |
|---|---|
| Space | Play / pause the beat |
| Tab | Switch Make / Mix |
| Z X C V, A S D F, Q W E R, 1 2 3 4 | Pads 1-16 (Shift = accent) |
| Alt+1 ... Alt+9 | Select pattern A-I |
| Ctrl+Z / Ctrl+Y | Undo / redo (patterns and song) |
| Ctrl+S / Ctrl+Shift+S | Save / save as |
| Ctrl+O, Ctrl+N | Open, new |
| Ctrl+E | Export |
| Ctrl+R | Record |

## Audio setup and troubleshooting
- *Menu > Audio & MIDI settings* picks the interface, sample rate and buffer size. Smaller buffers
  give lower latency: 128-256 samples is a good place to start.
- Windows builds support WASAPI and DirectSound, plus ASIO when the build includes the Steinberg SDK
  (the CI fetches it automatically when it can).
- **SmartScreen** warning on first launch: the installer isn't code-signed yet. Click *More info >
  Run anyway*.
- Your files are in `Documents/Merge` (Projects, Beats, Recordings, Samples, Mappings). The
  library database and caches are in your user app-data folder (`%APPDATA%\Merge` on Windows).

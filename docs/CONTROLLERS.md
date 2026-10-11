# MIDI controllers

Pads respond to notes **36-51** on any channel (the MPC / Maschine convention) with no setup. Other
controls are mapped with **MIDI learn**: open *Menu > MIDI controller mapping*, click **Learn** next
to an action, then move the control on your hardware.

Mappings are saved as JSON in `Documents/Merge/Mappings/<name>.json`. Share them by copying the file.

```json
{
  "format": "merge-midi-mapping",
  "name": "My controller",
  "bindings": [
    { "type": "note", "channel": 1, "number": 11, "control": "deck1.play", "mode": "toggle" },
    { "type": "cc", "channel": 1, "number": 19, "control": "deck1.volume", "mode": "absolute" },
    { "type": "cc", "channel": 1, "number": 33, "control": "deck1.jog", "mode": "relative" }
  ]
}
```

**Modes:**
- `absolute`: a 0-127 value maps to the control's full range.
- `relative`: two's complement steps (1-63 up, 65-127 down), used for jog wheels and encoders.
- `toggle`: acts on press.
- `momentary`: press and release, used for cue and pads.

## Control IDs

`deckN` is `deck1` to `deck4`.

| Control | IDs |
|---|---|
| Transport | `deckN.play`, `deckN.cue`, `deckN.sync`, `deckN.master`, `deckN.keylock` |
| Tempo | `deckN.pitch`, `deckN.jog` |
| Cues and loops | `deckN.hotcue1`...`deckN.hotcue8`, `deckN.loop`, `deckN.loophalf`, `deckN.loopdouble`, `deckN.jumpback`, `deckN.jumpfwd` |
| Mixer channel | `deckN.volume`, `deckN.trim`, `deckN.eqhigh`, `deckN.eqmid`, `deckN.eqlow`, `deckN.filter`, `deckN.headphone` |
| FX | `deckN.fxon`, `deckN.fxamount` |
| Mixer | `mixer.crossfader`, `mixer.master`, `mixer.cuemix`, `mixer.cuevolume` |
| Beatmaker | `beat.play`, `beat.stop`, `beat.record`, `beat.prevpattern`, `beat.nextpattern`, `beat.level`, `beat.bpm`, `beat.pad1`...`beat.pad16` |
| App | `app.mode` |

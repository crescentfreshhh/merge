# Samples

## Merge Factory Library (CC0)
`tools/samplegen/generate.py` synthesises every factory sound from scratch with the Python
standard library. It runs at build time and its output is deterministic. Because it's all our
own work, the library is released as **CC0 1.0** and can ship with Merge with no restrictions.

| Kit | Tempo / key | Highlights |
|---|---|---|
| Trap | 140 BPM, F minor | punchy kicks, tight snare + clap, rolling hats, distorted 808, dark stab |
| Boom Bap | 90 BPM, D minor | dusty bit-crushed drums, swung groove, jazz chord, vinyl |
| House | 124 BPM, A minor | 909-style kick and clap, offbeat hats, congas, organ stab |
| Drill | 142 BPM, C# minor | sliding 808, syncopated hats, rim snare |
| Lo-Fi | 82 BPM, E minor | soft crunchy drums, Rhodes, pad, vocal chop |
| Afro | 108 BPM, G minor | shakers, congas, log drum (amapiano), agogo, cowbell |

There are also 7 melodic instruments for the Keys sampler: E-Piano (FM), Pluck (Karplus-Strong),
Soft Pad, Sub Bass, 808 Long and two vocal formant chops.

Change the sounds by editing the kit functions in `generate.py` and rebuilding (bump
`GENERATOR_VERSION`).

## Kit format (`kit.json`)

```json
{
  "name": "Trap", "genre": "Trap", "bpm": 140, "swing": 50, "key": "F minor", "order": 0,
  "license": "CC0-1.0",
  "pads": [ { "pad": 1, "name": "Kick", "file": "01 Kick.wav", "choke": 0, "root": 36 } ],
  "pattern": { "length": 16, "steps": { "1": "x......x..x....." } }
}
```

Step strings use one character per 16th note: `X` = accent, `x` = normal, `o` = soft, `.` = off.
Any folder that contains a `kit.json`, whether it is in the factory library, a bundled pack or
`Documents/Merge/Samples`, appears in the Kits browser.

## Your packs
See [`samples/user-packs/README.md`](../samples/user-packs/README.md).

- Each pack needs a `pack.json` with `redistribution_allowed: true`.
- CI runs `tools/validate_samples.py` and refuses to package packs without it. Most commercial
  packs forbid redistributing the samples themselves.
- Audio in that folder is stored with Git LFS.

## CC0 sounds from the web
Freesound and BigSoundBank host CC0 (public-domain) sounds. If you add some:

1. Put them in a pack folder.
2. Record each sound's source URL and licence in `pack.json` (or a `CREDITS.txt` next to it).
3. Run `python tools/validate_samples.py samples/user-packs`.

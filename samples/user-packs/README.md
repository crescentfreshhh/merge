# Your sample packs

Put each pack you want bundled with Merge in its own folder here:

```
samples/user-packs/
  My Drum Pack/
    pack.json          <- required
    Kick 01.wav
    ...
    Kits/My Kit/kit.json   <- optional: makes a one-click kit (same format as the factory kits)
```

`pack.json` (copy `pack.json.template`):

```json
{
  "name": "My Drum Pack",
  "author": "Your name or the pack's creator",
  "license": "CC0-1.0 / your own work / the license text or URL",
  "source": "https://where-it-came-from.example",
  "redistribution_allowed": true
}
```

**Read your pack's license first.** Most purchased packs (Splice, marketplace packs, etc.)
let you use the sounds *in your music* but do **not** allow redistributing the samples
themselves - and bundling them inside an app is redistribution. Only set
`redistribution_allowed` to `true` if you made the sounds, they're CC0/public domain,
or the license explicitly allows redistribution in software.

CI runs `python tools/validate_samples.py samples/user-packs` and refuses to build an
installer if a pack is missing its license confirmation.

Audio files in this folder are stored with **Git LFS** (see `.gitattributes`):

```
git lfs install
git add "samples/user-packs/My Drum Pack"
git commit -m "Add My Drum Pack"
```

Packs you don't want to ship can simply go in `Documents/Merge/Samples` on your own
computer - Merge browses that folder too.

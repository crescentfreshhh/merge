# Third-party notices

Merge is licensed under the GNU General Public License v3.0 or later (see `LICENSE`).
It includes or links the following third-party software and assets:

| Component | License | Used for |
|---|---|---|
| [JUCE](https://juce.com) 9 | AGPLv3 (compatible with GPLv3 under GPLv3 section 13) | App framework, audio I/O, plugin hosting, file formats |
| [Steinberg VST3 SDK](https://github.com/steinbergmedia/vst3sdk) (via JUCE) | MIT | Hosting VST3 plugins |
| [Steinberg ASIO SDK](https://www.steinberg.net/developers/) (optional, Windows) | GPLv3 | Low-latency ASIO audio drivers |
| [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) + Signalsmith Linear | MIT | Keylock / time-stretching on DJ decks and sample matching |
| [SQLite](https://sqlite.org) | Public domain | Music library database |
| [Inter](https://github.com/rsms/inter) typeface | SIL Open Font License 1.1 (`resources/fonts/OFL.txt`) | User interface font |
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) (optional) | MIT | HQ stem separation |
| [Demucs](https://github.com/facebookresearch/demucs) model weights (optional, downloaded on first use) | MIT | HQ stem separation |

The **Merge Factory Library** (drum kits and instruments) is synthesized from scratch by
`tools/samplegen/generate.py` and released under **CC0 1.0** (public domain).

Sample packs placed in `samples/user-packs/` keep their own licenses, recorded in each
pack's `pack.json`.

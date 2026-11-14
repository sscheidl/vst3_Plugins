# Third-party code

The plug-ins built from this repository contain code from the projects below.
Their license texts must accompany any binary that is passed on.

| Project | Used for | License | Where |
| --- | --- | --- | --- |
| Steinberg VST3 SDK 3.8.0 | Plug-in interfaces, base classes, validator, tools | MIT, © 2025 Steinberg Media Technologies GmbH | Git submodule `external/vst3sdk`; text in `external/vst3sdk/LICENSE.txt` |
| VSTGUI 4.15 | Plug-in windows | BSD 3-Clause, © Steinberg Media Technologies | Part of the SDK submodule; text in `external/vst3sdk/vstgui4/LICENSE` |
| DaisySP | One function, `mtof`, in `instruments/toy-piano-classic/core/compat/Utility/dsp.h` | MIT, © 2020 Electrosmith, Corp. | Copied into this repository |
| Daisy-Seed, Toy Piano | The sound engine in `instruments/toy-piano-classic/core/` | MIT, © 2026 TAUREON | Copied into this repository, unchanged |
| Torus, the Daisy port of Mutable Instruments Rings | The synthesis DSP in `instruments/torus-classic/core/torus/`; with its reverb also in the test reference `instruments/torus-classic/tests/reference/`, which no plug-in contains | MIT, © Emilie Gillet and the upstream contributors | Copied through Daisy-Seed and patched, see `instruments/torus-classic/UPSTREAM.md`; text in `instruments/torus-classic/licenses/Torus-Rings-MIT.txt` |
| stmlib | DSP helpers in `instruments/torus-classic/core/stmlib/` | MIT, © Emilie Gillet | Copied through Daisy-Seed, unchanged; text in `instruments/torus-classic/core/stmlib/LICENSE` |
| Daisy-Seed, Physical Modeling | The instrument engine in `instruments/torus-classic/engine/` and one queue header in `source/`; that header also in `instruments/elements/source/` | MIT, © 2026 TAUREON | Copied into this repository and adapted, see `instruments/torus-classic/UPSTREAM.md` |
| Mutable Instruments Elements, from pichenettes/eurorack | The synthesis DSP, its tables and its sampled strikes in `instruments/elements/core/elements/`; unchanged also in the test reference `instruments/elements/tests/reference/`, which no plug-in contains | MIT, © 2014 Emilie Gillet | Copied into this repository and patched for a run-time sample rate, see `instruments/elements/UPSTREAM.md`; text in `instruments/elements/licenses/Elements-MIT.txt` |
| stmlib, from pichenettes/stmlib | DSP helpers in `instruments/elements/core/stmlib/` | MIT, © Emilie Gillet | Copied into this repository, unchanged; text in `instruments/elements/core/stmlib/LICENSE` |

Neither submodule content nor build output is stored in this repository.

VST is a registered trademark of Steinberg Media Technologies GmbH.
Mutable Instruments is a registered trademark; nothing in this repository is
made, endorsed or supported by Mutable Instruments.

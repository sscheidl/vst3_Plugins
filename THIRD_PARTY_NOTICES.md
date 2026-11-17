# Third-party code

The plug-ins built from this repository contain code from the projects below.
Their license texts must accompany any binary that is passed on. All of it is under
MIT-style terms except the phase vocoder of Messier, which is under the LGPL 2.1 or
later (row "Csound" below).

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
| Daisy-Seed, Nebulae on Daisy 0.2.2 | The granular looper engine, control laws and grain processor in `instruments/messier/core/` (`engine.*`, `control.*`, `pcm.h`, `wav.h`, `fft.h`, `profile.h`); verbatim also in the test reference `instruments/messier/tests/reference/`, which no plug-in contains | MIT, © 2026 Daisy-Seed contributors | Copied through `git show` of Daisy-Seed `8226a31` and patched, see `instruments/messier/UPSTREAM.md`; text in `instruments/messier/licenses/Project-MIT.txt` |
| Csound, `Opcodes/pvlock.c` (mincer) and `Engine/fgens.c` (windows) | The phase vocoder and the window generators in `instruments/messier/core/reference_dsp.h` and `reference_dsp.cpp`, through Daisy-Seed and the De-Pattern pedal port | **LGPL-2.1-or-later**, © 2009 V Lazzarini and the Csound authors | Taken through Daisy-Seed, which preserves the notice; texts in `instruments/messier/licenses/Csound-LGPL-2.1.txt` and `Csound-FGENS-NOTICE.txt`. Changes to these two files stay under the LGPL; the Messier bundle names the commit it was built from, so that this part can be replaced and rebuilt (`instruments/messier/README.md`) |
| De-Pattern, Nebulae Granular Looper Pedal | Window, FFT, vocoder, RMS limiter and control-law code selected into `instruments/messier/core/reference_dsp.*` | MIT, © 2026 Kenny Rakentine, including the Qu-Bit notice | Through Daisy-Seed; text in `instruments/messier/licenses/De-Pattern-MIT.txt` |
| Qu-Bit Electronix, Nebulae v2 firmware | The control laws and the design of the Granular Looper instrument that the core adapts | MIT, © 2019 Qu-Bit Electronix, Inc. | Through Daisy-Seed; text, byte for byte, in `instruments/messier/licenses/Qu-Bit-MIT.txt` |

Neither submodule content nor build output is stored in this repository.

VST is a registered trademark of Steinberg Media Technologies GmbH.
Mutable Instruments is a registered trademark; nothing in this repository is
made, endorsed or supported by Mutable Instruments. Nebulae is a product of Qu-Bit
Electronix; nothing in this repository is made, endorsed or supported by Qu-Bit
Electronix.

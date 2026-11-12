# Third-party code

The plug-ins built from this repository contain code from the projects below.
Their license texts must accompany any binary that is passed on.

| Project | Used for | License | Where |
| --- | --- | --- | --- |
| Steinberg VST3 SDK 3.8.0 | Plug-in interfaces, base classes, validator, tools | MIT, © 2025 Steinberg Media Technologies GmbH | Git submodule `external/vst3sdk`; text in `external/vst3sdk/LICENSE.txt` |
| VSTGUI 4.15 | Plug-in windows | BSD 3-Clause, © Steinberg Media Technologies | Part of the SDK submodule; text in `external/vst3sdk/vstgui4/LICENSE` |
| DaisySP | One function, `mtof`, in `instruments/toy-piano-classic/core/compat/Utility/dsp.h` | MIT, © 2020 Electrosmith, Corp. | Copied into this repository |
| Daisy-Seed, Toy Piano | The sound engine in `instruments/toy-piano-classic/core/` | MIT, © 2026 TAUREON | Copied into this repository, unchanged |

Neither submodule content nor build output is stored in this repository.

VST is a registered trademark of Steinberg Media Technologies GmbH.

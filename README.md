# vst3_Plugins

Experiments and feasibility studies for developing native C++ **VST3 plug-ins**.
A recurring theme is porting ARM firmware written for the Electro-Smith
**Daisy Seed3** ([sscheidl/Daisy-Seed](https://github.com/sscheidl/Daisy-Seed))
to desktop plug-ins.

This is a lab, not a product line: every project states what it is meant to
prove and records what was learned.

## Layout

| Path | Contents |
| --- | --- |
| [`instruments/`](instruments/) | Plug-ins that generate sound from MIDI (VST3 category `Instrument`) |
| [`effects/`](effects/) | Plug-ins that only process incoming audio (VST3 category `Fx`) |
| [`common/`](common/) | Code shared by several plug-ins: build helpers, DSP utilities, GUI parts |
| [`docs/`](docs/) | Planning, decisions and study results |

One directory per plug-in, named in lower-case kebab case. A plug-in is either
an instrument or an effect; if a port yields both, they are two plug-ins that
share their DSP code through `common/`.

## Projects

| Project | Kind | Status |
| --- | --- | --- |
| [Toy Piano Classic](instruments/toy-piano-classic/) | Instrument | Specified, not yet implemented |

## Status

The repository structure exists; no plug-in has been built yet. Framework,
platforms and licensing are still open, see
[docs/PLANUNG.md](docs/PLANUNG.md) (German).

## Development machine

Windows 11 x64, CMake 4.4, Visual Studio Build Tools 2022 and 2026.
Build instructions follow with the first plug-in.

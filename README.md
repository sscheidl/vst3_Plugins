# vst3_Plugins

Experiments and feasibility studies for developing native C++ **VST3 plug-ins**
directly on the Steinberg VST3 SDK. A recurring theme is porting ARM firmware
written for the Electro-Smith **Daisy Seed3**
([sscheidl/Daisy-Seed](https://github.com/sscheidl/Daisy-Seed)) to desktop
plug-ins.

This is a lab, not a product line: every project states what it is meant to
prove and records what was learned.

## Projects

| Project | Kind | Status |
| --- | --- | --- |
| [Toy Piano Classic](instruments/toy-piano-classic/) | Instrument | 0.2.0: builds, validated and tested; 0.1.0 loads in Studio One, not yet listened to |

## Layout

| Path | Contents |
| --- | --- |
| [`instruments/`](instruments/) | Plug-ins that generate sound from MIDI (VST3 category `Instrument`) |
| [`effects/`](effects/) | Plug-ins that only process incoming audio (VST3 category `Fx`) |
| [`common/`](common/) | Code shared by several plug-ins. Empty so far |
| [`docs/`](docs/) | Planning and decisions ([PLANUNG.md](docs/PLANUNG.md), German) |
| `external/vst3sdk` | Steinberg VST3 SDK 3.8.0 as a Git submodule |
| `scripts/` | Helper scripts |

One directory per plug-in, named in lower-case kebab case. A plug-in is either
an instrument or an effect; if a port yields both, they are two plug-ins that
share their DSP code through `common/`.

## Build

Windows x64 only so far. Needed: CMake 3.25 or newer and Visual Studio 2026
or 2022 (the Build Tools suffice) with the C++ workload. The commands below
use the 2026 presets; for 2022 replace `vs2026` with `vs2022`.

Fetch the SDK once. Only five of its seven parts are needed:

```bash
git submodule update --init external/vst3sdk
```

```bash
git -C external/vst3sdk submodule update --init --depth 1 base cmake pluginterfaces public.sdk vstgui4
```

Then configure, build and test:

```bash
cmake --preset windows-vs2026-x64
```

```bash
cmake --build --preset windows-vs2026-x64-release
```

```bash
ctest --preset windows-vs2026-x64-release
```

Every plug-in build ends with Steinberg's validator run on the result.
Plug-ins land in `build/windows-vs2026-x64/VST3/Release/`. They are not
installed automatically; each project's README says how.

The build also produces `bin/Release/editorhost.exe`, the SDK's minimal host.
It opens a plug-in's window without a DAW:

```powershell
& "build\windows-vs2026-x64\bin\Release\editorhost.exe" "build\windows-vs2026-x64\VST3\Release\Toy Piano Classic.vst3"
```

[scripts/capture-editor.ps1](scripts/capture-editor.ps1) does the same and
saves a picture of that window.

## License

[MIT](LICENSE). Third-party code is listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

VST is a registered trademark of Steinberg Media Technologies GmbH.

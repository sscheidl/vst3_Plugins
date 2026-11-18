# Build report: Messier 0.2.0, the window of the window study

Date: 2026-11-18. Built and tested by Claude (Opus 5.5) in the worktree
`.work/messier-gui/wt` on the branch `messier-gui`, on top of `8543254` (0.1.11, pushed on
`messier-0.1.0`), **uncommitted**. The sound is unchanged; the bit-exact comparison with the
firmware core passes as before.

0.2.0 replaces the provisional window of 0.1.4 to 0.1.11 with the window of the window
study, version 3 ([mockup](https://claude.ai/artifact/VxHc8Nbapj8ArJvkDQebWG)), and the
decisions the user took on 2026-11-18 (`docs/PLANUNG.md`, Messier, row "Fenster"). The
surfaces are the Slate bitmaps Codex/GPT delivered after [the bitmap brief](GRAFIKAUFTRAG_GUI.md)
(`out/messier-slate-2026-11-18/`, only `assets/` taken).

| What | Where | Test |
| --- | --- | --- |
| Window layout after the study: header, file line, plate, waveform, START slider, knobs, keyboard | `source/gui_layout.h` (new), `source/editor.*` | `TestViewLayout` (sizes 880 x 640 / 724) |
| GUI presets: colours, bitmap names, 1x/2x choice, nine parts, engraved text | `source/gui_skin.*` (new) | `TestGuiPresets` (new): the 26 files, their pixel sizes, names of the other presets |
| Background: chassis, screws, rules, brand, knob names | `source/panel_view.*` (new) | – (drawing) |
| The measuring plate: seconds axis, glass, nebula, grid, loop, spray, shooting stars, bezel, waveform, playhead, snowflake, START slider and row | `source/map_view.*` | `TestMapEdits` (as before) |
| Knobs, buttons, keys drawn with the skin | `source/widgets.*` (`MetalKnob` new) | `TestKeyboardLatch`, `TestResizeGrip` (as before) |
| Preset and size menus in the header | `source/editor.*` (`ShowPresetMenu`, `ShowSizeMenu`, `SetGuiPreset`, `SetZoom`) | – (native menus need a window) |
| The preset saved with the window: controller state version 2, version 1 still read | `source/parameters.h`, `source/controller.cpp` | `TestGuiPresets` |
| The waveform overview carries the file's rate (seconds axis) | `source/wave_overview.*`, `source/display_data.cpp` | `TestDisplayPath`, loader test |
| Bitmaps into the bundle (`Contents/Resources`), version 0.2.0 | `CMakeLists.txt`, `resource/gui/` (26 PNG), `components.json`, `source/version.h` | `messier.provenance` |

## Decisions

The user decided on 2026-11-18, asked where study and brief disagreed or said nothing:

* **Keyboard:** 15 white keys as before (C4 to C6 and the octave keys), at the key
  bitmap's own width of 36 units, so 540 units centred. The study drew three octaves.
* **STOP** in the header left of KEYS; **LATCH** in the keyboard section beside the
  octave keys (both came after the study).
* **No background stars** on the plate (the study had about 110 faint fixed dots; the
  later brief forbids dots, as they could be taken for grains).
* **Version 0.2.0** for the window; the input features planned for 0.2.0 (live, overdub,
  circular, dry blend, input gain, grain FM) move to 0.3.0.

Taken by me where nothing was said (also in `docs/PLANUNG.md`):

* **Knob columns** as in the study: column 0 holds only SPRAY (START is the slider), then
  Speed, Loop Size, Density, Pitch, Overlap, Window, Blend, Volume. The study puts Window
  before Blend, the firmware's order is the other way round; I followed the study, which
  the user approved. MASK stays under Window, the places of the input secondaries read
  "INPUT GAIN (0.3.0)" under Speed and "GRAIN FM (0.3.0)" under Blend (the study wrote
  "input"; Blend's secondary in the core is grain FM).
* **WINDOW** keeps its curve of 0.1.8 instead of a name.
* **Fonts:** Palatino Linotype (engraved capitals, italic file name) and Consolas
  (values), Windows system fonts. The study's web fonts are not in the bundle; adding them
  would bring font files and their licences.
* **Size menu:** 50, 75, 100, 125, 150 and 200 % (the limits of the grip and the window
  edge). The study offered 75 to 150 %.
* **Header buttons** 8 units further left than in the study: at the study's place the
  preset button covered the corner screw (seen in the first look at the real window).
* **Screws:** three corners; the lower right holds the resize grip of 0.1.11.
* **Catalogue number** "No. n" on the file line, as in the study: the loader's sequence
  number of the file the core reads (counts project and window loads of this instance).

## Bitmaps and resources

The 26 PNG files (13 images at 1x and 2x, 4,496,768 bytes) are copied unchanged from
`out/messier-slate-2026-11-18/assets/` into `resource/gui/`; each file's SHA-256 was
checked against the delivery's `SHA256SUMS.txt`. CMake adds them with
`smtg_target_add_plugin_resources` to `Messier.vst3/Contents/Resources/`, where the SDK's
bundle support (`setupVSTGUIBundleSupport`) points VSTGUI's resource path. Nothing else of
the delivery (sources, references, reports, tools) is in the plug-in.

* **Loading:** `Skin` asks the platform factory for `messier_<preset>_<image>@1x.png` and
  `@2x.png` (shared images without the preset). A file is taken only if its pixel size is
  the brief's (2x: double); otherwise, or if it is missing, that image is drawn as a flat
  stand-in in the preset's colours. The window therefore works without bitmaps, as in the
  tests.
* **1x or 2x:** each draw takes the 2x bitmap when the device pixels per window unit
  (display scale times zoom) exceed 1.01, else the 1x one. VSTGUI's own choice would take
  1x up to 150 % and enlarge it; this takes the finer one from 101 %.
* **Stretching:** a transform maps the bitmap's part onto the target rectangle, with high
  interpolation quality. Bezel (edges 12) and buttons (edges 8) are nine parts with
  stretched edges and centre; the part boundaries are put on whole device pixels, so
  neighbouring parts meet without a seam.
* **Nebulae:** the two white bitmaps are tinted per preset when the skin is made
  (`CBitmapPixelAccess` with straight alpha: colour set, alpha kept), Slate `#68529C` and
  `#384480`. They are drawn 40 units beyond the plate on each side, drift on slow sines
  (periods of 5 to 6 minutes) and take turns over about 2.3 minutes; their strength is 0.18 + 0.62 x (1 - BLEND).
* **Chassis:** without the keyboard the upper 640 units are drawn, with it all 724.

## The plate

Everything on it is drawn from the display the controller receives (since 0.1.4) and the
parameters; nothing is simulated.

* **Seconds axis** above the bezel: frames / rate of the file. The step is the smallest of
  1 ms ... 1 h that keeps labels 64 units apart (`layout::AxisStep`); minor ticks at a
  fifth (or half) of it. Without a file no axis.
* **Grid:** faint lines at the labels, crosses at half steps and at each quarter of the
  panorama; "L" and "R".
* **Loop** faint over the plate with dashed edges, **spray** around the playhead (wrapped
  as before), **waveform** below in the same scale, light inside the loop, with the loop's
  accent brackets.
* **Playhead** from the plate through the waveform, with an accent marker on the axis;
  while the display says frozen, dashed, with a snowflake in a dark box 46 units into the
  plate.
* **Grains as shooting stars:** head radius and brightness follow sin(pi x phase) and burn
  out after phase 0.6; a glow of three rings. The **tail** is the grain's real motion: the
  display carries no grain numbers, so the view matches each grain to one of the display
  before with the same panorama (a grain keeps it), a smaller window phase and the
  position nearest to where it would now be, and measures its speed in file frames per
  second (smoothed over two displays, wrap at the file's end handled). The tail is the
  path of the last 0.12 s, at most 80 units, growing in over the first third of the
  window; a grain seen once has none. Brightness 0.35 + 0.65 x BLEND.
* **START slider** under the waveform: the thumb bitmap at START, an accent line from the
  left, 40 ticks. A click or drag anywhere on the view sets START (as the map did since
  0.1.8, now with the time axis inset by the bezel's 12 units); the wheel steps SPRAY.
* **START row:** "START" and its value (the loop's start in seconds from the display, and
  the parameter in percent), and a legend.

The view is drawn on new displays (about 33 times a second while playing) and otherwise
every 120 ms, so the nebula keeps drifting.

## Presets and state

`GuiPreset` (0 Slate, 1 Silver, 2 Copper, 3 Photo plate) is part of the window settings
and of the controller state, version 2 (version, zoom, keyboard, transposition, preset;
21 bytes). Version 1 (0.1.x) is read and opens with Slate; an unknown preset number,
version 2 without the preset, or another version is refused, as before for damage. The
colours of all four presets are in `gui_skin.cpp` (study and brief); only Slate is marked
delivered. The preset menu lists all four, the other three disabled "(bitmaps to come)";
a song naming one of them keeps the setting and is shown as Slate. To add a preset: put
its 20 bitmaps (10 images x 2) into `resource/gui/`, add them to `MESSIER_GUI_IMAGES`, set
`delivered`.

## Data path

One change: `WaveOverview` carries the file's sample rate (`rate`, plus a reserved word
so that no padding travels), 8216 instead of 8208 bytes. The loader fills it;
`ReadOverviewBytes` accepts 0 (unknown) or 1 to 768 kHz and refuses anything else.
`EngineDisplay` (288 bytes) and the core are unchanged.

## Exception boundaries

As since 0.1.10 (MS-R37): every new `draw` goes through `Guarded`, the menus' callbacks
too (they run inside the button's guarded mouse handler and check that the editor is
still alive). Making a skin may throw (allocation); a preset change makes the new skin
first and keeps the old one if that fails. Fonts are kept for the skin's life, so a font
handed to a draw call stays valid.

## Build matrix

Run on 2026-11-18 on this state; logs in `out/messier-0.2.0/logs/`.

| Build | ctest | Validator (all four plug-ins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 16/16 | 47/0 |
| VS 2026 Debug | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | 16/16 | 47/0 |
| ASan (the five Messier test programs) | all exit 0, no report | – |

Bit-exact with the separately built firmware core as before (plug-in: 0 of 960,000 samples
differ).

## Looked at

Once, in the SDK's `editorhost` on this machine (display at 120 %, so the 2x bitmaps):
the window opens, all 13 images load, alpha edges, bezel and buttons show no seams or
fringes at that size, texts fit. That look found the screw under the preset button (moved).
No file was loaded there, so the plate's drawing with a sample, the grains and the menus
were not seen.

## Not checked

* The window with a sample: axis, grains and their tails, nebula drift, snowflake, START
  slider, in a host (DAW test list, section 6).
* The 1x bitmaps (a display at 100 % and zoom up to 100 %).
* The preset and size menus (native menus need a window).
* CPU with the window open compared with 0.1.11.
* Silver, Copper and Photo plate: their colours are entered but never seen with bitmaps;
  Photo plate's light glass will need its own look at the playhead colour on the metal.

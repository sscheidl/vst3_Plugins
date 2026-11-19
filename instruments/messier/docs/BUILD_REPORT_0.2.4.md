# Messier 0.2.4 build report

Base: 0.2.3, commit `086e178` (code `a77041c`), released by Codex for the controlled DAW
test ([CODEX_REVIEW_0.2.3.md](CODEX_REVIEW_0.2.3.md)). Brief: the hand-over of
2026-11-19 (`CLAUDE_AUFTRAG_MESSIER.md`, the GUI standard template and the bitmap
package). Window only: the engine, the processor, the parameters and the processor's
state are unchanged; the firmware comparison stays bit-exact.

## MS-R53 and MS-R54

| Finding | Fix in 0.2.4 | Test |
| --- | --- | --- |
| MS-R53 (P3): with close window phases and a late reply, a running grain hid an ended one's afterglow. | `MapView::ShowAt` moves each grain of the display before on by the audio's own time, the quanta rendered between the two displays (64 samples at 48 kHz each). First every grain still inside its window takes the grain of the new display nearest to where it should be (same pan, speed and window rate, within `0.01 + rate * 0.003`); only then the ended ones look for one left over. Each grain of the new display continues at most one. The end time comes from the same audio time. | Codex' case: phases 0.90 and 0.85, 10 quanta after 30 ms; the first glows. The 0.2.2/0.2.3 cases now carry 23 quanta for 30 ms. |
| MS-R54 (P3): without further displays, a trail predicted from a grain's end stopped ageing at the 150 ms limit of the prediction. | `MapView::EndedFor` measures the end on the wall clock; the place of a grain still stops at 150 ms. The drawing uses it, so the trail fades and is gone `kTrail` after the predicted end. | About 100 ms left, no further display: not ended at 50 ms, ended 0.25 s at 350 ms, gone at 450 ms. |

## The GUI standard

One layout for Slate, Silver, Copper and Photo plate (`gui_layout.h`); the presets change
material and colours only.

* **Header** (0 to 52): the brand in sans serif, an accent stroke, LOAD..., the file's
  name, format and catalogue number; on the right KEYS, STOP and the size and preset
  menus as one group, 6 units apart. The subtitle is gone; maker and version stand at
  the right end of the DETAILS bar.
* **Plate view** (54 to 396): unchanged inside. START's thumb and the loop brackets now
  come from one mapping (`MapView::StartX`, `FrameX`); the rail spans the plate's x axis
  and the value stands under its left end.
* **Primary knobs**: three groups (SPRAY, SPEED, LOOP SIZE | DENSITY, PITCH, OVERLAP,
  WINDOW | BLEND, VOLUME), columns of 82 units, rules between the groups, FREEZE and
  RESET beside VOLUME. All keys outside the header are 74 units wide.
* **DETAILS**: a flat switch with a chevron in a ruled bar. Open, the window grows by 96
  units for the five secondary knobs (PAN RND, DISPLACE, PITCH RND, SIZE RND, MASK);
  closed, those knobs, their values and names are hidden. Values and automation stay.
  Closed for a new instance.
* **Keyboard row** (84, below everything): LATCH, "- OCT", the 15 white keys, "OCT +",
  centred. LATCH goes with the keyboard; KEYS stays reachable in the header.
* Window: 880 x 560, +84 with the keyboard, +96 with DETAILS (740 with both). The
  chassis bitmap (724 high) is stretched only for the 740 case; it is a plain material.

### Bitmaps and colours

* The 72 delivered PNGs (eight per preset, four shared, 1x and 2x) checked against the
  package's SHA-256 list (72 of 72) and against the sizes the window expects (all match;
  nothing missing). They replace the interim Slate bitmaps of 0.2.2. CMake lists all of
  them.
* `kPresets` takes the package's `GUI_PRESETS.inc` (designated initialisers, the order of
  the stored preset IDs). All four presets are delivered; Slate stays the default.

### State

The controller's state is version 3: version 2 plus DETAILS as one byte at the end (22
bytes). Versions 1 and 2 are read as before, with DETAILS closed. Preset IDs are
unchanged.

## Files

* `source/gui_layout.h` (new layout), `source/editor.cpp`, `source/editor.h` (DETAILS,
  sizes with DETAILS, KEYS in the header, LATCH in the keyboard row),
  `source/panel_view.*` (rules of the groups and of DETAILS, brand, version),
  `source/widgets.*` (the flat DETAILS switch), `source/gui_skin.cpp` (colour tables),
  `source/controller.cpp`, `source/parameters.h` (state version 3),
  `source/map_view.*` (MS-R53, MS-R54, START mapping).
* `resource/gui/`: 72 PNGs; `CMakeLists.txt`: the image list.
* `tests/test_plugin.cpp`: sizes with DETAILS, the layout, all four presets' bitmaps,
  state version 3, the menu, MS-R53, MS-R54 and START at 0, 0.5 and 0.98.
* `tests/preview_window.cpp`: seven cases, all four presets, DETAILS open and closed.
* Version 0.2.4 in `CMakeLists.txt`, `components.json` and `source/version.h`; CHANGELOG,
  README, `DAW_TEST_0.1.0.md` (section 0.2.4). The 0.2.3 report's "24 grains" corrected
  to 20.

## Tests

Run on 2026-11-19 on exactly this state; logs in `out/messier-0.2.4/logs/`.

| Build | ctest (Messier tests) | Validator (all four plug-ins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 6/6 | 47/0 each |
| VS 2026 Debug | 6/6 | |
| VS 2022 (MSVC 19.44) Release | 6/6 | |

ASan was not run; the changes are layout, drawing and checks on plain values.

Previews: `preview_messier_window` gives 21 pictures in `out/messier-0.2.4/preview/`
(seven cases at 1x, 2x and 0.5x): Slate with the keyboard at the lowest octave, with
DETAILS open at the highest octave and STOP, without keyboard and frozen, with 20
grains; Silver, Copper and Photo plate with DETAILS open. I looked at them: the
controls stand in the same places in all four presets, and on Photo plate the waveform,
the playhead and the grains read well on the light glass.

## Open

* The user's DAW test, section 0.2.4 of `DAW_TEST_0.1.0.md`.
* Codex' check of 0.2.4.

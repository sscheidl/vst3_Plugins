# Messier 1.0.0-beta.2 build report (interim)

Base: 1.0.0-beta.1, commit `a6823d6` (tag `messier-v1.0.0-beta.1`), branch
`claude/messier-beta2-117e35`. The order: a new, more innovative window after the user's
render and decisions of 2026-11-20, and the findings of the user's Studio One tests of beta
1. This is an interim state: the user's DAW findings, the bitmaps of
[GRAPHICS_BRIEF_BETA2.md](GRAPHICS_BRIEF_BETA2.md) and a possible touch-pad keyboard are
still to come. The sound, the parameters, MIDI, automation and the processor's state are
unchanged; the engine is 0.1.8.

## What changed

Details in the [CHANGELOG](../CHANGELOG.md).

| Part | Change | Where |
| --- | --- | --- |
| Layout | The round window: glass centre (440, 272), radius 180, ring 16; nine knobs on two arcs and below the glass; START row; info line. 880 x 624 (+96 DETAILS, +84 keys). | `source/gui_layout.h`, `source/editor.*`, `source/panel_view.*` |
| Plate | Beta 1's plate on the round glass through a fish-eye lens (elliptical square-to-disc mapping and a radial lens, both with closed inverses); data, prediction, afterglow and mouse unchanged. GALAXY (a turning nebula) or WAVE (waveform and grid) behind the marks. Only the glass and ring take the mouse. | `source/fisheye.h`, `source/map_view.*` |
| START | Its own bar with the value box; the thumb stands where the loop begins (MS-R56 kept). | `source/widgets.*` (`StartBar`) |
| View switch | GALAXY / WAVE, a window setting; controller state version 8 (one byte). | `source/widgets.*` (`ViewSwitch`), `source/controller.cpp`, `source/editor.*` |
| Presets | One dark design; the GUI preset IDs 0 to 3 now name nebula variants (spiral, barred spiral, ring nebula, diffuse nebula), each with its own tints and HUE. | `source/gui_skin.*` |
| HUE | Red over violet to blue (0 down to 230 degrees), a neutral slider; colours the nebula and the accents, not the labels. | `source/gui_skin.*`, `source/editor.cpp` |
| Bitmaps | The names and sizes of the graphics brief first, beta 1's of Slate where they fit; ring, glass and nebulae drawn by code until delivered. The bitmaps of Silver, Plum and Gold, the bezel, the plate and the nebula bands removed: 56 files, 16 left. | `source/gui_skin.*`, `CMakeLists.txt`, `resource/gui/` |
| Tools | `tools/check_gui_bitmaps.py`: checks a delivery against the brief (sizes, alpha radii, white nebulae, no small bright points, neutral colour, contrast, size, hashes). | `tools/` |

## Tests

New or changed in `tests/test_plugin.cpp`: `TestLens` (the lens fills the glass, edges on
the rim, 1.5x in the middle, every place found back, monotonic), `TestHueScale` (the
slider's arc, old hues at the nearer end, labels untouched), `TestRoundLayout` (knobs,
names, values and scales clear of the ring and of each other), the state test (version 8),
the bitmap tests (new and old names), `TestMapEdits` and the START tests on the round
glass and the START bar. Three bitmaps of the former presets are kept in `tests/skin/` as
other images for the custom-skin test.

Mutation check: each change undone or broken once, the test failed, then restored.

| Mutation | Test that failed |
| --- | --- |
| The lens' strength 0.5 to 0.4 | `TestMapEdits` (a quarter of the file no longer at 0.6 of the radius) |
| The lens' inverse left out | `TestMapEdits` |
| Old hues: the halfway point moved | `TestHueScale` |
| HUE turns the labels again | `TestHueScale` |
| A knob moved into the ring | `TestRoundLayout` |
| State version 8: GALAXY / WAVE not read | `TestGuiPresets` (the state checks) |
| No fallback to beta 1's bitmaps | `TestCustomSkin` (`complete`) |
| A click on the glass mapped linearly | `TestMapEdits` |

| Build | ctest (Messier tests) | Validator (all four plug-ins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 6/6 | 47/0 each |
| VS 2026 Debug | 6/6 | |
| VS 2022 (MSVC 19.44) Release | 6/6 | 47/0 each |
| VS 2026 Release, AddressSanitizer | 6/6, no reports | |

Firmware comparison (`messier.engine`, Release): five runs of 12 s each, 0 of 1,152,000
samples differ in each; `messier.plugin`: 0 of 960,000 samples differ, no allocation in
`process`.

## Side-by-side test build

On the user's wish, the CMake option `MESSIER_SIDE_BY_SIDE` (off by default) builds
`Messier2.vst3`: the same code with other class IDs (processor `501A4A23-7861474B-9BC6C4A2-
2F0101EF`, controller `673B81B8-768042D7-851351E4-FE59C349`) and the name "Messier2", so that
beta 1 and beta 2 can be tried side by side in one host. Validator 47/0; no tests of its
own (the user's decision). Staged in `out/messier-1.0.0-beta.2-side-by-side/`. Projects
saved with it open only in it; never in a release.

## Not covered by an automated test

The look (the user's visual test and the previews), the nebula's turn and its rest at STOP,
Freeze and 0x, HUE on the drawn nebula, the drop frame, the CPU load with the window open:
DAW test list, section 1.0.0-beta.2.

## Open

* The user's DAW findings of beta 1.
* The bitmaps of the graphics brief (checked with `tools/check_gui_bitmaps.py`, then built
  in); the touch-pad keyboard, if the user picks it.
* Codex' cross-review of the beta 2 code.
* G1.1 (a lost non-empty status text), P3, still open from the GPT review of 0.2.9.

## Test rounds 1 to 5 (2026-11-21)

These are the user's DAW tests of the side-by-side build "Messier2". The CHANGELOG lists
the changes; the session notes keep the details.

State of this commit (round 5, staged as Messier2, binary 14:10):

* All six `messier.*` ctests pass (VS 2026, Release).
* The validator passes for Messier and Messier2: 47 tests, 0 failed.
* The new tests of each round were mutation-checked, and every mutant was caught except
  one equivalent mutant:
  - the range slider;
  - window settings state version 9;
  - the pads and the transposition range;
  - the stars' light;
  - the RMS of the overview;
  - the layout of the lower area;
  - the S-shaped ramp of the turn;
  - the GALAXY / WAVE arcs;
  - HUE on the value texts.
* **Not rerun since the first matrix:**
  - VS 2026 Debug;
  - VS 2022 Release;
  - AddressSanitizer;
  - the firmware comparison.
  The sound path has not changed. The overview gained an RMS array, and the processor
  sends the overview to the controller, so it travels with that message. The full matrix
  is due before a release.
* Not built: the WAVE redesign after GPT's "Orbital Scope" concept. It is only assessed,
  in the session notes.

## The GUI review fixes and the full matrix (2026-11-21)

These are the fixes from the GUI review of 768cea1. The CHANGELOG lists them; the session
notes hold the verdicts and how each was done.

Full matrix on the fixed state:

| Step | Result |
|---|---|
| VS 2026 Release, `ctest -R '^messier\.'` | 6/6 |
| VS 2026 Debug, ctest | 6/6 |
| VS 2022 Release, build of all four plug-ins and ctest | 6/6 |
| AddressSanitizer (VS 2026 Release), ctest | 6/6 |
| Validator, VS 2022 Release: Messier, Kekule, Mendelejew, Toy Piano Classic | 47/0 each |
| Validator, Messier2 (side-by-side, VS 2026) | 47/0 |
| Firmware comparison (`messier.engine`) | 0 differing samples in every run of 1,152,000 |
| Mutation check of the new tests | 6 mutants, all caught |

Measured: `TurnPixels` takes 3.5 ms at 720 × 720 (24 ms before the fix). The review measured
the other gains on Direct2D's software target; on-screen timings in a host are still to be
taken (Studio One, GALAXY at 4x).

## The WAVE redesign, steps 0 to 3, and round 6 (2026-11-21)

The WAVE redesign (steps 0 to 3 of the feasibility assessment) and the fixes of the user's
round 6. The CHANGELOG lists them. The user tested both builds in Studio One and approved
them (round 6: 18:15 build). The sound engine is unchanged.

| Step | Result |
|---|---|
| VS 2026 Release, `ctest -R '^messier\.'` | 6/6 |
| Validator, Messier2 (side-by-side, VS 2026) | 47/0 |
| Mutation check of the new tests (wave columns, blur, preset colours) | 5 mutants, all caught |

Not rerun for these GUI-only changes: VS 2026 Debug, VS 2022, ASan. The look is checked in
previews and by the user in the host; automated tests cover the column list, the blur and
the preset's HUE reset.

## Rounds 7 and 8 (2026-11-21)

The waveform turning round the glass with the playback (round 7), and the inverted spray
display, the display gain for quiet files and the flat GALAXY / WAVE keys (round 8). The
CHANGELOG lists them. The user tested round 7 in Studio One (smooth, STOP and Freeze fine,
the seam not noticeable); round 8 is built and staged, not yet tested by the user.

| Step | Result |
|---|---|
| VS 2026 Release, `ctest -R '^messier\.'` | 6/6 |
| Validator, Messier2 (side-by-side, VS 2026) | 47/0 |
| Mutation check of the new tests (turn, remap, click, GALAXY's fixed axis, shades, gain) | 10 mutants, all caught |

Measured: laying the waveform onto the glass takes at most 1.4 ms a frame at 100 % (a
glass of 360 pixels, a strip without clear pixels; 6.6 ms at 720 pixels, which the window
does not use: the soft layers are made at one pixel per window unit). The upload of the
bitmap is not in that number. Not rerun for these GUI-only changes: VS 2026 Debug, VS 2022,
ASan.

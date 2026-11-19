# Messier 0.2.5 build report

Base: 0.2.4, commit `b7d7414`, released by Codex for the DAW test
([CODEX_REVIEW_0.2.4.md](CODEX_REVIEW_0.2.4.md)). Brief: the hand-over "Plum, Gold, HUE
und TRAIL" of 2026-11-19 (`out/messier-claude-uebergabe-plum-gold/`). Window and display
only: the core, the processor's state, the parameters and the sound are unchanged; the
firmware comparison stays bit-exact.

## HUE and TRAIL

They were in the worktree, uncommitted. `git apply --reverse --check` with the package's
`MESSIER_HUE_TRAIL.patch` confirmed that the patch is contained; nothing of it was
overwritten. In short: a spectrum slider HUE (left in DETAILS; double-click: the preset's
colour) for labels, knob arcs, START fill and the held key, per preset; a knob TRAIL
(right in DETAILS; 150 to 900 ms, 450 by default) for the grains' fade time; the
controller's state version 4 (the four hues) and 5 (TRAIL in milliseconds), versions 1 to
4 still read. The ended-grain store holds 96 entries for the longest trail.

## Plum and Gold

* Stored IDs 2 and 3 keep their places: `GuiPreset::kPlum` (was `kCopper`) and
  `GuiPreset::kGold` (was `kPhotoPlate`), file keys `plum` and `gold`. Projects that chose
  Copper or Photo plate open in Plum or Gold; the state format does not change for that.
* The package's 72 PNGs checked against its SHA-256 list (72 of 72). The 32 new files
  for Plum and Gold are in `resource/gui`; the 40 for Slate, Silver and the shared images
  are byte-identical to 0.2.4. The Copper and Photo plate bitmaps are removed from the
  folder and from CMake (prefixes `slate silver plum gold`).
* `kPresets` takes the package's `GUI_PRESETS.inc`. Plum uses the negative path (dark
  marks on light glass), Gold the normal filled waveform.
* Gold: the plate bitmap's scanlines measure only about one grey level, so the plate also
  draws faint lines across at the quarters of the panorama (`grid_line`), for Gold only.

## MS-R55 to MS-R57

| Finding | Fix | Test |
| --- | --- | --- |
| MS-R55 (P3): a newly started grain with equal values could hide an ended grain's afterglow. | The engine adapter numbers every grain start (`LooperEngine::Impl::TrackGrains` after each quantum, reading only; a slot that starts to sound or whose window begins again gets the next number). `GrainDot::id` carries it; the display is 536 bytes (engine 0.1.6). `MapView::ShowAt` matches grains by that number; displays without numbers keep the phase matching of 0.2.4. | Codex' case with the real engine: a mono file of 96,000 frames, DENSITY 0.85, OVERLAP 0.55, DISPLACE 0.08, displays after 111 and 129 quanta. Three grains end between them, and all three glow (0.2.4 kept two). |
| MS-R56 (P3): at START 100 % on a very short file the thumb stood at the end, the bracket on the last frame. | `MapView::StartX` takes the core's clamp to `frames - 1` while a file is loaded and draws through `FrameX`. | A file of five frames, START 100 %: thumb and bracket at frame 4. |
| MS-R57 (P3): an identical display set the rest, and a grain predicted to have ended came back as a head. | The rest remembers when it began (`rest_at_`); `EndedFor` keeps every end predicted before it and lets it age on the wall clock. While resting no further grain ends. | 60 ms left, identical display after 125 ms: ended 0.07 s at 130 ms, gone after the trail; a grain with 0.4 s left does not end while resting. |

## Files

* `engine/looper_engine.h`, `engine/looper_engine.cpp`: grain numbers (engine 0.1.6).
* `source/display_data.cpp`: 536 bytes.
* `source/map_view.cpp`, `source/map_view.h`: MS-R55 to MS-R57, Gold's lines across.
* `source/gui_skin.cpp`, `source/gui_skin.h`: palettes, the names Plum and Gold.
* `resource/gui/`: 32 new PNGs, 32 removed; `CMakeLists.txt`: the prefixes.
* `tests/test_plugin.cpp`: MS-R55 with the engine, MS-R56, MS-R57; names Plum and Gold.
* `tests/preview_window.cpp`: eleven cases, each preset open and closed.
* The HUE and TRAIL files from the worktree (editor, layout, skin, controller,
  parameters, map view, tests, README) as they were.
* Version 0.2.5 in `CMakeLists.txt`, `components.json` and `source/version.h`; CHANGELOG,
  README, `DAW_TEST_0.1.0.md` (section 0.2.5).

## Tests

Run on 2026-11-19 on exactly this state; logs in `out/messier-0.2.5/logs/`.

| Build | ctest (Messier tests) | Validator (all four plug-ins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 6/6 | 47/0 each |
| VS 2026 Debug | 6/6 | |
| VS 2022 (MSVC 19.44) Release | 6/6 | |

The firmware comparison inside `messier.plugin`: 0 of 960,000 samples differ. ASan was
not run.

HUE's double-click and the live look of HUE and TRAIL are not tested automatically (the
window's mouse events need a host); the TRAIL limits are tested in the map view and in
the saved state.

Previews: `preview_messier_window` gives 33 pictures in `out/messier-0.2.5/preview/`
(eleven cases at 0.5x, 1x and 2x): each preset with DETAILS and KEYS open and with both
closed, and for Slate also both octave limits, STOP, Freeze and 20 grains. I looked at
them. The controls stand in the same places in all four presets. Plum shows the light
glass with the grey waveform and dark grains. Gold shows the dark green glass with a
stronger waveform inside the loop than outside, no glowing edges, and readable orange
markers and warm grains. Its scanlines stay faint at 0.5x.

## Open

* The user's DAW test, section 0.2.5 of `DAW_TEST_0.1.0.md`.
* Codex' check of 0.2.5.

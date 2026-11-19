# Messier 0.2.3 build report

Base: 0.2.2, commit `34a5a70` (review brief fix `085d8ce`, both pushed). Window only: the
engine, the processor and the sound are unchanged.

## What changed

### Codex' probe checks of 0.2.2

Codex' review of 0.2.2 (`CODEX_REVIEW_0.2.2.md`, KEINE FREIGABE) came in late. Before
that, its probe program (`checks/review_checks.cpp`, `Review022`) had already shown the
five points below; in the review they are MS-R49 to MS-R52. Its bitmap runs gave the same
hashes as `resource/gui`. The review's only other finding is MS-R48 (P2), see below.

| Point | Defect in 0.2.2 | Fix in 0.2.3 |
| --- | --- | --- |
| 1 | `ShowAt` took an ended grain for still sounding if a running grain had the same pan, speed and window rate; its afterglow was dropped. | The match also needs the running grain to be further on in its window than before, by about the time since (`kSameGrainSlack`, 0.25 s, for the display's age). |
| 2 | A waveform overview arriving with an unchanged display restarted the playhead's prediction and the drift: the head jumped. | An equal display with a new overview only takes the overview; the prediction and the resting go on. |
| 3 | Without new displays, the drift kept moving at full speed. | `AdvanceDrift` heads for 0 when no display came for `kDriftStaleAfter` (0.5 s). |
| 4 | The nebula's tile period swelled with the cloud, so after a long drift a tile jumped (about 95 units at an offset of 10000). | The tile period no longer swells. Each nebula's place is kept as a share of its period (`drift_a_`, `drift_b_`, wrapped to [0, 1)), so neither the swell nor the old 1e6 wrap can make a jump. |
| 5 | `ReadDisplayBytes` accepted a grain window rate near 0; the grain's end lay beyond the clock's range (no crash proven). | The reader refuses a window rate between 0 and 1e-3 (the core's longest grain is 8 s, a rate of 0.125). `ShowAt` compares in seconds before it makes a time point. |

### MS-R48 (P2): the first display request while the window opens

`Editor::OpenUnchecked` asked the processor directly, before the SDK had finished opening
the window; a reply may make the host close it. The first request now goes the delayed
way as well (`AnimateLater`), which checks the editor and the window serial first.

### Brief point 13: diffraction rays and PITCH tint

* Bright grain heads (window level above 0.35) get four fine rays across and down. Each
  ray is 1 unit wide and `r + 7 + 14 * level` long, and fades outwards and with the head.
  With more than six grains sounding the rays are fainter (6 / count), so the plate stays
  calm.
* `MapView::PitchTint` tints the halo, rays and tails with the main PITCH knob in octaves
  (`PitchOctaves`, the knob's own curve, so neutral is the 0 the knob shows). At 0 the colours
  stay warm. Towards -3 they go to a muted rose (222, 138, 150), towards +2 to a light
  blue-violet (168, 180, 255), at most 55 % of the way (`0.55 * share^0.8`). The head's core stays
  ivory. The editor passes PITCH on open and on every change, host automation included.

## Files

* `source/map_view.cpp`, `source/map_view.h`: points 1 to 4, the rays and the tint.
* `source/display_data.cpp`: point 5.
* `source/editor.cpp`: PITCH to the map.
* `tests/test_plugin.cpp`: the drift test keeps displays coming; new checks for points 1,
  2, 3 and 5, and for the tint at 0.
* `tests/preview_window.cpp`: PITCH per case (-3, 0, +2) and a case with 20 grains (the most a display carries).
* Version 0.2.3 in `CMakeLists.txt`, `components.json` and `source/version.h`. CHANGELOG,
  README and `DAW_TEST_0.1.0.md` (section 0.2.3) are updated.

## Tests

Run on 2026-11-19 on exactly this state; logs in `out/messier-0.2.3/logs/`.

| Build | ctest (Messier tests) | Validator (all four plug-ins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 6/6 | 47/0 each |
| VS 2026 Debug | 6/6 | |
| VS 2022 (MSVC 19.44) Release | 6/6 | |

ASan was not run this time; the changes are drawing code and checks on plain values.
The firmware comparison is part of `messier.plugin` and stays bit-exact.

Previews: `preview_messier_window` gives 12 pictures in `out/messier-0.2.3/preview/`,
including PITCH -3, 0 and +2 and a case with 20 grains (the most a display carries). I looked at them: the rays and
the tint show (rose at -3, warm at 0, blue-violet at +2) and the dense case stays calm.

## Open

* The user's look at the rays and the tint in the host, at low and high density, and at
  PITCH -3, 0 and +2, also automated across 0.
* Codex' check of 0.2.3.

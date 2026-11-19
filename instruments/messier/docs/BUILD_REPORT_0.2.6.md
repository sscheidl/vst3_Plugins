# Messier 0.2.6 build report

Base: 0.2.5, commit `6a2ce0c`. The user's look at 0.2.5 (2026-11-19): HUE should turn
only the hue, not the brightness; TRAIL should be a slider like HUE, but high values
drew ugly streaks, so its place goes to another function, the waveform's brightness.
Window only: the core, the engine, the processor and the sound are unchanged; the
firmware comparison stays bit-exact.

## Changes

* **HUE** (`Skin::SetAccent`): the slider's hue replaces only the hue of the preset's
  `control`, `value`, `text` and `button_text` colours, in HSL. Each colour keeps its
  own saturation and lightness, so brightness and contrast stay as the preset made them.
  Near-grey colours (most labels) hardly change; the arcs, values, START fill and the held
  key take the new hue. 0.2.5 set a fixed saturated colour and moved the ink towards
  black or white for contrast, which changed the brightness. Double-click still brings
  back the preset's colours.
* **TRAIL** is gone. A trail is the straight path a grain travelled in that time, so long
  times drew long bars. The grains' paths glow a fixed 0.45 s (`MapView::kTrail`, the
  value the user had chosen); the ended-grain store keeps 96 entries.
* **WAVE** takes TRAIL's place: a slider of HUE's size and look (`WaveSlider`: a groove,
  filled in the controls' colour up to the value; double-click: 100 %) with its value
  below. It scales the waveform on the glass from 25 to 200 %, the three parts (outside,
  inside, outside the loop) alike, at most fully opaque, so the loop stays as clear.
  Saved per project.
* **State**: the controller's state is version 6: WAVE as int32 percent where version 5
  had the trail in milliseconds (42 bytes). Version 5 is still read; its trail is checked
  and ignored, WAVE is then 100 %. Versions 1 to 4 as before.

## Files

`source/gui_skin.cpp` (HUE), `source/map_view.*` (fixed trail, `SetWave`),
`source/editor.*` (`WaveSlider`, `SetWave`, `ApplyWave`), `source/controller.cpp`,
`source/parameters.h` (version 6, WAVE's range), `source/gui_layout.h` (WAVE's place),
`tests/test_plugin.cpp` (states 5 and 6, the trail fixed), `tests/preview_window.cpp`
(two cases with HUE and WAVE), version 0.2.6, CHANGELOG, README, `DAW_TEST_0.1.0.md`
(section 0.2.6).

## Tests

Run on 2026-11-19 on exactly this state; logs in `out/messier-0.2.6/logs/`.

| Build | ctest (Messier tests) | Validator (all four plug-ins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 6/6 | 47/0 each |
| VS 2026 Debug | 6/6 | |
| VS 2022 (MSVC 19.44) Release | 6/6 | |

Firmware comparison: 0 of 960,000 samples differ. ASan was not run. HUE and WAVE are not tested
automatically in the window (mouse events need a host); WAVE's range is tested in the
saved state.

Previews: 39 pictures in `out/messier-0.2.6/preview/` (thirteen cases at 0.5x, 1x and
2x), among them Slate with HUE 30 (orange) and WAVE 160 %, and Gold with HUE 200 (blue)
and WAVE 50 %. I looked at them: the turned arcs and values keep the brightness of the
preset's own colour, and WAVE dims or brightens the waveform with the loop still
clearer.

## Codex handoff for Claude Code and the next GitHub review

The following changes were made for local Studio One skin testing on 19 November 2026.
They are uncommitted in `.work/messier-gui/wt` on `messier-gui`, whose HEAD is `6a2ce0c`.
The worktree already contains the 0.2.6 changes. Review the sections below against that
work in progress and bring these changes through the next GitHub review.

### Chassis alignment when KEYS or DETAILS is toggled

The 880 × 724 chassis bitmap was stretched to 740 layout units when both sections were
open. Shorter views drew the bitmap at its native scale. The scale difference made the
vines in the new Plum skin move relative to the knobs and their scales.

`source/panel_view.cpp` now draws the chassis at the fully open layout height in all
four section states and clips it to the visible panel. This uses the fully open view as
the reference for skin placement. The same rule applies to every skin at both bitmap
resolutions. The 1× and 2× Plum chassis artwork is loaded from the installed `Messier-Skin`
folder. The current artwork was rendered from the original metal with a vignette sampled
from the user's PaintShop Pro version. The new vine passes behind the primary knob caps
and stays clear of the names, values, and time scale. The original chassis files are in
`Messier-Skin/Messier-Skin-Backup`.

`tests/preview_window.cpp` now also renders Plum with KEYS only and DETAILS only. The
chassis pixels in the inspected bands at both sides of the primary knob row matched
across all four window states at 0.5×, 1×, and 2×. The Release build passed the 47 VST3
validator checks and the `messier.plugin` and `messier.provenance` tests. The section
positions were checked in offscreen renders. Studio One verification is pending.

### Plum WAVE default and controller state

Plum now defaults to 200% WAVE. The other presets default to 100%. Double-clicking WAVE
restores the active preset's default. A saved explicit value, including 100% in a
version 6 Plum project, takes precedence. Controller state version 7 uses `kPresetWave`
(0) in the existing WAVE field to represent the preset default. Versions 1–6 remain
readable. Restoring GUI state saved by this test build requires a reader that supports
version 7.

Review `source/editor.h` (`EditorSettings::WavePercent`), `source/editor.cpp`
(`SetGuiPreset`, `SetWave`, `ApplyWave`, and `ApplySettings`), `source/controller.cpp`
(`Controller::setState`), and `source/parameters.h`. `tests/test_plugin.cpp` covers the
default, saved values, and preset changes. The README and preview defaults reflect the
new behavior. All six Messier tests passed for the WAVE change before the chassis fix.

### Palette menu order

The user wants the menu ordered Slate, Silver, Gold, Plum. `source/editor.cpp` uses
`kPresetMenuOrder` to populate the menu and translate the clicked row back to its
`GuiPreset`. Project state and per-preset HUE values still refer to the original IDs,
with Plum as 2 and Gold as 3. `tests/test_plugin.cpp` checks the displayed order and the
check marks for Gold and Plum. The skin-test reload action follows the four presets.
The plugin and provenance tests and all 47 VST3 validator checks passed for this build.

The combined Windows x64 skin-test module was built with `MESSIER_SKIN_FOLDER=ON` and
installed in the standard VST3 folder. The same binary is in
`out/messier-0.2.6-skintest-dunkle-regler/Messier.vst3`. Its SHA-256 is
`e94db85e0ffec2be4fb579558e74640a78519938ef446d8419a7eb678b65e28d`. Restart Studio One to
load it for the DAW test.

## Open

* The user's DAW test, section 0.2.6 of `DAW_TEST_0.1.0.md`.
* Codex' check of 0.2.5 and 0.2.6 together (`CODEX_PRUEFAUFTRAG_0.2.5.md`, plus this
  report).

### Next pass for Claude Code — sample name in Studio One

Fix the sample name refresh in the next build. The user reports that the name beside
**LOAD** stays at `down from.wav` after loading another sample in the Messier skin-test
build. Reproduce this in a single Studio One instance by loading two valid WAVs with
distinct names. After the second load completes, the header should show that file's name
with the editor still open. Check the waveform and the file information to determine
whether the issue is limited to the header.

The preview program uses `down from.wav` as example data. The live VST3 label takes its
value from `FileStatus.path`.
Trace the response from `Processor::SendFileStatusUnchecked` through `Controller::notify`
to `Editor::ShowFileStatusUnchecked`. One code path to verify is the empty `detail`
attribute. The processor sends it as a zero-length binary attribute and the controller
rejects the entire status if the host omits it. The cause in Studio One is still open.

In the same pass, match the filename to the upright type used in the surrounding header.
The label currently uses `skin.Italic(19)` in `source/editor.cpp`. Keep the filename's
original spelling and case, and check that long names still fit the available width.

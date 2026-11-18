# Build report: Messier 0.2.1, the user's first look at the window and Codex' check of 0.2.0

Date: 2026-11-18. Built and tested by Claude (Opus 5.5) in the worktree
`.work/messier-gui/wt` on the branch `messier-gui`, on top of `37d4466` (0.2.0, code
`17a6970`, pushed), **uncommitted**. The sound is unchanged; the bit-exact comparison with
the firmware core passes as before.

Codex released 0.2.0 for the controlled DAW test with four findings
([CODEX_REVIEW_0.2.0.md](CODEX_REVIEW_0.2.0.md)). The user tried 0.2.0 in Studio One:
operation, scaling (edge and menu), STOP and the graphics work; seven wishes and one
about the font. All are in 0.2.1.

| Point | Kind | State | Where | Test |
| --- | --- | --- | --- | --- |
| 1 The values under the knobs do not change while a knob moves | user, defect | fixed | `source/editor.cpp` (`ShowValue`, file texts) | – (needs a host's timer) |
| 2 Knob changes freeze or stutter the plate's animation | user, defect | fixed | `source/map_view.*` (moving on between displays), `source/editor.cpp` (`Animate`) | – |
| 3 The plate should follow START more precisely and fluidly | user | done | `source/map_view.*` (`SetStart`, `SetSize`, `LoopStart`, `LoopLength`) | – |
| 4 The waveform on the plate, colour and contrast adjusted (trial) | user | done | `source/map_view.cpp` (`DrawWave`), `source/gui_layout.h`, `source/gui_skin.*` (`plate_wave`) | – |
| 5 The nebula should change and move slowly | user | done | `source/map_view.cpp` | – |
| 6 RESET sets all knobs to their defaults and ends Freeze | user | done | `source/editor.cpp` (`PressReset`) | `TestWindow021` |
| 7 A click on the snowflake ends Freeze | user | done | `source/map_view.*` (`SnowflakeBox`, `PressAt`), `source/editor.cpp` | `TestWindow021` |
| 8 A sans-serif font for the controls | user | done | `source/gui_skin.*` (`Sans`), `panel_view.cpp`, `widgets.cpp`, `map_view.cpp` | – |
| MS-R40 A menu that fails to show stays in the frame; closing may crash | P2 | fixed | `source/editor.cpp` (`PopUpMenu`, window serial) | partly, see below |
| MS-R41 A preset change can be taken in part after an exception | P3 | fixed | `source/editor.cpp` (`SetGuiPreset`, `ApplySettings`, `ApplySkin`) | – |
| MS-R42 The menus show no ticks | P3 | fixed | `source/editor.cpp` (`MakePresetMenu`, `MakeSizeMenu`) | `TestWindow021` |
| MS-R43 A new grain can inherit a wrong tail | P3 | fixed | `source/map_view.cpp` (`FollowGrains`) | `TestWindow021` |

## 1 and 2: one cause

Since 0.2.0 (and before) the value labels were changed with `CTextLabel::setText`, which
only marks a view dirty; VSTGUI draws dirty views on the window's timer
(`CFrame::idle`). The window asks the processor for the display on the same timer.
In the SDK's editor host a knob turned by the wheel updated its value at once (seen in a
test), so the labels work; in Studio One the timer evidently does not come through while a
knob is dragged. Both the values and the plate then stood still.

* The labels are invalidated directly after `setText` (values, file name and info,
  catalogue number), so they are drawn with the next paint, not the next timer.
* `Editor::Animate` is the window's heartbeat: it asks for the display at most every
  24 ms and lets the plate draw again. It runs on the timer and also on every edit from the
  window or the host (`valueChanged`, `ParameterChanged`), so a drag keeps the plate going.
* Between two displays the plate moves the playhead and the grains on at their measured
  speeds (playhead: from two displays, inside the loop; grains: speed and window phase,
  only for grains whose moves agree, see MS-R43), for at most 0.15 s; a display unchanged
  for longer than 0.1 s (the engine rests) stops this. The plate is drawn every 25 ms at
  most, about 40 times a second.

## 3: the loop at once

The engine sets the loop from the smoothed pots: start = START x length, length =
SIZE^2 x (length - start) (`LooperEngine::Display`). The plate now draws the loop, its
brackets, the waveform's bright part and the START value in seconds from the parameters as
they change, not from the next display; the playhead moves on inside that loop. The
engine's own smoothing (20 ms) is not drawn.

## 4: the waveform on the glass (trial)

The strip under the plate is gone; the plate is 242 units high instead of 186 (its bitmap
stretched), the bezel follows. The waveform is drawn once per file, look and device scale
into a bitmap (`COffscreenContext`) in the preset's new colour `plate_wave` (Slate: a cool
`#B0BCDE`), 84 % of the plate's height, and laid on the glass above the nebula and below
grid, loop and grains: 16 % strong outside the loop, 30 % inside. The loop's edges are now
accent brackets on the plate. To go back, `layout::kPlateHeight` and the old strip; the
user decides after the DAW test.

## 5: the nebula

Each nebula moves on its own slow sines, horizontally up to 70 and 80 units, vertically up
to 12, swells by up to 5 %, and the two take turns; periods 45 to 90 s, at most about
6 units a second. With the plate drawn every 25 ms the motion is smooth.

## 6 and 7: RESET and the snowflake

RESET in the window first sets START and the 14 knobs to their defaults (`kDefaults`), as
edits with begin, perform and end, only where a value differs, then ends Freeze, then, as
before, sends the reset message (playhead back to START, MS-R08). Parameter 121 (for
automation) is unchanged: the firmware's reset of the playhead. A click on the snowflake
(22 units square at the frozen playhead) ends Freeze as the FREEZE button does; elsewhere a
click sets START as before.

## 8: the font

The controls' texts (knob names, the places reserved for 0.3.0, buttons, the seconds
axis, L and R, START and the legend) are Bahnschrift bold, a sans serif of Windows, with
less space between the letters. Brand, version, file name and catalogue number stay in
Palatino Linotype; values stay in Consolas.

## Codex' findings

* **MS-R40:** both menus open through `PopUpMenu`. It holds a reference of its own for the
  whole call, and if the menu was not answered when `popup` returns or throws (Windows
  answers before it returns), it takes the menu out of the frame. The answer is dropped if
  the editor is gone or if it belongs to an earlier window of this editor (`window_serial_`,
  counted up on every open). Not tested: a frame without a platform window, as Codex' test
  used, crashes inside VSTGUI in this test program (`COptionMenu::popup` reaches the missing
  platform frame); the test checks only that nothing is shown without a frame.
* **MS-R41:** `SetGuiPreset` makes the new skin, hands it to every view, and only then takes
  the preset number and reports the change; `ApplySkin` does each step on its own guard. The
  same preset again redraws if the window does not show it (a failed change before).
  `ApplySettings` takes the project's preset, makes its look under its own guard, and takes
  zoom, keyboard and transposition in any case.
* **MS-R42:** both menus use the multiple-check style, in which the Windows menu shows each
  entry's own tick; a freely dragged size ticks nothing. (Codex suggested the single-check
  style with a current index; the multiple-check style needs no index for "none".)
* **MS-R43:** a grain takes the speed of a match only after two moves in a row that agree
  (within 35 %); a match that another candidate fits nearly as well (within twice the error
  plus 64 frames) is not taken; a move longer than a quarter of the file is not taken in
  either direction. Codex' case (40,000 frames in 25 ms, then 1,200) gives no tail; a grain
  moving evenly at 48,000 frames per second gets exactly that speed after three displays.
  Taking the rule out once made the test fail.

## Build matrix

Run on 2026-11-18 on this state; logs in `out/messier-0.2.1/logs/`.

| Build | ctest | Validator (all four plug-ins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 16/16 | 47/0 |
| VS 2026 Debug | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | 16/16 | 47/0 |
| ASan (the five Messier test programs) | all exit 0, no report | – |

Bit-exact with the separately built firmware core as before (plug-in: 0 of 960,000 samples
differ).

## Looked at

In the SDK's editor host at 120 %: the sans serif, the taller plate and the nebula; a wheel
turn on SPEED changes its value. No sample was loaded there.

## Not checked

* Points 1 to 5 and 7 in Studio One: they depend on how the host drives the window's timer
  and on a sample playing (DAW test list, section 6, "0.2.1").
* The menus in a host (ticks, MS-R40's failure path cannot be provoked there).
* CPU with the plate drawn about 40 times a second.

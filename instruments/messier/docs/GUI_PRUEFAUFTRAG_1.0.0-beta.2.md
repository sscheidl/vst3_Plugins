# Review brief: Messier 1.0.0-beta.2, GUI and visualisation only

Commit `768cea1` (`768cea1eb210fe6ad3708cb86b3c3230f6743fb5`) on branch
`claude/messier-beta2-117e35` of the public repository `sscheidl/vst3_Plugins`, compared
with beta 1, `a6823d6` (tag `messier-v1.0.0-beta.1`). Beta 2 replaces beta 1's window with
a round glass window, built in five test rounds with the user:
- a fish-eye plate with GALAXY and WAVE views;
- painted galaxy presets tinted by HUE;
- counter-rotating dark clouds and star-shaped grains;
- a two-thumb START/END bar;
- a one-octave keybed;
- new buttons.

The changelog and the build report of the commit describe it:
* <https://github.com/sscheidl/vst3_Plugins/blob/768cea1eb210fe6ad3708cb86b3c3230f6743fb5/instruments/messier/CHANGELOG.md>
* <https://github.com/sscheidl/vst3_Plugins/blob/768cea1eb210fe6ad3708cb86b3c3230f6743fb5/instruments/messier/docs/BUILD_REPORT_1.0.0-beta.2.md>

**Scope: only the window and its drawing.** Look for bugs and for performance gains.
**Out of scope, do not review:**
- the sound engine and core (`engine/`, `core/`, `adapter/`);
- MIDI handling;
- the VST shell: processor, factory, parameters' DSP mapping, the processor's state and messages;
- the file loader thread.

Where a GUI file touches one of these, review only the GUI side.

The sound was checked separately (firmware comparison unchanged); the full test matrix
(Debug, VS 2022, ASan) has **not** been rerun since round 1 of beta 2.

How to use this brief:
- **Codex** takes part 0 (it can build and run).
- **GPT** takes parts A, B and C, **one part per chat**, A first.
- Give all answers to Claude, which checks each finding in the code before anything is fixed.

---

## Part 0: Codex (local, may build and run)

```text
Review the GUI and visualisation code of Messier 1.0.0-beta.2 for bugs and performance.
Repository: D:\Eigene Dateien\Eigene Dokumente\Playground\vst3_Plugins
Commit: 768cea1eb210fe6ad3708cb86b3c3230f6743fb5 (branch claude/messier-beta2-117e35).
Baseline: a6823d6 (tag messier-v1.0.0-beta.1).

Work in your own checkout, never in another session's worktree:
  git worktree add .work/messier-review-beta2 768cea1
Remove it when you are done (git worktree remove). Do not commit, push or install.

Scope (only these files, under instruments/messier/):
  source/map_view.cpp, map_view.h, fisheye.h       (the glass: GALAXY, WAVE, grains, dust)
  source/widgets.cpp, widgets.h                    (knobs, buttons, StartBar, ViewSwitch, keybed)
  source/editor.cpp, editor.h                      (window, layout, settings, timer)
  source/gui_layout.h, view_layout.h, panel_view.* (layout and the panel)
  source/gui_skin.cpp, gui_skin.h                  (presets, HUE, bitmaps, custom skins)
  source/value_display.h                           (value texts; PITCH in semitones)
  source/wave_overview.*                           (only the new RMS per column)
  source/controller.cpp                            (only getState/setState of the window
                                                     settings, versions 8 and 9)
  tests/test_plugin.cpp, tests/preview_window.cpp  (the GUI tests and the preview tool)
Out of scope: engine/, core/, adapter/, MIDI, processor.*, factory.cpp, the processor's
state, file_loader.*. Do not report findings there.

You may build (CMake presets of the repository, VS 2026), run the Messier tests
(ctest -R '^messier\.') and the preview tool preview_messier_window to render the window
offscreen. Measure where you can instead of guessing.

Questions:
1. Correctness:
   a) Do fish-eye mapping and inverse (fisheye.h, MapView::PlateToView, hitTest, the mouse
      handlers) agree everywhere, including at the rim and at 50 % and 200 %?
   b) StartBar with two thumbs (DragTo, SizeAt, Share, StartX, EndX, LoopFrames): can START
      or END leave the file, swap, or send values out of range or without beginEdit/endEdit?
   c) The view switch around BLEND and the knob: can a click reach the wrong control?
      The PITCH snap (SnappedByKnob in editor.cpp, used in Editor::valueChanged): does it
      stay in the window, and does it interact badly with fine-drag, double-click
      defaults or host automation while the knob is held?
   d) The keybed (KeyboardView, NoteAtPoint, SetTranspose, Press/Release): wrong notes at
      the octave limits, stuck notes on hide (KEYS), resize or window close?
   e) Window size with KEYS and DETAILS (Editor::SetKeyboardVisible, SetDetailsVisible,
      Resize, OnSizeUnchecked, checkSizeConstraint): can the content and the host size
      disagree, or the settings be lost?
   f) Controller state versions 8 and 9 (window settings only): old projects (versions 1 to
      7, beta 1) and the preset IDs 0 to 3 that now mean other presets; any state that
      should be refused but is accepted, or the reverse?
   g) The WaveOverview grew by the RMS per column: does every reader check the new size?
   h) Lifetime: timers, Editor::Later, offscreen bitmaps and skins after close or reopen.
2. Performance (the window redraws every 25 ms, kFrameInterval in map_view.cpp):
   a) What does one frame cost in GALAXY and in WAVE at 100 % and 200 %, with few and
      with many grains? Where does the time go (gradients, paths, offscreen rebuilds)?
   b) Which drawings are rebuilt per frame although their inputs did not change (HUE,
      preset, size, file)? What could be cached, and what invalidates the cache?
   c) Allocations per frame (createGraphicsPath, CGradient::create, offscreen contexts).
   d) Is the whole view invalidated where a smaller rect would do? Does the timer keep
      running while the window is hidden or the host is idle?
   e) Several instances with open windows: anything that scales badly?

Rules: name the function and quote the line; no style remarks. For each finding:
MS-R63+ · P1-P3 (or PERF-high/medium/low) · title / evidence / trigger / proven
(test, measurement) or suspected / fix in one sentence.
Write the result in English to CLAUDE_REVIEW_1.0.0-beta.2_GUI.md in the root of your
review checkout and tell the user where it is.
```

---

## Part A: GPT, the glass (rendering and performance)

```text
You are reviewing the GUI code of the user's own audio plug-in (VST3, VSTGUI 4, C++20,
Windows). Read only; do not build or run anything. Commit 768cea1 of the public
repository sscheidl/vst3_Plugins.

Whole files:
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/768cea1eb210fe6ad3708cb86b3c3230f6743fb5/instruments/messier/source/map_view.cpp
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/768cea1eb210fe6ad3708cb86b3c3230f6743fb5/instruments/messier/source/map_view.h
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/768cea1eb210fe6ad3708cb86b3c3230f6743fb5/instruments/messier/source/fisheye.h

The view draws a round glass with either a rotating painted galaxy plus dark clouds
(GALAXY) or a waveform mapped through a fish-eye lens (WAVE), and the grains as stars
with rays and afterglow. It redraws every 25 ms (kFrameInterval).

Read in full: MapView::DrawUnchecked, DrawGalaxy, DrawDust, DrawDustLayer, DrawWave,
DrawStarfield, DrawTrail, StarLook, StarFly, Afterglows, Tick, AdvanceDrift, EaseTurn,
TurnedNow, TurnTarget, PlateToView, hitTest, the mouse handlers; fisheye.h in full.

Scope: only this view. Do not review sound, MIDI or the VST shell.

Questions:
a) Bugs: wrong drawing or hit tests (fish-eye and its inverse, the rim, 50 % and 200 %
   zoom), divisions by zero, NaN, angles that grow without bound in long sessions,
   anything that can throw or crash in draw.
b) Performance: what is rebuilt or allocated in every frame although its inputs did not
   change (paths, gradients, offscreen bitmaps)? What can be cached, and what must
   invalidate the cache (HUE, preset, size, file)? Is invalid() wider than needed?
c) Does the animation keep a steady pace (time-based, not frame-count-based), and does
   it stop costing CPU when nothing moves (FREEZE, no file, hidden window)?

Rules: only these files; if you need another file, name it instead of guessing. Do not
cite line numbers: cite the function name and quote the code line. No style remarks.
For each question answer "OK" with one sentence of reasoning, or a finding:
B2A.n · P1-P3 or PERF-high/medium/low · title / evidence: function + quoted line /
trigger / proven or suspected / fix in one sentence.
```

---

## Part B: GPT, controls and window

```text
You are reviewing the GUI code of the user's own audio plug-in (VST3, VSTGUI 4, C++20,
Windows). Read only. Commit 768cea1 of the public repository sscheidl/vst3_Plugins.

Whole files:
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/768cea1eb210fe6ad3708cb86b3c3230f6743fb5/instruments/messier/source/widgets.cpp
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/768cea1eb210fe6ad3708cb86b3c3230f6743fb5/instruments/messier/source/widgets.h
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/768cea1eb210fe6ad3708cb86b3c3230f6743fb5/instruments/messier/source/editor.cpp
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/768cea1eb210fe6ad3708cb86b3c3230f6743fb5/instruments/messier/source/editor.h
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/768cea1eb210fe6ad3708cb86b3c3230f6743fb5/instruments/messier/source/gui_layout.h

Read in full: StartBar (all functions), ViewSwitch, ToggleButton, MetalKnob,
KeyboardView; in editor.cpp SnappedByKnob, Editor::LayOut, SetKeyboardVisible,
SetDetailsVisible, Resize, OnSizeUnchecked, checkSizeConstraint, ApplySettings,
SetGalaxy, SetGuiPreset, SetAccent, ShowValue, ShownKnobValue, valueChanged, beginEdit,
endEdit, Animate, close.

What changed since beta 1: a START bar with a second thumb for the loop end (LOOP END,
the same parameter as the knob); a GALAXY/WAVE switch; DETAILS as a button; FREEZE and
RESET moved; all text buttons one size; the PITCH knob snaps in 50-cent steps in the
window only (SnappedByKnob, called from Editor::valueChanged; automation and MIDI stay
continuous); value texts follow HUE; a one-octave keybed
(13 pads) that KEYS shows and hides.

Scope: only the window. Do not review sound, MIDI handling or the VST shell.

Questions:
a) Can a control send a value out of range, without beginEdit/endEdit, or twice; can
   the two START bar thumbs cross or leave the file; does the PITCH snap ever reach the
   parameter itself?
b) Window size: can KEYS or DETAILS leave the content and the host window at different
   sizes, or lose the setting? Any layout overlap at 50 % or 200 %?
c) The keybed: wrong notes, stuck notes after hiding it, resizing or closing the window?
d) Performance: work done per value change or per frame that could be skipped (text
   formatting, colour mixing, relayout)?

Rules: only these files; name another file instead of guessing. Do not cite line
numbers: cite the function name and quote the code line. No style remarks. For each
question answer "OK" with one sentence of reasoning, or a finding:
B2B.n · P1-P3 or PERF-high/medium/low · title / evidence: function + quoted line /
trigger / proven or suspected / fix in one sentence.
```

---

## Part C: GPT, skins, presets and saved window settings

```text
You are reviewing the GUI code of the user's own audio plug-in (VST3, VSTGUI 4, C++20,
Windows). Read only. Commit 768cea1 of the public repository sscheidl/vst3_Plugins.

Whole files:
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/768cea1eb210fe6ad3708cb86b3c3230f6743fb5/instruments/messier/source/gui_skin.cpp
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/768cea1eb210fe6ad3708cb86b3c3230f6743fb5/instruments/messier/source/gui_skin.h
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/768cea1eb210fe6ad3708cb86b3c3230f6743fb5/instruments/messier/source/controller.cpp
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/768cea1eb210fe6ad3708cb86b3c3230f6743fb5/instruments/messier/source/parameters.h
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/768cea1eb210fe6ad3708cb86b3c3230f6743fb5/instruments/messier/source/wave_overview.cpp
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/768cea1eb210fe6ad3708cb86b3c3230f6743fb5/instruments/messier/source/wave_overview.h

Read in full: gui_skin.cpp (the preset table kPresets, Skin, SetAccent, the bitmap and
custom-skin loading); in controller.cpp only Controller::getState and
Controller::setState (the window settings, state versions 1 to 9); in parameters.h the
comments and constants around kControllerStateVersion; wave_overview.* (the RMS per
column added in beta 2).

What changed since beta 1: the GUI presets 0 to 3 now name galaxy/nebula variants, plus
painted galaxies (blue, orange, red, violet) as further presets; HUE tints the chosen
galaxy and the accents; state version 8 stores GALAXY/WAVE, version 9 stores the count of
accent hues before them; the waveform overview carries an RMS value per column.

Scope: only these parts. Do not review the processor, its state or the sound.

Questions:
a) setState: is every old state (versions 1 to 7 from beta 1 and earlier, 8, 9) read as
   intended, and refused only when it is really invalid? Can a beta 1 project's preset ID
   select something unexpected? Can a large count in version 9 cause harm?
b) Bitmaps and custom skins: missing, wrong-sized or broken files, the fallback to the
   built-in ones, and what happens when a preset is added later.
c) HUE tinting: is it done once per change or per frame; can it be cached?
d) The RMS per column: correct for mono and stereo, for very short files and for silence?

Rules: only these files; name another file instead of guessing. Do not cite line
numbers: cite the function name and quote the code line. No style remarks. For each
question answer "OK" with one sentence of reasoning, or a finding:
B2C.n · P1-P3 or PERF-high/medium/low · title / evidence: function + quoted line /
trigger / proven or suspected / fix in one sentence.
```

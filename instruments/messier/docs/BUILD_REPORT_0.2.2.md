# Build report: Messier 0.2.2, Codex' check of 0.2.1 and the design round for Slate

Date: 2026-11-19. Built and tested by Claude (Opus 5.5) in the worktree
`.work/messier-gui/wt` on the branch `messier-gui`, on top of `69aa73b` (0.2.1, pushed),
**uncommitted**. The sound is unchanged; the bit-exact comparison with the firmware core
passes as before.

Codex did not release 0.2.1 for the DAW test: one P2 (MS-R44) and three P3
([CODEX_REVIEW_0.2.1.md](CODEX_REVIEW_0.2.1.md)), with remarks on MS-R40, MS-R41 and
MS-R43. The user then gave a design round for Slate: twelve points
(`out/messier-gui-entwurf-2026-11-19/CLAUDE_AUFTRAG_MESSIER_GUI.md` in the main checkout,
with the draft `Messier-Slate-GUI-Entwurf.png`; the brief's text goes before the picture)
and five decisions on 2026-11-19 (below). All are in 0.2.2.

## Codex' findings

| Point | Kind | State | Where | Test |
| --- | --- | --- | --- | --- |
| MS-R44 A parameter change can call a window the host just closed | P2 | fixed | `source/editor.*` (`AnimateLater`, `Animate`, `~Editor`), `source/controller.*` (`ForEachEditor`, poll spacing) | `TestWindow021` |
| MS-R45 The playhead runs backward on short loops, keeps an old speed after a pause | P3 | fixed | `engine/looper_engine.*` (`head_speed`), `source/display_data.cpp`, `source/map_view.*` (`HeadAt`) | `TestWindow021` |
| MS-R46 Automation of START or SIZE only at the processor leaves the brackets behind | P3 | fixed | `source/map_view.*` (`ShownLoop`, `kPreview`) | `TestWindow021` |
| MS-R47 The waveform bitmap is retried on every frame; the loop drawn over the rest | P3 | fixed | `source/map_view.cpp` (`DrawWave`) | – |
| MS-R43 (rest) Short files can give a consistently wrong tail | P3 | fixed | `engine/looper_engine.*` (`GrainDot::speed`, `phase_rate`), `source/map_view.*` | `TestWindow021` |
| MS-R41 (rest) The file info's colour behind the texts | P3 | fixed | `source/editor.cpp` (`ApplyFileInfoColour`) | – |
| MS-R40 (remark) `isChild` instead of `isAttached` | – | done | `source/editor.cpp` (`PopUpMenu`) | – |

* **MS-R44:** `Editor::ParameterChanged` no longer asks the processor. It lets the plate
  draw and puts one request on VSTGUI's main queue (`Tasks::schedule`), which runs after
  the controller's call has returned. The task checks that the editor is alive and that
  its window is the one it was made for, and at most one waits per window. `Animate`
  asks only while a frame is open. The controller calls its windows through
  `ForEachEditor`: from a copy of its list in which each window is held, and each only
  while it is still registered. `~Editor` takes itself out of the list before anything
  can reach the host. Several windows of one instance together ask at most every 20 ms.
  The test has two windows: the first closes the second from its call; the second is not
  called again.
* **MS-R45, MS-R43:** the guesses from two displays are gone. The display (456 bytes, the
  engine at 0.1.5) carries the core's own speeds:
  * the playhead's speed (frames of the file per second, signed, 0 while frozen);
  * for each grain, its step and its window increment, per second;
  * a count of the rendered quanta, so a display with the same count is one without
    audio in between.

  Between displays the plate moves the playhead on inside the display's own loop, and
  each grain round the file, for at most 0.15 s. A display unchanged for 0.1 s stops
  this. Codex' cases are tests: the 2,000-frame loop at +1x runs forward, the
  2,000-frame file's grain goes round forward, and after a pause only the new speed
  counts.
* **MS-R46:** the plate draws the loop from START and SIZE for 300 ms after a change and
  then the loop the core reports. The playhead stays in the core's loop.
* **MS-R47:** a waveform bitmap that cannot be made is tried again only after 2 s or for
  another key. The three parts (left of, inside and right of the loop) are drawn once
  each, with the strength that is meant.

## The design round

The user's decisions of 2026-11-19, through the session that gave the brief:

1. The light-metal bitmaps come from a script of mine, which is kept for the other
   presets. They are interim, since Codex/GPT may render final ones.
2. LATCH stays visible without the keyboard, dimmed and inactive.
3. The triangle's lane comes out of the plate's height; window size and aspect ratio
   stay.
4. INPUT GAIN and GRAIN FM (0.3.0) are hidden until 0.3.0.
5. The labels use Bahnschrift in a lighter weight with letter spacing, set glyph by
   glyph.

The nebula's drift follows the effective signed tempo, never the read position.

| Point | State | Where |
| --- | --- | --- |
| 1 Light metal for knobs and START thumb | done (interim bitmaps) | `tools/make_metal_bitmaps.py`, `resource/gui/messier_slate_{knob_l,knob_s,thumb,button,button_down}@{1,2}x.png` |
| 2 Grains in front, longer glow (0.3 s), cleared by a new file | done | `source/map_view.*` (`DrawTrail`, `Ended`, `Keep`, `Afterglows`), `source/gui_skin.cpp` |
| 3 Nebula drift follows the signed tempo | done | `source/map_view.*` (`DriftTarget`, `AdvanceDrift`, `SetStopped`), `source/editor.cpp` |
| 4 SPRAY large at the head of the upper row | done | `source/editor.cpp` (`kKnobs`) |
| 5 Waveform a little brighter inside the loop | done | `source/map_view.cpp` (`DrawWave`: 16 % / 40 % / 16 %) |
| 6 LATCH and KEYS under SPRAY | done | `source/editor.cpp`, `source/gui_layout.h` |
| 7 Blue-violet controls, orange markers, separate colours | done | `source/gui_skin.*` (`control`, `value`, `marker`), all views |
| 8 No screws | done | `source/panel_view.cpp`, `gui_skin.*`, `CMakeLists.txt` (the screw bitmaps are gone) |
| 9 Octave keys "− OCT" / "OCT +" | done | `source/editor.cpp`, `source/widgets.*` (`SetValueInk`) |
| 10 The triangle's own lane | done | `source/gui_layout.h` (`kMarkerTop`, `kMarkerBottom`, `kPlateTop` 42, `kPlateHeight` 232), `source/map_view.cpp` |
| 11 Active keys pressed in | done | `button_down` bitmap (shadow under the upper edge, lit lower edge), `source/widgets.cpp` |
| 12 STOP and the symbol keys as a group | done | `source/gui_layout.h` (gaps of 6 units) |

Notes on the points:

* **1:** `tools/make_metal_bitmaps.py` draws the knob caps and the thumb in warm satin
  silver, and the key faces in dark satin metal, raised and pressed in. Each is drawn at
  four times the size and reduced, at 1x and 2x, with the logical sizes `gui_skin.cpp`
  expects. The same call gives the same files (checked by hash). Each preset is one entry
  of parameters (base colours, ring and sheen strength, seeds). The pointer stays the
  plug-in's own, in Slate's dark `pointer` colour.
* **2:** a point of a grain's path glows 1 − (age / 0.3 s), eased (power 1.6). This holds
  while the grain sounds, back to its start at most, and for 0.3 s after it ended.
  * When a display comes, the grains of the one before that have ended by then are kept
    with where and when they ended.
  * A grain still in the new display is recognised by its pan, speed and window rate,
    which are its own for its whole life.
  * A grain over between two displays glows from where it ended.
  * A new file clears everything.

  The heads are warm white (`#FFF7E8`), with a soft warm halo and a thin dark edge
  against bright waveform. BLEND changes their strength by 15 % at most, 35 % before. On
  a 24 s file at 1x a grain moves about 35 units a second, so its path is about 10 units
  long: the user tunes `MapView::kTrail`.
* **3:** the target speed is the display's playhead speed over the file's rate (the
  effective tempo, signed) times 9 units a second, at most 22. The drift eases towards it
  with a time constant of 0.7 s, so a change of direction passes smoothly through rest.
  * Frozen (the core reports 0), STOP (from the parameter at once), a resting display or
    no file: the target is 0.
  * A turn of the loop or the file changes the read position, not the tempo, so the
    drift goes on.
  * Each nebula repeats every 0.9 of its width, so the drift can go on one way; the
    second moves at 0.62 of the first's speed.
  * The vertical sway, the swell and the turns of the two stay as in 0.2.1.
* **6:** LATCH (at FREEZE's height) and KEYS (at RESET's) are 82 x 26 units, in column 0.
  Without the keyboard, LATCH keeps its state, is dimmed and takes no mouse.
* **7:** the skin keeps `control` (`#9DACEB`: arcs, START rail, WINDOW curve, the struck
  key, small strokes, the drop frame), `value` (`#BEC8FF`: values, the START value, texts
  of keys that are on, the octave keys, the catalogue number) and `marker` (orange,
  formerly `accent`: brackets, the legend's loop, the triangle). The playhead line is
  light and neutral (`#F2F0EC`). The undelivered presets use their old accent for all
  three until they are designed.
* **9:** the octave keys are 82 x 26, 10 units from the keyboard, centred on its height,
  in the light blue-violet. At its limit a key is dimmed and takes no mouse, as before.
* **10:** labels, then their ticks (down to 20), then the lane (21 to 30), then the bezel
  (from 30). The plate is 10 units lower; its lower edge, the window's size and saved
  sizes stay.
* **12:** STOP 720–780, size 786–816, preset 822–852; the version ends 14 units left of
  STOP.
* **Font:** VSTGUI on Windows offers only normal or bold weight. The labels are now in
  Bahnschrift's regular weight. Their spacing is wider: knob names 1.9 units, small ones
  1.5, keys 1.8, drawn glyph by glyph as before (`DrawTracked`).

## Window previews

`preview_messier_window` (new, built with the tests, run by hand) opens the real `Editor`
in a hidden window. It uses the bundle's bitmaps (`resource/gui`) and a made-up file of
24 s: START 0.36, SIZE 0.75, the playhead on "14 s", five grains. Each picture is drawn
offscreen, at 1x, 2x and 0.5x:

* `keys_lowest`: keyboard shown, octave at its lowest ("− OCT" dimmed), LATCH on with a
  held key, KEYS on.
* `keys_highest_stop`: keyboard shown, octave at its highest ("OCT +" dimmed), STOP on,
  LATCH off.
* `nokeys_freeze`: keyboard hidden (LATCH dimmed while on), FREEZE on (dashed playhead,
  snowflake).

They are staged in `out/messier-0.2.2/preview/`. I looked at all of them:

* The keys and their gaps.
* The octave labels and the dimming at both limits.
* "14 s" stays free with the triangle under it.
* The pressed faces at 0.5x.
* The grains in front of bright waveform.

A preview does not show motion: drift, glow after a grain ended and the clearing on a new
file are in the tests and the DAW list.

## Tests

New `TestWindow022` (in `messier.plugin`):

* **Drift:** forward +9 units/s, eased there. A loop turn with the read position jumping
  back does not turn it. Backward −18, easing round through rest (still positive one frame
  later). 8x is capped at 22. Frozen and STOP give 0, and the drift comes to rest.
* **Glow:** a grain that ended between two displays glows until 0.3 s after its end, not
  after. A grain still in the new display is not counted. Another file clears the glow.
* **Layout:**
  * STOP and the symbol keys have even gaps.
  * LATCH and KEYS are level with FREEZE and RESET.
  * The lane lies between the axis and the bezel.
  * The plate's lower edge and the window height stay.
* **Colours:** Slate's control, value and marker colours are distinct.

`TestGuiPresets` counts 24 bitmaps (12 images, no screw). The 0.2.2 cases for MS-R43 to
MS-R46 are in `TestWindow021`.

## Build matrix

Run on 2026-11-19 on this state; logs in `out/messier-0.2.2/logs/`.

| Build | ctest | Validator (all four plug-ins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 16/16 | 47/0 |
| VS 2026 Debug | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | 16/16 | 47/0 |
| ASan (the five Messier test programs) | all exit 0, no report | – |

Bit-exact with the separately built firmware core as before (plug-in: 0 of 960,000
samples differ).

## Changed files

Under `instruments/messier/`:

* **Engine and display data:** `engine/looper_engine.{h,cpp}`,
  `source/display_data.cpp`.
* **Window:** `source/{controller,editor,map_view,widgets,panel_view,gui_skin}.{h,cpp}`,
  `source/gui_layout.h`, `source/version.h`.
* **Bitmaps:** `resource/gui/messier_slate_{knob_l,knob_s,thumb,button,button_down}@{1,2}x.png`
  (new); `messier_slate_screw@{1,2}x.png` (removed).
* **Tools:** `tools/make_metal_bitmaps.py` (new), `tools/stage_result.ps1`.
* **Tests:** `tests/test_plugin.cpp`, `tests/preview_window.cpp` (new).
* **Build and docs:** `CMakeLists.txt`, `components.json`, `CHANGELOG.md`, `README.md`,
  `docs/DAW_TEST_0.1.0.md`, this report, `docs/CODEX_PRUEFAUFTRAG_0.2.2.md`.

Outside it: `docs/PLANUNG.md`.

## Not checked

* Everything visual in motion and in a host: Studio One's timer, the drift, the glow,
  the menus, CPU with the plate drawn about 40 times a second (DAW test list, section 6,
  "0.2.2").
* The final look of the metal: the bitmaps are interim, by script.
* MS-R44 in a real host that closes a window from a status reply: the test models it
  without a host.

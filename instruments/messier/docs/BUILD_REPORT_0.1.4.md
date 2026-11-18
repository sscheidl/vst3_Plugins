# Build report: Messier 0.1.4, the display data path

Date: 2026-11-18. Built and tested by Claude (Opus 5.5) in the worktree `.work/messier/wt`
on the branch `messier-0.1.0`, on top of `160a478` (0.1.3); not committed on its own, but
together with the fixes of 0.1.5 (`CODEX_REVIEW_0.1.3_UMSETZUNG.md`). The user
decided on 2026-11-18 to build the data path of the window first, before its Atlas/Orion
layout and colours, which are still open (work order section 8).

**What 0.1.4 is:** everything the window needs to draw the nebula map from real data,
from the core to the controller, plus a plain provisional strip that shows it, so that
the path can be seen working in a DAW. **Not** the Atlas/Orion window: no nebula
shading, no Blend slider, no final layout or palette. The sound is unchanged; the
bit-exact comparison with the firmware core passes as before.

## 1. The path

```
audio thread                 UI thread (processor)            UI thread (controller, window)
-----------------------      ---------------------------      ------------------------------
process(): after the        <- request (every 30 ms while   <- the window's timer
block, if a request is         a window is open; carries the
waiting: LooperEngine::        overview number it holds)
Display() into a
LatestValue (no lock,       -> takes the newest frame,       -> checks the bytes
no allocation)                 replies with it, plus the        (ReadDisplayBytes,
                               overview of that file if         ReadOverviewBytes), keeps
loader worker / setState:      the controller lacks it          them, hands them to every
MakeOverview() right after                                      open window; the map
the file is read, kept by                                       redraws only on change
the loader under the
file's sequence number
```

* **Grain and loop data** (`EngineDisplay`, `engine/looper_engine.h`, 288 bytes): the
  sequence number and length of the file the core reads, START and SIZE as the core
  applies them (the same two lines as `Engine::Process`, from the same smoothed values),
  the playhead inside the loop, the spray range (the SPRAY secondary squared, times the
  file length), Freeze, whether a swap waits, and the sounding grains: read position,
  panorama, phase of the grain window. Waiting grains (Displacement) are left out; there
  are no invented points.
* **Core patch P4** (`core/engine.h`, see UPSTREAM.md): three `const` accessors for the
  grains, the smoothed controls and the secondaries. They change no value; the
  reference test against the unpatched core still passes bit for bit.
* **Waveform** (`WaveOverview`, `source/wave_overview.h`, 8208 bytes): per column the
  lowest and highest sample over all channels, 2048 columns (one per frame for shorter
  files), computed from the very PCM16 buffer the engine plays, on the loader's worker
  thread for a file from the window and in `setState` for a project's file, outside the
  loader's lock. Tagged with the file's sequence number; the loader keeps the newest
  four. A failed load makes no new number, so the old overview stays with the old
  sound.
* **Same generation:** the window shows an overview only if its number equals the one
  in the newest frame. During a swap the frame names the old file until the core has
  faded it out; then the new file and its overview appear together.
* **When nothing is drawn:** without an open window no requests come; the audio thread
  then publishes nothing (one relaxed atomic load per block). The window's timer runs
  only while it is open.
* **Thread safety:** audio to UI through `LatestValue` (three slots, one atomic
  exchange); the overview is shared immutable data (`shared_ptr<const WaveOverview>`),
  never touched by the audio thread. Messages are answered on the UI thread. Everything
  on the path is `noexcept` or guarded; out of memory means "no overview" or "no reply
  this tick", never a crash.
* **Damaged messages:** the controller refuses a frame or overview of the wrong size,
  with numbers out of range (NaN, positions beyond the file, more than 20 grains, flags
  other than 0 and 1), or an overview of another file.

## 2. Cost

Measured on this machine (i7-8700K), Release, printed by the tests:

| What | Where | Cost |
| --- | --- | --- |
| `LooperEngine::Display()` | audio thread, once per block while a window is open | 23 ns (5 grains) |
| publishing the frame | audio thread | one 288-byte copy and one atomic exchange |
| no window open | audio thread | one relaxed atomic load per block |
| `MakeOverview()` | loader thread, once per file | 52 ms for ten minutes of stereo at 48 kHz |
| messages | UI thread | about 33 a second each way while a window is open; 288 bytes, plus 8208 bytes once per new file |
| memory | | 8208 bytes per kept overview, at most four in the loader and one in the controller |

## 3. The provisional map

`source/map_view.*`: a strip between the file line and the knobs (the window grows by 130
pixels to 700 x 436, 522 with the keyboard). Across, the position in the file. Upper
part: the loop shaded, the spray range around the playhead (wrapped at the file's end,
as the core wraps the grains), the grains as dots, down their panorama (left at the
top), brightest in the middle of their window. Lower part: the waveform in the same
scale, brighter inside the loop. A white line for the playhead across both. Display
only, no input. Its colours and proportions are placeholders for the Atlas/Orion design.

## 4. Tests

All in Release (VS 2026), the three Messier tests that cover the path:

* `test_engine` `TestDisplay`: reading the display after each of 1500 quanta with every
  control moving, notes, Freeze and Reset leaves the output bit-identical to a run
  without; no allocation. Loop and playhead equal the core's formulas; grains are there
  and, without Pan Random, hard left and right as the core places them. Through a swap
  the old file until the core switches, then the new one; no file, nothing.
* `test_loader` `TestOverview`: the overview equals an independent computation over the
  buffer the engine gets (2048 columns, and one per frame for a 441-frame file); no
  overview for no file or a missing one; the newest four kept; an overview a window
  still holds stays valid; when only the overview's memory runs out, the file plays
  without one.
* `test_plugin` `TestDisplayPath`: through processor and controller, connected as a host
  connects them: nothing is published before the first request; 486 replies, each with
  the file the core reads and its own overview, which travels once; the loudest column
  is the file's loudest sample; the sound is bit-identical to an instance nobody
  watches, with no allocation in `process`; after a file change the new file and
  overview; damaged messages refused. `TestNoExceptionLeavesHostCalls` now has eleven
  calls: the display request (9 allocations) and the controller's reply (1) fail at
  each allocation in turn without an exception.

## 5. Build matrix

Run on 2026-11-18 on the uncommitted 0.1.4 state; logs in `out/messier-0.1.4/logs/`.

| Build | Build | ctest | Validator (all four plug-ins) |
| --- | --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | ok | 16/16 | 47/0 |
| VS 2026 Debug | ok | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | ok | 16/16 | 47/0 |
| ASan (the five Messier test programs) | ok | all exit 0 | no report |

Bit-exact with the separately built firmware core as before (engine layer in all five
scenarios, plug-in 0 of 960,000 samples differing). The countdown tests run in Release
builds only, for the reason given in the README.

## 6. Not checked

* How the map looks and moves in a DAW, at 50 to 200 % zoom (the user's test).
* A host that runs the controller in another process: the messages are byte copies of
  trivially copyable structs, checked on arrival, but this was not tried.
* At 30 ms the dots move in steps; a smoother picture would need interpolation in the
  window, left for the Atlas/Orion design.

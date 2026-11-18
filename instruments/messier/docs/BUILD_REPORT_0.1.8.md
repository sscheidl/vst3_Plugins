# Build report: Messier 0.1.8, Codex' check of 0.1.7 and drag and drop

Date: 2026-11-18. Built and tested by Claude (Opus 5.5) in the worktree `.work/messier/wt`
on the branch `messier-0.1.0`, on top of `46c3998` (0.1.6, pushed), together with 0.1.7,
**uncommitted**.

Codex checked the uncommitted 0.1.7: [CODEX_REVIEW_0.1.7.md](CODEX_REVIEW_0.1.7.md), no
release for the DAW test. MS-R29 is fixed, MS-R27 is moot, MS-R26 and MS-R28 are open,
and there are four new findings, MS-R30 to MS-R33; all four hold. 0.1.8 also adds drag
and drop of WAV files and LATCH for the screen keyboard, and it changes Speed, Pitch
Random and the WINDOW display after the user's DAW test of 0.1.7 (all wishes of
2026-11-18). The sound is unchanged; the
bit-exact comparison with the firmware core passes as before.

| Finding | Severity | State | Where | Test |
| --- | --- | --- | --- | --- |
| MS-R30 A late load before the new connection's first message replaces a restored project's file | P2 | fixed | `source/processor.*` (`disconnect`, `NotifyUnchecked`, `LoadState`), `source/controller.cpp` (`NewMessage`), `source/parameters.h` (`kAttrIssued`) | `TestLateRequest`, parts 2 and 3 (Codex' case) |
| MS-R31 A late key release ends a newer note | P3 | fixed | `source/processor.cpp` (`disconnect`, no exception for releases) | `TestLateRequest`, part 4 |
| MS-R32 A -> B -> A marks a load again | P3 | fixed | `source/controller.*` (`known_`, `CountFromStatus`) | `TestReturnToProcessor` (new) |
| MS-R33 The offset on parameter 199 does not exclude collisions | P3 | narrowed and documented | `source/processor.cpp` (`MakeSampleOffset`), README | none (a bound by arithmetic) |

## MS-R30, MS-R31: a hard barrier instead of generations alone

Generations could only tell the processor about a new connection once its first
message arrived. 0.1.8 gives the processor its own barriers, set by events it sees
itself:

* **The moment it is disconnected.** `Processor::disconnect` notes the time.
* **The moment a project is loaded.** `LoadState` notes it.

Every message of the controller now carries the moment it was made (`kAttrIssued`, in
ticks of `std::chrono::steady_clock`, one clock for the whole process). The processor
refuses:

* any request made up to its last disconnect: the old connection is closed, whatever
  of it arrives later;
* any load made up to its last project load: the project overtakes it.

The generation check from 0.1.7 stays as a third gate.

On a disconnect the processor also releases the screen keys the old connection still
holds, through the same queue as a normal release, which always has room for every
release. A late release of the old connection is then no longer needed. It is refused
like any other late request, so it cannot end a note that a newer connection struck on
the same key. The exception for releases from 0.1.7 is gone.

Tests (`TestLateRequest`):

1. A load held back past a reconnect and a newer load: refused.
2. **Codex' case:** a load held back; both ways disconnected and connected again, nothing
   of the new connection has reached the processor; a project with another file is
   restored. The late load is refused, and the project's file stays the one `getState`
   saves.
3. Without a disconnect: a load made before a project load is refused, one made after
   it is taken.
4. **MS-R31:** key 72 held by the old connection, its release held back. After the
   disconnect, a reconnect and a new strike of key 72, the late release is refused and
   the note still sounds an octave up.

With both barriers taken out, the test fails at Codex' case.

Limit, documented: the moment comes from one clock per process. A controller in another
process than its processor (rare) is covered only by the generations, as before.

## MS-R32: returning to a processor

The controller keeps the last count of files from the window of the eight most recent
processors. A status of a processor it has seen before marks only a higher count than
the one kept. Test (`TestReturnToProcessor`): processor A loads one file, the controller
moves to B and back to A, and A's status marks nothing. With the memory taken out, the
tests fail.

## MS-R33: collisions on parameter 199

The offsets are now spaced 4099 apart for the processors of one process, starting from
a random value per process. Two processors show the same value of parameter 199 only if
the older one's count of loads exceeds the newer one's by 4099·m, modulo 65535, where m
says how many processors later the newer one was created (1 for the next one). For the
next processor's first load, the one before would need 4100 loads.

A hard guarantee would need the sender's identity inside parameter 199, which a host
does not carry, or a host that drops late values on a change of processor. Where the
status reaches the controller, it marks the load in any case. This limit is in the
README.

## Drag and drop

`source/file_drop.*`: `PickDroppedWav` takes the first file path in a VSTGUI drag
package that ends in `.wav`, in any case. Text items and other files are skipped. The
path arrives as UTF-8 and goes to the loader as such. `FileDropTarget`:

* answers "copy" while a WAV is over it and "none" otherwise, so the system pointer
  shows "not allowed";
* tells the map to show "Drop WAV to load" while a WAV is over it;
* on a drop, calls the same load callback as LOAD... .

The target sits on the map and on a transparent zone over the file line. The zone also
carries the file name's tooltip, because the labels take no mouse. Knobs have no target,
so a stray drop on them does nothing. With no sample loaded, the map now says "no sample
- drop a WAV here or use LOAD...". Everything in the target catches its own exceptions,
and a callback that outlives its window does nothing, because it checks the editor's
`alive_` first.

Test (`TestFileDrop`): a fake drag package with text, a `.txt`, an upper-case `.WAV`
with umlauts and a counted terminator, a second `.wav` and a `.wa`. The upper-case WAV is
chosen. A package without a WAV is refused. Enter, move, leave and drop give the right
answers and highlight calls, and a load callback that throws makes the drop report
failure without letting anything out. The real drag from the Explorer is part of the DAW
test list.

## From the user's DAW test of 0.1.7

The user found operation and sound in order, with three wishes and one addition:

* **Speed: 0x exactly at 12 o'clock.** On the module, +1x sits at the centre of the pot
  and 0x at 40 % of its travel. The plug-in now gives the core the pot position
  `PotOf(Speed, u)`: 0.8·u below the centre, 0.4 + 1.2·(u − 0.5) above it. So u = 0.5 is
  the pot's 0.4 (0x), the ends stay −4x and +4x, and +1x (the default, double-click)
  lies at u = 7/12, about 1 o'clock, with the law's snap band.
* **Pitch Random: ±12 semitones.** The pot gets half the parameter
  (`PotOf(PitchRandom, u) = u/2`), so the top is the core's ±1 octave. The host shows
  semitones (`±12.0 st`).
* **WINDOW: a symbol instead of cryptic text.** The value label under the knob is a
  small curve of the mixed grain window, drawn from the core's own seven shapes and
  mixing (`BuildWindows`, `WinMix`; `WindowShapeView`). The knob's tooltip and the host
  show the nearest shape's name, with "~" when between two; typing that name picks the
  shape.
* **LATCH** (addition): a toggle next to KEYS. With it, a struck screen key stays held
  after the mouse is let go; another key moves the hold, the same key or LATCH off lets
  it go. Closing the window lets it go too (no note without a window that shows it). It
  is not saved with the song.

* **The map takes the mouse** (a further wish): a click or drag sets START to that
  place in the file, as an edit with begin, perform and end, which the host records
  like a knob turn. The wheel steps SPRAY by 0.025 of its range, 0.005 with Shift.
  Without a file a click does nothing. The map used to take no mouse; VSTGUI looks for
  drop targets only among views that do, so the drop on the map would not have worked
  in a host. Building this showed it, and the map now takes the mouse. The unit test of
  the drop target does not go through VSTGUI's search and could not have seen it.
  `TestMapEdits` drives the map's logic directly.

The core and its laws are unchanged. `PotOf` is applied wherever a parameter becomes a
pot value: `SettingsFrom`, `QueueParameter`, and the display and parse functions. The
firmware comparison in `test_plugin` gives the firmware the same pot positions and stays
bit-exact.

Songs saved with 0.1.7 or earlier keep their parameter values, so Speed and Pitch Random
sound different in them. None has been released.

Tests:

* `TestLaws` (test_engine): 0x at the centre, +1x at 7/12, the ends, `PotOf` at the
  centre and ends, Pitch Random at the top.
* `TestController`: the new texts and parsing (`+0.00` at the centre, `±12.0` st,
  `~Bartlett`, `~Ramp Up`).
* `TestKeyboardLatch`: hold, move, let go, LATCH off, and unchanged without latch. The
  test drives the keyboard's logic directly, because VSTGUI's mouse events need its
  platform.
* `TestWindowSymbol`: Rectangle flat, Bartlett a triangle, Gaussian highest in the
  middle.

## Build matrix

Run on 2026-11-18 on the uncommitted 0.1.8 state; logs in `out/messier-0.1.8/logs/`.

| Build | ctest | Validator (all four plug-ins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 16/16 | 47/0 |
| VS 2026 Debug | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | 16/16 | 47/0 |
| ASan (the five Messier test programs) | all exit 0, no report | – |

Bit-exact with the separately built firmware core, given the same pot positions
(plug-in: 0 of 960,000 samples differ). The countdown tests run in Release builds only
(README). The fixes of MS-R30 (the barriers) and MS-R32 (the memory of processors) were
each taken out once: the tests then failed.

## Not checked

* The drag from the Explorer in a real host, at 50 to 200 % zoom (DAW test list).
* Hosts that delay or reorder messages; the cases here are built by the tests.
* A controller in another process than its processor.

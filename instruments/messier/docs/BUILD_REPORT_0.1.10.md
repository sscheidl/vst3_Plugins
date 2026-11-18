# Build report: Messier 0.1.10, the findings of Codex' checks of 0.1.8 and 0.1.9

Date: 2026-11-18. Built and tested by Claude (Opus 5.5) in the worktree `.work/messier/wt`
on the branch `messier-0.1.0`, on top of `302b9fc` (0.1.7 to 0.1.9, pushed),
**uncommitted**. Codex' checks: [CODEX_REVIEW_0.1.8.md](CODEX_REVIEW_0.1.8.md) and
[CODEX_REVIEW_0.1.9.md](CODEX_REVIEW_0.1.9.md), no release for the DAW test, four new
findings MS-R34 to MS-R37. All four were traced in the code and hold. The sound while
playing is unchanged; the bit-exact comparison with the firmware core passes as before.

As in 0.1.7, each finding is fixed as a class, not as the reported case:

* MS-R35 is not only about Stop: any switch that acts on the whole block instead of its
  sample makes the output depend on the block size. Stop and Bypass now act on their
  sample, and everything else the engine hears is put on the engine's own time.
* MS-R36 and MS-R34 are about the screen keyboard being a second source of notes. It now
  follows the same rules as the host's MIDI while resting, and it shares notes with the
  host's MIDI on its channel instead of overriding them.
* MS-R37 is one unguarded copy, but the class is every call from VSTGUI, the host or the
  system into the window's code. All of them are now exception boundaries.

| Finding | Severity | State | Where | Test |
| --- | --- | --- | --- | --- |
| MS-R34 A disconnect ends a host MIDI note that the screen also held | P2 | fixed | `source/processor.*` (`screen_held_`, `host_held_`, `EngineNoteOff`, `ReleaseKey`, `ReadEvents`) | `TestScreenAndHostKeys` (new), Codex' case |
| MS-R35 Stop and Bypass ignore the sample offset of their automation | P2 | fixed | `source/output_gate.h` (new), `source/processor.*` (`ReadSwitches`, `Render`, event offsets) | `TestSwitchesOnTheSample` (new) |
| MS-R36 The screen keyboard plays while stopped, and sends MIDI | P2 | fixed, and the class (releases during a long pause) | `source/processor.cpp` (`PlayKeyboardNote`, `ReleaseKey`, `ReadEvents`) | `TestScreenAndHostKeys`, second part; `TestLongPause` (new) |
| MS-R37 A drop can allocate before its exception boundary | P2 | fixed, and the class | `source/file_drop.cpp`, `source/guard.h` (new), `source/editor.*`, `source/map_view.*`, `source/widgets.*` | `TestNoExceptionLeavesHostCalls`: three new calls |

State of the earlier findings, as Codex left them: MS-R30 and MS-R31 fixed. MS-R32 is
fixed within the memory of eight processors; a processor pushed out of it may mark the
project once more when it comes back, which the contract of 0.1.7 allows (a mark too
many, never one too few). MS-R33 stays a documented limit: parameter 199 alone, without
any status reply, cannot tell two processors apart in every case; a valid status reply
marks the project in any case (README, "The sample").

## MS-R35: Stop and Bypass on their sample

Codex showed that a Stop point at sample 512 of a block of 1024 silenced the block from
its start, and that an on and off within one block vanished. The switches were applied
when read, before rendering, and rendering used the block's last value.

0.1.10 adds `OutputGate` (`source/output_gate.h`). Before anything else of a block is read,
`Processor::ReadSwitches` gives it the block's points of both switches, and `Plan` cuts
the block into segments. In each segment the gain glides toward one target, one step per
sample (5 ms, as before), and the engine either renders or rests. The engine rests from
the sample after the gain reaches 0 until a point releases it. `Render` follows the
segments; the plan and the rendering use the same step function, so they agree to the
sample. At full gain the output is not touched, as before.

The same plan says where the engine rests and how many samples it renders before a host
sample (`EngineOffset`). Parameter points and notes are put on the engine's own time:

* a point or a release while the engine rests acts when it goes on;
* a note struck while the engine rests is dropped (as since 0.1.9, now at its sample);
* an event after a pause in the same block comes after the pause, at its distance from
  the release.

So the output is the same for any division of the host's blocks.

Limits: 64 points per switch and block; a further point takes the place of the last, so
the switch ends the block as the host left it. A block of 0 samples takes the switches'
last values.

Test (`TestSwitchesOnTheSample`, for Stop and for Bypass): a held note; the switch on at a
sample on no block or quantum boundary; a note struck 100 samples into the fade (played);
one struck while resting (dropped); a parameter point and a release while resting; the
switch off; an on and off 50 samples apart inside one block of 2048. The output is
compared bit for bit for blocks of 512, 2048, 1024, single samples around the switches,
the varying sizes of the block test, and 333/1000 alternating. Further:

* before the point the output equals a run without the switch;
* the engine rests until the release, and renders fewer samples;
* the note struck while resting changes nothing, the one struck while fading does;
* the release while resting acted (the pitch at the end is that of the note still held);
* the blip inside one block is heard.

With the points taken back to the block's start (the 0.1.9 behaviour), the test fails at
the first comparison of block divisions.

## MS-R36: the screen keyboard while resting

`PlayKeyboardNote` now drops a strike while the engine rests, as `ReadEvents` does for the
host's notes, and sends no MIDI out for it. Screen keys act at the block's start, so the
plan is asked for sample 0. Releases still go through.

Test (`TestScreenAndHostKeys`, second part): a screen key held from before, Stop, the gain
faded out; a screen key struck now sends no note-on to the host's MIDI output. Both are
released while resting; after Stop off the pitch is the base pitch: the struck key was
dropped, the held key's release acted.

With the check taken out, the test fails at the MIDI output.

### The same class: releases during a long pause

While the engine rests, the adapter is not called, so its note queue does not drain.
Strikes have been dropped there since 0.1.9, but every release still went into the queue,
also of notes that were never struck. A busy MIDI track during a long Stop could fill the
queue (1024 events); a full queue then pushes out its newest waiting event, which may be
the release of a note that does sound.

0.1.10 therefore keeps, per MIDI channel, which notes the host holds (see MS-R34), and a
release of a note that nobody holds is not passed on at all. The engine would have
ignored it anyway, so the sound is unchanged; the firmware comparison passes as before.
The same for the screen keyboard: a key dropped while resting, or struck before a
restart, sends no release to the engine.

Test (`TestLongPause`): a held note on channel 3; Stop; while resting, 1150 strikes and
releases on channel 4 and, in between, the held note's release; Stop off. The held note
is let go, and the adapter dropped no note event. With the rule taken out, 1100 releases
fill the queue and the adapter drops note events.

## MS-R34: the screen keyboard and host MIDI on one note

The screen keyboard plays on the engine's MIDI channel 1 (index 0), the same as host MIDI
on channel 1. The core keeps one entry per channel and note, so a release from either
source ended the note of both. A disconnect, which lets go of the screen's keys since
0.1.8, therefore ended a host note that was still held.

The processor now keeps, on the audio thread, which notes the screen holds and which the
host holds on that channel. The engine gets the note-off only when the last holder lets
go. Both sets are cleared with the engine at every restart (activation, project).
Strikes from either source go to the engine as before, so the sound while both play is
unchanged. Host MIDI on other channels is not affected; the core already keeps those
apart.

The core's own keyboard source (16) was not used: it belongs to the firmware's sixteen
hardware keys with fixed notes and a fixed velocity, and it would have changed the pitch
wheel's effect on screen notes.

Test (`TestScreenAndHostKeys`, first part): the host holds C5; the screen strikes and
lets go of it: the note stays. The screen holds C5 and the host lets go: the note stays;
the screen lets go: it ends. Codex' case: both hold C5, both connection points are
disconnected (the processor lets go of the screen's keys): the host's note sounds on
until the host lets go.

With the holder check taken out, the test fails at the first case.

## MS-R37: exception boundaries of the window

`FileDropTarget::onDrop` copied the stored verdict, path and reason, before its `try`.
It now moves it (moving strings allocates nothing), and resets the hover state before
anything that could throw. `onDragLeave` also forgets the stored verdict.

The class: every call from VSTGUI, the host or the system into the window's own code. A
helper `Guarded` (`source/guard.h`) runs a body and catches everything. It is used for:

* drawing (`MapView`, `KeyboardView`, `WindowShapeView`, `ToggleButton`: `draw` calls
  `DrawUnchecked`);
* mouse and wheel (`MapView`, `KeyboardView`, `ToggleButton`). The button's action (LOAD...,
  KEYS, LATCH, STOP, FREEZE, RESET, the octave keys) runs inside the boundary; if it
  fails, the button shows its old state again;
* `Editor::notify` (the timer: settling, the status and display poll), `valueChanged`,
  `onSize`, `getSize`, `checkSizeConstraint`, `setContentScaleFactor`;
* `Editor::close`: finishing open edits, letting go of held screen keys and of a START
  drag are each guarded on their own, and the frame is let go in any case;
* the file dialog's answer (making the path's string);
* `invalid()` inside the two `noexcept` functions of the map (`Show`, `SetDropState`).

Already guarded before: `Editor::attached` and `open`, the controller's calls into the
window, the drop target's callbacks, the controller's own host calls.

Test: three new calls in the countdown test (each allocation fails in turn): a drag that
enters and drops a WAV, which loads it through the controller as the window does; a drop
without an enter; the editor without a window answering sizes, a scale factor and a timer
tick (which asks the controller for status and display). With the copy put back, the
test fails with an exception leaving the drop.

Not testable without a window: the views' handlers need an initialised VSTGUI platform,
which the test programs do not have. They were checked in the code.

## Codex' further notes (no findings)

* The WAV probe while a drag hovers reads only the headers, once per drag, on the UI
  thread. A slow network path can still delay the window for that read; there is no time
  limit. Left as it is, and in the DAW test list.
* The window polls the status on every tick while no status has arrived yet; bounded
  work per tick, as Codex says.

## Build matrix

Run on 2026-11-18 on the uncommitted 0.1.10 state; logs in `out/messier-0.1.10/logs/`.

| Build | ctest | Validator (all four plug-ins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 16/16 | 47/0 |
| VS 2026 Debug | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | 16/16 | 47/0 |
| ASan (the five Messier test programs) | all exit 0, no report | – |

Bit-exact with the separately built firmware core as before (plug-in: 0 of 960,000
samples differ). The countdown tests run in Release builds only (README). The fixes of
MS-R34, MS-R35, MS-R36 and MS-R37 and the rule for releases during a pause were each
taken out once: the new tests then failed (MS-R35 at the block divisions, MS-R34 at the
shared note, MS-R36 at the MIDI output, MS-R37 with an exception leaving the drop, the
pause rule at the adapter's dropped note events).

## Not checked

* The window's handlers under failing allocations (needs a VSTGUI platform); the window
  in a host is part of the DAW test.
* A host that automates Stop or Bypass with more than 64 points per block.
* Real hosts that delay messages across a connection change (as in 0.1.7).

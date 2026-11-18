# Build report: Messier 0.1.11, the edge cases of Codex' check of 0.1.10, and a resize grip

Date: 2026-11-18. Built and tested by Claude (Opus 5.5) in the worktree `.work/messier/wt`
on the branch `messier-0.1.0`, on top of `9aa0a1a` (0.1.10, pushed), **uncommitted**.
Codex' check of 0.1.10 ([CODEX_REVIEW_0.1.10.md](CODEX_REVIEW_0.1.10.md)) released it for
the controlled DAW test and left two edge cases, MS-R38 and MS-R39. Both were traced in the
code and hold. The user asked for both to be fixed without another check, and for a resize
grip. The sound is unchanged; the bit-exact comparison with the firmware core passes as
before.

| Point | Severity | State | Where | Test |
| --- | --- | --- | --- | --- |
| MS-R38 More than 1,024 held notes let go during a pause can end a note still held | P3 | fixed | `adapter/stream_adapter.h` (`kNoteCapacity`, adapter 0.1.2) | `TestLongPause`, a second part with every note held |
| MS-R39 LOAD... stays blocked if the system cannot open the dialog | P2 | fixed | `source/dialog_start.h` (new), `source/editor.cpp` (`ChooseFile`) | `TestDialogStart` (new) |
| Resize grip in the lower right corner | wish | new | `source/widgets.*` (`ResizeGrip`), `source/editor.*` (`GripBegin`, `GripDrag`, `LayOut`) | `TestResizeGrip` (new); the drag in a host is the DAW test's |

## MS-R38: the note queue during a pause

While the engine rests, the adapter is not called and its note queue does not drain.
Since 0.1.10 strikes are dropped there and only releases of notes that are held are passed
on, at most one for each held note. So the queue can receive at most as many releases
during a pause as notes can be held at once: 16 channels of 128 (the screen keyboard shares
channel 1's entries). The queue held 1,024; Codex filled it with 1,152 releases, and the
overflow rule turned the last waiting event into "all notes off", which ended a note still
held.

The queue now holds twice that bound, `2 * 16 * 128` = 4,096 events (adapter 0.1.2). The
second half is room for events already waiting when the pause began. This is the second of
Codex' two suggestions; the first (collecting releases per channel and note and applying
them at the resume) would have needed one event per release at the resume as well, so the
bound would have been the same.

Test (`TestLongPause`, second part): every note of all 16 channels is held, the last struck
(C5 on channel 16) sounds. During Stop, all 2,047 others are let go. The adapter drops no
event, and after Stop off C5 still sounds. With the old capacity, the test fails at the
dropped events.

## MS-R39: the LOAD... dialog that does not open

`ChooseFile` set `dialog_open_` before `CNewFileSelector::run` and cleared it only in the
dialog's answer. If the system cannot create the dialog, VSTGUI returns false without an
answer, so LOAD... stayed blocked. VSTGUI's `run` also keeps one reference for the answer;
without an answer it was never given back, so the selector leaked.

* The selector is owned from its creation (`SharedPointer`), so a failure while setting it
  up frees it.
* `StartDialog` (`source/dialog_start.h`) starts the dialog: if it did not start and no
  answer came, or `run` threw before an answer, it clears the flag and gives the reference
  back. A dialog that answered is left alone. On Windows a cancelled dialog answers and
  then returns false, so the answer is what counts, not the return value.

Test (`TestDialogStart`): a dialog that cannot open leaves LOAD... usable and gives its
reference back once; a second attempt opens and is cancelled; a dialog still open (a
platform that answers later) stays open; a `run` that throws resets and gives back. With
the reset taken out, the test fails at the first case. The native dialog itself needs a
window; that part is in the DAW test list.

## The resize grip

The user found the window's edges hard to hit. The lower right corner now has a small
hatched grip, 18 by 18 units at 100 %, with the diagonal resize pointer. It sits on top of
everything, in the corner of the height the keyboard leaves, clear of the keyboard and its
octave keys.

Dragging the grip resizes as a corner drag of the window does: from the size at the press
and the distance the mouse moved, `ConstrainEditorSize` takes the allowed size nearest to
the asked one, with the aspect ratio and the zoom limits (50 to 200 %). The editor then asks
the host for that size (`Resize`); if the host refuses, the window stays as it is. The
size is saved with the song, as before.

The distance is measured in window pixels: the mouse position in the frame's units times
the frame's zoom. The resize changes the zoom while the drag goes on, so both are taken at
each move. The window's top left corner does not move, so the measure stays valid.

Test (`TestResizeGrip`): a press starts a drag, moves report the distance from the press, a
release ends it, and moves without a press do nothing. A corner drag of 40 by 25 pixels from
100 % gives a larger size with the window's aspect ratio. The drag in a host is in the DAW
test list.

## Build matrix

Run on 2026-11-18 on the uncommitted 0.1.11 state; logs in `out/messier-0.1.11/logs/`.

| Build | ctest | Validator (all four plug-ins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 16/16 | 47/0 |
| VS 2026 Debug | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | 16/16 | 47/0 |
| ASan (the five Messier test programs) | all exit 0, no report | – |

Bit-exact with the separately built firmware core as before (plug-in: 0 of 960,000 samples
differ). The fixes of MS-R38 and MS-R39 were each taken out once: the new tests then
failed. No Codex check was asked for this version.

## Not checked

* The grip and the native file dialog in a host (DAW test).
* A host that delivers more than 2,048 note events in one block just before a pause.

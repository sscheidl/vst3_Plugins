# Build report: Messier 0.1.9, from the user's DAW test of 0.1.8

Date: 2026-11-18. Built and tested by Claude (Opus 5.5) in the worktree `.work/messier/wt`
on the branch `messier-0.1.0`, on top of `46c3998`, together with 0.1.7 and 0.1.8,
**uncommitted**. 0.1.8 was installed for the user's DAW test; it "runs", with one bug
and three wishes. The sound while playing is unchanged; the bit-exact comparison with
the firmware core passes as before.

| Point | What changed | Where | Test |
| --- | --- | --- | --- |
| The file line did not follow a drop | after a load from the window the open window asks for the status every tick until a status shows that request finished (at most about 12 s), and every 16 ticks (about 0.5 s) anyway; LOAD... opens in the playing file's folder | `source/controller.*` (`LoadFile`, poll), `source/editor.cpp` (`ChooseFile`) | none automatic (the poll lives in the window); DAW test list |
| Refuse unplayable WAVs while they hover | `ProbeWavFile`: the reader's header checks without the audio data; `JudgeDrop` takes the first WAV that passes, else the first refusal's reason; the map shows "Can't load: ..." in the warning colour | `source/wav_reader.*`, `source/file_drop.*`, `source/map_view.*`, `source/editor.cpp` | `TestFileDrop` (rewritten, real files), `TestStreamErrors` (probe) |
| A STOP button | parameter 131, a toggle, not saved; the output fades out in 5 ms, then the engine is not rendered (its time stands); released, it fades in and goes on | `source/parameters.h`, `source/processor.*` (`Render`, `Restart`, `QueueParameter`), `source/controller.cpp`, `source/editor.*` | `TestStopAndBypass` (new) |
| Studio One's bypass had no effect | parameter 130 with `kIsBypass`, the same fade and rest; saved in the state (version 2, a 17th value; version 1 is still read, without bypass) | as above, `source/state_io.h` | `TestStopAndBypass`, the damaged-state test now uses version 3 as the unknown one |
| Mute had no effect | not a defect: Messier plays without notes, so muting Studio One's instrument track mutes only its notes; the mixer channel's mute silences the sound | none | none |

## Notes on the design

* **The bug's cause** could not be reproduced without Studio One. The window asked for
  the status only while it knew of a running load. If the first reply ("loading") is
  lost or comes too late, the file line never follows; the parameter 199 path marks the
  project, but it calls `RequestFileStatus` only when the host passes the output
  parameter on. Asking until the request is seen finished, and every half second
  anyway, removes the dependence on any single reply.
* **While resting** (stopped or bypassed, faded out) the adapter is not called, so its
  time stands. Parameter changes still queue, and act at the first step when the sound
  goes on. A control's points in the same quantum merge, so its queue cannot overflow.
  Note-ons are dropped while resting, so a long pause cannot fill the note queue and no
  note waits for the end of the pause. Note-offs still queue.
* A new start (activation, project) takes Bypass and Stop from the values without a
  fade. A project always opens playing; Stop is reset by `ParameterState::Load` and in
  the controller's `setComponentState`.
* The fade step follows the host rate (`SetFadeRate` in `setupProcessing`). At full
  gain the output is not touched, so the firmware comparison stays bit-exact.

## Build matrix

Run on 2026-11-18 on the uncommitted 0.1.9 state; logs in `out/messier-0.1.9/logs/`.

| Build | ctest | Validator (all four plug-ins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 16/16 | 47/0 |
| VS 2026 Debug | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | 16/16 | 47/0 |
| ASan (the five Messier test programs) | all exit 0, no report | – |

Bit-exact with the separately built firmware core, given the same pot positions
(plug-in: 0 of 960,000 samples differ).

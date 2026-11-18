# Build report: Messier 0.1.7, the findings of Codex' check of 0.1.6

Date: 2026-11-18. Built and tested by Claude (Opus 5.5) in the worktree `.work/messier/wt`
on the branch `messier-0.1.0`, on top of `46c3998` (0.1.6, pushed), **uncommitted**.
Codex' check of `46c3998`: [CODEX_REVIEW_0.1.6.md](CODEX_REVIEW_0.1.6.md), no release for
the DAW test. MS-R22 to MS-R24 are fixed, MS-R20, MS-R21 and MS-R25 are open in parts,
and there are four new findings, MS-R26 to MS-R29. All four were traced in the code and
hold. The sound is unchanged; the bit-exact comparison with the firmware core passes as
before.

Three of the four (MS-R26 to MS-R28) are the same class as the findings before them:
messages a host delivers late, across a change of connection. 0.1.7 does not add another
special case. It changes the rules so that a late message can no longer do harm:

* Nothing is subtracted, so a late report cannot hide a real one.
* A processor refuses requests of older connections.

| Finding | Severity | State | Where | Test |
| --- | --- | --- | --- | --- |
| MS-R26 A late parameter 199 of the old connection hides a real change | P2 | fixed | `source/controller.*` (`MarkChanged`, `CountFromStatus`), `source/parameters.h` (`SampleChangesStep`), `source/processor.cpp` | `TestNewProcessorSameController`, rewritten around Codex' case |
| MS-R27 A late first status marks the same load again | P3 | by contract: allowed and documented | `source/controller.*`, README | `TestNewProcessorSameController`, `TestFileChangeMarksProject` |
| MS-R28 A late load request of the old connection replaces the newer file | P2 | fixed | `source/processor.*` (`NotifyUnchecked`, stamps), `source/controller.cpp` (shared generation counter) | `TestLateRequest` (new) |
| MS-R29 The stream's exception mask is not restored on two error paths | P3 | fixed | `source/wav_reader.cpp` (`RethrowStreamErrors`) | `TestStreamErrors`, two new cases |

## MS-R26, MS-R27: marking the project as changed

Codex showed that subtracting reports from each other is unsafe. Parameter 199 carries no
identity of its sender, so a late value of an old processor can be counted against a real
load of the new one. 0.1.7 therefore drops all deduplication. The new contract, also in
the README and the planning document:

* every change of parameter 199 that the host passes on gives one `setDirty(true)`;
* every status whose count of files from the window is higher than the last status of
  the same processor gives one. For a processor not seen before, any count above zero
  does;
* nothing else gives one: polls, failed loads, loaded projects and repeated values do
  not.

A file can so be marked twice, once on each way. For a host, `setDirty(true)` only marks
the document as changed, so a second call changes nothing. Missing a mark could lose
work; marking twice cannot. This is also the answer to MS-R27: a late first status may
mark the same load again, and that is allowed.

Parameter 199 starts at 0 on every connect, and only a change of its value reaches the
controller. A late value of the old processor could therefore equal the new processor's
first value and hide it. That is Codex' case, where both counts are 1. To prevent it,
each processor adds its own offset to the count: `SampleChangesStep`, from its instance
number, never 0. Two processors show the same value only by chance, 1 in 65,535, and
even then the new processor's status still marks the load when the host passes it.

Test (`TestNewProcessorSameController`, rewritten): processor A loads one file. The host
holds back A's value of parameter 199 (count 1); A's status marks the load. The host
then delivers A's late value after the connect to B, before B has loaded anything. B
loads with no way back for its status, so parameter 199 alone must mark the project, and
it does. Further: B's late first status, a second status (no mark), a load after it, a
reconnect to B (no mark) with a load, and processor C without any way back.

Every load marks the project once or twice, and nothing else marks it. With the offset
taken out, the test fails at B's first load (no mark), as in Codex' report.

## MS-R28: late requests

Every controller now takes its generation from a counter shared by all controllers of
the process. A reconnect, and also a new controller instance, therefore always has a
higher generation than any connection before it.

The processor keeps the highest generation it has heard. It refuses any request with a
lower one before that request changes anything: load, reset, note, status and display
requests answer `kResultFalse` and do nothing. The one exception is the release of a
screen key: releasing is always safe, and refusing it could leave a note hanging.

Each reply carries the generation of the request it answers, no longer the newest heard.

Not covered, and documented: a controller in another process than its processor. The
counter is shared only within a process, and hosts that split controller and processor
into two processes are rare.

Test (`TestLateRequest`): a load request of the current connection is held back. The
controller reconnects and loads another file. The late request is then refused, and the
newer file stays the one that `getState` saves. A second controller instance takes a
higher generation. A late key strike of an older generation is refused, a late key
release goes through. With the refusal taken out, the test fails at the late request.

## MS-R29: the stream's exception mask

`RethrowStreamErrors` is now complete before anything can throw. Its constructor only
saves the mask; `Arm()` sets badbit and may throw, for example on a stream without a
buffer. The destructor sets the saved mask back on its own: `exceptions()` sets the mask
before it may throw, so the mask is restored even when the state check throws.

Test: a stream without a buffer, and a file cut short whose caller has set failbit and
eofbit in the mask. Both give an error, and both streams have their caller's mask back.

## Build matrix

Run on 2026-11-18 on the uncommitted 0.1.7 state; logs in `out/messier-0.1.7/logs/`.

| Build | ctest | Validator (all four plug-ins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 16/16 | 47/0 |
| VS 2026 Debug | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | 16/16 | 47/0 |
| ASan (the five Messier test programs) | all exit 0, no report | – |

Bit-exact with the separately built firmware core as before (plug-in: 0 of 960,000
samples differ). The countdown tests run in Release builds only (README). The fixes of
MS-R26 (the offset) and MS-R28 (the refusal) were each taken out once: the new test then
failed.

## Not checked

* Real hosts that delay or reorder messages across a connection change; the cases here
  are built by the tests.
* A controller in another process than its processor (see MS-R28).
* The window (MS-R24 of 0.1.6) in a host; that is part of the DAW test.

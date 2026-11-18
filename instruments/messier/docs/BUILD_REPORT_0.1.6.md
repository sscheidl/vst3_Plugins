# Build report: Messier 0.1.6, the findings of Codex' check of 0.1.5

Date: 2026-11-18. Built and tested by Claude (Opus 5.5) in the worktree `.work/messier/wt`
on the branch `messier-0.1.0`, on top of `1d1632b` (0.1.5, pushed); committed and pushed on the user's word after the matrix.
Codex' check of `1d1632b`, which covered the display data path of 0.1.4 as well:
[CODEX_REVIEW_0.1.5.md](CODEX_REVIEW_0.1.5.md), no release for the DAW test, MS-R16
fixed, MS-R17 to MS-R19 open in parts, six new findings MS-R20 to MS-R25. All six were
traced in the code and hold. The sound is unchanged; the bit-exact comparison with the
firmware core passes as before.

| Finding | Severity | State | Where | Test |
| --- | --- | --- | --- | --- |
| MS-R20 Dirty count before the first status depends on the old counter | P2 | fixed | `source/controller.*` (`connect`, `CountFromParameter`, `CountFromStatus`, `MarkChanged`) | `TestNewProcessorSameController`, rewritten: four orders |
| MS-R21 A late reply can fix another processor's waveform | P2 | fixed | `source/parameters.h` (`kAttrGeneration`, `kAttrReply`), `source/controller.*` (`NewMessage`, `ReadDisplay`, status, `ShownWave`), `source/processor.*` (stamps) | `TestLateDisplayReply` (new) |
| MS-R22 A change of sample rate drops a waiting reset | P3 | fixed | `source/processor.cpp` (`PrepareAdapter`) | `TestResetButtonThroughHost`, new section at 96 and 48 kHz |
| MS-R23 Receiving a display reply can terminate | P1 | fixed | `source/controller.cpp` (`ReadDisplay`), `source/processor.cpp` (`notify`) | `TestNoExceptionLeavesHostCalls`, two new calls |
| MS-R24 Opening the window has an open exception boundary | P2 | fixed, not run | `source/editor.*` (`attached`, `open`, `Add`, `MakeLabel`) | none (needs a window; DAW test list) |
| MS-R25 File-system and stream errors partly unspecific | P3 | fixed where the library gives a reason; the rest documented | `source/wav_reader.cpp` | `TestStreamErrors` (new, test_loader) |

State of the earlier findings: MS-R17 fixed (the open order was MS-R20), MS-R18 fixed
(MS-R22), MS-R19 fixed for Messier's own code (MS-R23, MS-R24; the SDK's
`GetPluginFactory` stays as documented in 0.1.5).

## MS-R20: counting after a connect

Codex is right that parameter 199 alone cannot tell an old processor's value from the same
value of a new one. 0.1.6 therefore no longer tries to. Between a `connect` and the first
status of that connection, every change of parameter 199 marks the project: the first as
one load, later ones by their step. Every change means a file was loaded, whichever
processor sent it. `marked_since_connect_` counts what was marked. The first status names
the processor. If it is the same processor as before (a reconnect), the files it loaded
since its last counted value count. If it is another one, all of its files count. Of
these, only those not yet marked are added.

After that first status, the confirmed rules hold. Parameter 199 counts steps forward
from the counted value, and a step back is an old value. The status counts its full
32-bit count, and a step back is an old reply.

The contract on `setDirty`, now stated in the README and the planning document: at least
one call for every change the host passes on, one per file up to 64 for one report. One
call is enough for a host to know.

Test (`TestNewProcessorSameController`, rewritten), one controller:

1. Processor A, status first, one load.
2. Processor B, parameter 199 first: its first value, 1, equals A's last (Codex' case).
   Two loads, then its late status, then a load after it.
3. A reconnect to B, a load, and its status.
4. Processor C without any way back, two loads.

Each load gives exactly one `setDirty`, and no status adds one. The old waveform is gone
after each connect. With the new rule before the first status taken out (parameter 199
then counts only against the old counter), the test fails at B's first load (1 call
where 2 were expected in total), as in Codex' report.

## MS-R21: replies across connections

Every message of the controller carries its connection generation (`kAttrGeneration`),
which grows with every `connect`. This covers the status request, display request, load,
note and reset messages. The processor keeps the generation it heard last and stamps it
on its status and display replies. It also numbers its display replies (`kAttrReply`).

The controller takes a reply only if it belongs to the current generation. Within that
generation, it also needs a newer reply number (display) or a status serial no older than
the newest taken. An old processor never hears the new generation, so nothing it sends
after the connect is taken.

A display reply is read completely before frame and waveform are taken together. The
cached waveform is shown only if its file number and length match the frame.

Test (`TestLateDisplayReply`): after a connect, a display reply of the previous generation
with the same file number and length but another waveform is refused. So is a reply of the
current generation with an older number. After five more ticks the waveform is still that
of the file the core reads. With the generation check removed, the test fails at the late
reply.

## MS-R22: a waiting reset and a change of rate

`Processor::PrepareAdapter` wraps `StreamAdapter::Prepare`. If a RESET press waits in the
adapter and the preparation succeeds, it puts the press once on the new start. If the
preparation fails, the old queues stay with the press in them, and nothing is added. It is
used by `initialize`, `setupProcessing` and the rate change in `setActive`. `Restart`
keeps the press through the activation, as since 0.1.5.

Test: an automated press in a one-sample block in the middle of a quantum, so that it
waits in the adapter. Then `setupProcessing` to 96 kHz, an activation and 1024 samples:
one reset. The same back to 48 kHz. With the re-add removed, the test fails.

## MS-R23: receiving messages

`Controller::ReadDisplay` now catches everything, including `getAttributes`, which a host
may implement by making the list on first access (as the SDK's `HostMessage` does). It
answers `kOutOfMemory` and keeps the last valid display. `Processor::notify` is guarded as
a whole in the same way. Before, the note message read its attributes outside any guard.

Test: two new calls in the countdown test, a display reply and a note message whose
attribute list does not exist yet. Each fails at each of its 2 allocations without an
exception.

## MS-R24: building the window

`Editor::open` builds inside a guard, and every view is handed to the frame right after
it is made (`Add`, and `MakeLabel` lets its label go if setting it up fails). On an
exception, `close()` releases the frame with what it owns, and `open` answers false.

`Editor::attached` wraps the SDK's `VSTGUIEditor::attached`. That method attaches the
editor even when `open` fails, so if the window was not built or something after it threw,
`attached` calls the SDK's `removed()`. That stops the timer, closes the editor and
detaches it from the controller. `attached` then answers `kResultFalse` or `kOutOfMemory`
instead of leaving an empty window.

No automatic test: VSTGUI needs a real parent window and an initialised platform, which
the test programs do not have. The DAW test list now asks to open and close the window
several times.

## MS-R25: file errors

The reader uses `std::filesystem::status`:

* "not found" is a missing file;
* `not_enough_memory` is a want of memory;
* any other error is a read error ("cannot check the file");
* a directory is "not a file".

`file_size` is classified the same way. While decoding, badbit joins the stream's
exception mask (`RethrowStreamErrors`, the caller's mask is restored afterwards). An
exception inside the stream, such as `std::bad_alloc` in its buffer, then reaches the
reader's guard as itself instead of as a short read.

Left as it is, and documented in the README: `std::ifstream` gives no reason when a file
cannot be opened. That stays "could not be read (cannot open the file)", also if memory
was the cause.

Test (`TestStreamErrors`): a stream whose buffer throws `std::bad_alloc` in the audio data
gives `kOutOfMemory`, and the caller's exception mask is unchanged. The same stream
without the throw decodes normally. A directory and a path under a missing directory are
"missing".

## Build matrix

Run on 2026-11-18 on the 0.1.6 state before its commit (the same files); logs in `out/messier-0.1.6/logs/`.

| Build | ctest | Validator (all four plug-ins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 16/16 | 47/0 |
| VS 2026 Debug | 16/16 | 47/0 |
| VS 2022 (MSVC 19.44) Release | 16/16 | 47/0 |
| ASan (the five Messier test programs) | all exit 0, no report | – |

Bit-exact with the separately built firmware core as before (plug-in: 0 of 960,000
samples differ). Reading the display costs 25 ns per block. The countdown tests run in
Release builds only (README). In addition, the fixes of MS-R20, MS-R21 and MS-R22 were
each taken out once: the new test then failed.

## Not checked

* The window itself (MS-R24) in a host; opening it under failing allocations.
* Hosts that reorder or delay messages: the generation and reply numbers were tested with
  messages built by the test, not with a host that delays them.
* A host that runs controller and processor in two processes.

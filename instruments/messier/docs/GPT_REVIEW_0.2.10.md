# Messier 0.2.10 — ChatGPT review, final check before 1.0 beta 1

Brief: [GPT_PRUEFAUFTRAG_0.2.10.md](GPT_PRUEFAUFTRAG_0.2.10.md), commit `9361014`, static
only. Claude Opus checked each finding in the code.

## Part A: the four fixes of 0.2.10

GPT's verdict: no P1 or P2 confirmed; GC.2, GA.2 OK; GD.1 OK with one special case
(HA.1); GA.1 OK under VSTGUI's ownership rules; no new allocation or lock on the audio
thread.

| ID | GPT | Finding | Verdict |
| --- | --- | --- | --- |
| HA.1 | P2, suspected | If the host calls `Controller::setState` re-entrantly while `ShareSettings` runs (inside a `resizeView` of window B), `setState` stores the new settings in `editor_`, but `ShareSettings` returns at once; the round goes on with the old settings, and some windows show them. | **Not reachable in practice, P3, not changed.** A host calls `setState` to load a project or preset, from its own main-thread code; `resizeView` answers with `onSize` and does not load a project in between. No host is known to do so. It is not a regression either: in 0.2.9 such a call would have shared the new settings and the outer round would then have written the old ones over them, the same end state. The controller stores the new settings, so the project saves them, and the next change or reopening of a window shows them. A fix (a pending flag that only `setState` sets, with a second round) would add code before the beta for a case no host produces. |

Checked as well:
* The 16-quantum bound of `FinishSwapSilently` (GPT's remark): if the core acknowledged
  only in the last of the 16 quanta, the swap would stay open, and `Render` would finish
  it at the start of the first quantum after STOP is released. The fade-out takes 4
  quanta, so the bound is never reached with this core. The old file is not heard
  then either, since that quantum already plays the new one.
* GA.1 rests on `CView::removed` clearing the menu's frame and on `Editor::close` calling
  `frame->removeAll(true)`. Both were checked in VSTGUI of SDK 3.8.0 for round 2 of the
  0.2.9 review.

## Part B: the audio thread and the project state

GPT found no P1 or P2. It confirmed:
* `ReadSavedState` takes only version 1 or the current version, checks each value
  (finite, 0 to 1), bounds the path and checks its UTF-8, and assigns only at the end.
* `process()` handles blocks of 0 samples, missing buses and null channel pointers, and
  takes no lock and allocates nothing on the path it could see.
* `TakeHandovers` pairs a project's file and values by the project number.
* `TakeWindowFile` and `RetireReleased` free nothing on the audio thread.

GPT named three things it could not see in its three files. They are covered elsewhere:

| Open for GPT | Where it is covered |
| --- | --- |
| How `ParameterState` synchronises (`Current`, `Load`, `TakeLoaded`, `Write`, `Adopt`) | `parameter_state.h`, taken over from Kekulé 0.3.1: the audio side uses atomics and `LatestValue` only, the other side a mutex the audio thread never takes. The plug-in tests check automation against loaded states across blocks. |
| Lifetime of a file buffer the engine still reads | Round 2 of the 0.2.9 review (C.1: `SwapFile`, `Release`, `RetireReleased`), `TestSwap` and the processor's swap and project tests; every build runs under ASan without reports. |
| Default values when a version 1 project is read | `TestStopAndBypass` reads a version 1 state (16 values, without Bypass); the controller's tests read its states 1 to 7. |

GPT's remark on the `int32` counter in the render loop (`done += kChunk`) needs a block
close to 2^31 samples. By the VST3 interface a host sends no more than the
`maxSamplesPerBlock` it announced in `setupProcessing`, and none announces such a size.
Not changed.

## Part C: the window's life and the file loader

GPT found no P1 or P2 and named two suspected P2 cases. Both need a host that releases
its last reference to the view from inside a call into that same view.

| ID | GPT | Finding | Verdict |
| --- | --- | --- | --- |
| HC.1 | P2, suspected | `Editor::OpenUnchecked` calls `Resize()`, which may call the host's `resizeView`; if the host destroyed the editor in it, `AnimateLater()` would run on a freed object. | **Not reachable from a correct host, not changed.** The view is reference counted; the host holds it while it calls `attached`. Releasing the last reference inside its own `resizeView` while `attached` is still running would break the SDK's own `VSTGUIEditor::attached` as well, which goes on after `open()` returns. That would be a host bug that hits every VSTGUI plug-in. |
| HC.2 | P2, suspected | `Editor::notify` calls `callbacks_.settled()` (the controller's `setDirty(true)`) and then `Animate()`; if the host destroyed the editor in `setDirty`, `Animate()` would run on a freed object. | **Not reachable from a correct host, not changed.** `setDirty` only marks the project as changed; destroying the view inside it is the same host bug as in HC.1. Timers after destruction cannot come: `~VSTGUIEditor` releases the timer, and `removed` stops it first (`vstguieditor.cpp`, SDK 3.8.0). |

GPT's open questions, answered from the SDK:
* The controller cannot go while a window is open: `EditorView` holds it as
  `IPtr<EditController> controller` (`vsteditcontroller.h`).
* The loader's `worker_.join()`: the read gives up per chunk header and per MiB
  (`WavCancel`, since 0.2.9, tested by `TestCancelledRead`). A file-system call that blocks
  in the operating system cannot be interrupted; that was accepted in round 1 of the 0.2.9
  review.

## Outcome

Parts A, B and C: **no P1 or P2 confirmed.** The three suspected cases (HA.1, HC.1,
HC.2) need host behaviour no known host shows, or that would be a host bug; none is
changed. By the user's order, 1.0.0-beta.1 can now go ahead.

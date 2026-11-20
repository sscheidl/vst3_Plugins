# Messier 0.2.9 — ChatGPT review, round 1

On 2026-11-20 the user ran [GPT_PRUEFAUFTRAG_0.2.9.md](GPT_PRUEFAUFTRAG_0.2.9.md)
(six parts) in the ChatGPT chat. It was a static review of commit `50fe057`, with no
build and no tests. Claude Opus checked each finding in the code.

## Line numbers did not match

GPT read the right version: it quotes `Editor::Later` and
`LooperEngine::FinishSwapSilently`, which exist only in 0.2.9. Its line numbers drift
further from the file towards the end, though: in `editor.cpp` by up to 70 lines
(`PopUpMenu` at 950, GPT says 895; `close` at 1205, GPT says 1135). Its reader apparently
counts no blank lines. GPT therefore found most of the requested ranges "unsuitable" and
left most questions open: none of the six parts was completed.

**Lesson for the next brief:** name functions, not line ranges, and link whole files.

## Findings

| ID | GPT | Finding | Verdict |
| --- | --- | --- | --- |
| G1.1 | P2 | A host that loses a non-empty status text (info or detail) on the way goes unnoticed: a missing text reads as empty, and `HasItsTexts` checks neither | **Confirmed in principle, P3.** It needs a host that stores an attribute (`setBinary` returned ok) and then loses it. The effect is an empty format or error line; the file name and path are checked. Cheap fix: the processor sends a bit mask of the texts that are not empty, and the controller refuses a status in which a flagged text is missing. |
| G6.1 | P2 | An exception in the worker after a newer request came in drops that request and marks it failed | **Refuted.** In the `try` block of `FileLoader::Run` only `std::string path = wanted_` can throw, and it runs while the mutex is held, so no newer request can come in meanwhile. After `lock.unlock()` nothing throws: `ReadWavFile`, `MakeOverview` (wave_overview.cpp) and `Finish` are `noexcept` and handle lack of memory themselves. GPT had not seen `MakeOverview` and marked the precondition as unverified. |

Answered "in order" by GPT:
* 1c: a refused status does not change `last_status_serial_`.
* 4a: `FinishSwapSilently` is bounded to 16 quanta.
* 5b: `ForEachEditor` holds its own references.
* 6b: the atomics `newest_ticket_` and `stopping_`.
* 6c: a cancel check per chunk header and per MiB. A blocking file-system call itself
  cannot be interrupted, which is known and accepted.

## Still open

Not answered because of the line numbers:
* 2b and 2c: the 64 KiB limit in `ReadLimited`, `ParseHex`, `ApplyColourTable`;
* 3a to 3c: lifetimes after the dialog or menu;
* 4b and 4c: `SwapFile` replaced repeatedly; STOP released in the same block;
* 5a and 5c: re-entrancy of `ApplySettings`; the size from `getSize`;
* 6a: an overtaken result shown.

Point 3 matters most (C1, a possible crash).

# Round 2

Brief: [GPT_PRUEFAUFTRAG_0.2.9_B.md](GPT_PRUEFAUFTRAG_0.2.9_B.md), functions and whole
files at `50fe057`. Claude Opus checked each finding against the code and against VSTGUI
of SDK 3.8.0 (`external/vst3sdk/vstgui4`).

## Part A: dialog and menus after the window closed

GPT's conclusion: the regular C1 crash is not confirmed on Windows. What remains are
popup and dialog paths that fail or are interrupted.

| ID | GPT | Finding | Verdict |
| --- | --- | --- | --- |
| GA.1 | P2 | `take_out` in `PopUpMenu` calls `frame->isChild(menu, false)` on the raw `CFrame*` after `popup()` returned. If the host closed the window during the menu's loop, that frame may be freed. | **Real in the code, not reachable with SDK 3.8 on Windows: P3, hardened.** `take_out` acts only without an answer. `Win32OptionMenu::popup` calls its callback unconditionally after `TrackPopupMenu`, and the wrapper in `COptionMenu::popup(CFrame*, …)` calls ours before anything that could throw. The paths without an answer (no platform menu, `createMenu` failing) all lie before the nested loop, while the frame is still alive. **Fix (0.2.10):** `take_out` uses the menu's own frame, `menu->getFrame()`, never `frame`. `Editor::close` calls `frame->removeAll(true)`, and `CView::removed` clears the menu's frame, so after a close the menu has none. No test: popping up a menu needs a real window. |
| GA.2 | (not logged) | `StartDialog` gets `dialog_open_` by reference. If the editor is destroyed during `run()` and `run()` then returns without an answer, `open = false` writes into the dead editor. | **Real, P3, fixed.** On Windows, `VistaFileSelector::run` calls the answer after `Show` on every path. Without an answer it returns only when `CoCreateInstance` failed, before any loop. The one way in: an allocation fails between `Show` and the answer (`UTF8String` of the result) while the editor is gone. **Fix (0.2.10):** `StartDialog` takes `alive` and writes `open` only while `*alive`; `give_back` uses only locals of `ChooseFile`. `TestDialogStart` covers both cases (thrown, and not started), with the editor dying in `run()`. |

Answered "in order" by GPT, and confirmed:
* A.b: a task from `Later` checks `alive`, `frame` and `window_serial_` before it touches
  anything. After the nested loop that the task itself starts, `ChooseFile`,
  `ShowPresetMenu` and `ShowSizeMenu` touch nothing of the editor: the menu answers check
  `alive` themselves, and `StartDialog` now does too (GA.2).
* A.c: `ChooseFile` returns at once while `dialog_open_` is set. `StartDialog` sets it
  before `run()`, and both the answer and the failure path reset it.

## Part B: skin files

GPT found nothing, and the check confirms it:
* `ReadLimited` keeps `bytes.size() <= limit` and takes 65,536 bytes, refusing 65,537.
  `TestCustomSkin` checks both sizes.
* `ParseHex` takes only 7 or 9 characters, so its indices stay in range.
* `ApplyColourTable` is safe for lines without `=`, empty names or values, bad colours
  and unknown keys. It checks the length before `substr(3)` for a byte-order mark.
* `SkinPngSize` reads within bounds thanks to `size >= 33` and the signature check. It does
  not check the CRC, which is accepted: the dimensions are checked, and WIC decodes.

## Part C: a new file while STOP rests the output

| ID | GPT | Finding | Verdict |
| --- | --- | --- | --- |
| GC.2 | (detail) | In `FinishSwapSilently`, the quantum that makes the swap also renders the new file's first quantum into nothing. | **Confirmed, fixed (engine 0.1.8).** `Render` swaps at the start of a quantum and then processes it with the new file. Under STOP that quantum went into nothing, so on resume the new file started 64 samples late, with part of its fade-in gone. **Fix:** `FinishSwapSilently` makes the swap (`CompleteSwap`, split out of `Render`) as soon as the core acknowledges, without the quantum after it. **Test:** `TestSwap` in `test_engine.cpp` runs two engines with the same history. One swaps while playing, the other finishes the swap unheard; their next 100 quanta must match sample for sample. |

Answered "in order" by GPT, and confirmed:
* C.1: `SwapFile` releases the waiting file only when it is replaced and is not the
  current one; `RetireReleased` runs at the start and end of `process`, and at most one
  `TakeWindowFile` comes per block. No file is lost or released twice. GPT's caveat (four
  release slots without `TakeReleased`) does not apply to the processor, which takes them
  in every block.
* C.2: `FinishSwapSilently` runs before `ReadSwitches`, so the old file is never heard
  once STOP is released in the same block.
* C.c: no allocation and no lock; `TestSwap` and the processor tests count allocations.

## Part D: window settings across several windows

| ID | GPT | Finding | Verdict |
| --- | --- | --- | --- |
| GD.1 | P2 | A change goes through `callbacks.changed` to window B, whose `ApplySettings` calls `Resize`. If B's host answers with another size, `OnSizeUnchecked` computes another zoom and calls `UserChanged`, which sends the settings round again. | **Confirmed, P3, fixed.** Two hosts with different limits (screen size, a host's own minimum or maximum) make the windows resize each other in turn; the nesting can go on until the stack runs out. A single correction also sent the settings back into window A while A was still inside its own `onSize`. **Fix (0.2.10):** `Controller::ShareSettings` sends the settings round once. A window that reports a correction during that round has its settings kept (`editor_`) but not sent on. `setState` goes the same way. No test: it needs real windows and a host that corrects their size. |

Answered "in order" by GPT, and confirmed:
* D.2: `getSize` takes `callbacks.current` before it answers, and `OpenUnchecked` takes the
  same settings and then calls `Resize`.

## Part E: an overtaken file read

GPT found nothing:
* E.1: `FinishUnchecked` drops a result whose ticket is no longer `ticket_`, under
  `mutex_`.
* E.2: GPT withdrew G6.1 from round 1 as a false positive; it matches the refutation above.

## Outcome

Of round 2, GA.1, GA.2, GC.2 and GD.1 hold up, each P3; all four are fixed in 0.2.10. The
regular C1 crash (the window closed during a menu or the file dialog) is not confirmed for
Windows. Still open from round 1: G1.1 (a lost non-empty status text), P3, left for a later
version.

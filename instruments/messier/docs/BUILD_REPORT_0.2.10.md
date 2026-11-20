# Messier 0.2.10 build report

Base: 0.2.9, commit `50fe057`. The order of 2026-11-20: verify the ChatGPT review of 0.2.9
(two rounds, [GPT_REVIEW_0.2.9.md](GPT_REVIEW_0.2.9.md)) and fix what holds up. Four
findings hold up, each P3. The sound of a playing file, the processor's state and the
firmware comparison are unchanged; the engine is 0.1.8.

## The fixes

| ID | Fix | Where |
| --- | --- | --- |
| GC.2 | `FinishSwapSilently` makes the swap (`CompleteSwap`, split out of `Render`) as soon as the core acknowledges, without the quantum after it. On resume after STOP, the new file starts with its first quantum, from START, as after a swap while playing. Before, that quantum went into nothing: a 64-sample late start with part of the fade-in gone. | `engine/looper_engine.*` |
| GD.1 | `Controller::ShareSettings` sends the window settings round the open windows once. A window whose host corrects its size during the round keeps that size; its settings are kept (`editor_`) but not sent on. `setState` takes the same path. | `source/controller.*` |
| GA.1 | `PopUpMenu` takes the menu out of its own frame (`menu->getFrame()`), never through the `CFrame*` it was given, which the host may have closed during the menu. Not reachable with SDK 3.8 on Windows (the menu always answers after its loop); hardening. | `source/editor.cpp` |
| GA.2 | `StartDialog` takes `alive` and writes `dialog_open_` only while the editor lives. Reachable only if an allocation fails after the file dialog closed, with the editor destroyed meanwhile. | `source/dialog_start.h`, `source/editor.cpp` |

Not changed: G1.1 (a host that loses a non-empty status text), P3, left for a later
version; the fix (a bit mask of the texts sent) changes the message format.

## Tests

Each fix with a test has been mutation-checked: undone once, its test failed, then restored.

| Fix undone | Test that failed |
| --- | --- |
| GC.2 | `TestSwap` (engine): the 100 quanta after an unheard swap differ from those after a swap while playing |
| GA.2, thrown path | `TestDialogStart`: `open` written after the editor died in `run()` |
| GA.2, not started | `TestDialogStart`: the same without an exception |

No automated test: GA.1 and GD.1. They need a real window and a host that closes it during
a menu or corrects its size. They are in the DAW test list, section 0.2.10.

| Build | ctest (Messier tests) | Validator (all four plug-ins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 6/6 | 47/0 each |
| VS 2026 Debug | 6/6 | |
| VS 2022 (MSVC 19.44) Release | 6/6 | 47/0 each |
| VS 2026 Release, AddressSanitizer | 6/6, no reports | |

Firmware comparison (`messier.engine`): five runs of 12 s each, among them a swap to a mono
44.1 kHz file while playing, 0 of 1,152,000 samples differ in each. The swap while
playing goes the same way as before; only the unheard swap changed. Logs in
`out/messier-0.2.10/logs/`.

## Open

* The user's DAW test, sections 0.2.9 and 0.2.10 of `DAW_TEST_0.1.0.md`.
* Codex next week: the Fable audit list, the fixes of 0.2.9 and 0.2.10, and the open
  [CODEX_PRUEFAUFTRAG_0.2.8.md](CODEX_PRUEFAUFTRAG_0.2.8.md) and
  [CODEX_PRUEFAUFTRAG_0.2.7.md](CODEX_PRUEFAUFTRAG_0.2.7.md).

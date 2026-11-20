# Messier 0.2.9 build report

Base: 0.2.8, commit `ba3a71d`. The user's order of 2026-11-20: fix the findings of the
first full audit "as recommended" ([FABLE_AUDIT_0.2.8.md](FABLE_AUDIT_0.2.8.md)). Five Fable
reviewers wrote that audit while Codex was away. The sound of a playing file, the processor's
state and the firmware comparison are unchanged; the engine is 0.1.7.

## The fixes

| ID | Fix | Where |
| --- | --- | --- |
| C1 | LOAD..., the size and the preset menu run from the main queue after the click (`Editor::Later`), with the editor/window check `AnimateLater` uses. No button and no VSTGUI event dispatch is on the stack while the dialog or menu runs its own message loop. | `source/editor.cpp` |
| C2 | `ToggleButton` ignores the follow-up mouse events of its click, so the window gives the mouse capture back. | `source/widgets.cpp` |
| F1 | `HasItsTexts` asks for the requested file only while loading. A refused project names its file as the failed request where memory allows. | `source/file_loader.h`, `.cpp` |
| F2 | While the output rests, a running swap finishes at once: `LooperEngine::FinishSwapSilently` renders the old file's fade-out (at most a few quanta) into nothing. | `engine/looper_engine.*`, `source/processor.cpp` |
| F3 | A change in one window goes to the other registered windows. A view without a window takes the controller's settings in `getSize` and `open` (`EditorCallbacks::current`). | `source/controller.cpp`, `source/editor.*` |
| E3 | A project taken over clears a failed window request. | `source/file_loader.cpp` |
| E5, C10 | `KeyboardView::ForgetHeld` after a project load or a new connection; a latched key is let go when KEYS hides the keyboard; `close()` forgets LATCH. | `source/widgets.*`, `source/editor.cpp`, `source/controller.cpp` |
| C3 | `Editor::beginEdit`/`endEdit` pass on only tags ≥ 0. | `source/editor.cpp` |
| C5 | `LayOut` takes the focus from a small knob it hides. | `source/editor.cpp` |
| C6 | HUE and WAVE treat VSTGUI's default-value gesture (Ctrl+click) like the double-click (`dispatchEvent`). | `source/editor.cpp` |
| C7 | A custom PNG is decoded once in full (`lockPixels`) before it is taken. | `source/gui_skin.cpp` |
| C8 | `negative`, `wave`, `wave_outside`, `loop_edge` documented as without effect. | guide text, README |
| C9 | `ApplyColourTable` skips a UTF-8 byte-order mark. | `source/gui_skin.cpp` |
| C11 | The catalogue number takes the mouse and lies over the file zone. | `source/editor.cpp` |
| F6 | `ReadWavFile` takes a `WavCancel`, asked once per chunk header and once per MiB. The loader's worker gives up when a newer ticket or the end comes (`newest_ticket_`, `stopping_`). | `source/wav_reader.*`, `source/file_loader.*` |
| A3 | The processor class is no longer `kDistributable`. | `source/factory.cpp` |
| D3 | "0x exactly" reworded; the creep is described. | README, `parameters.h` |

Not changed, as recommended: B2 (needs host calls on two threads within microseconds),
and D2 (the firmware core's own behaviour; the user's decision is open).

**C7 is only partly fixed.** A PNG whose pixel data WIC cannot decode at all (tested:
zeroed data) now falls back to the built-in bitmap. A cut-off or scrambled file is decoded
by WIC without an error, as far as it goes, and draws partly. That is the user's own file,
shown as far as it can be, not an empty window.

## Tests

Each fix with a test has been mutation-checked: undone once, its test failed, then restored.

| Fix undone | Test that failed |
| --- | --- |
| F1 rule | project without memory: a nameless failed request refused |
| F1 project path as request | project without memory: request is not the project's file |
| E3 | project without memory: the failed request stays after the next project |
| F2 | `TestSwapWhileResting`: the swap still waits |
| F3 push to other windows | controller state: the second window keeps Slate |
| F3 settings on `getSize` | controller state: the closed view keeps old settings |
| C3 | controller state: the menu's tag −1 reaches the handler |
| C7 | `TestCustomSkin`: zeroed pixel data taken |
| C9 | GUI presets: the first line after a BOM lost |
| F6 chunk check | `TestCancelledRead`: the read goes on |
| F6 piece check | `TestCancelledRead`: the read goes on |

No automated test: C1, C2, C5, C6, C10, C11 and E5. They need a real window, which the
test program does not open; they are in the DAW test list, section 0.2.9. A3 and the docs
(C8, D3) are checked by reading.

| Build | ctest (Messier tests) | Validator (all four plug-ins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 6/6 | 47/0 each |
| VS 2026 Debug | 6/6 | |
| VS 2022 (MSVC 19.44) Release | 6/6 | |
| VS 2026 Release, AddressSanitizer | 6/6, no reports | |

Firmware comparison: 0 of 960,000 samples differ, 0 allocations in process. Logs in `out/messier-0.2.9/logs/`.

## Open

* The user's DAW test, section 0.2.9 of `DAW_TEST_0.1.0.md`; C1 matters most.
* D2: decided after the build. The user keeps the firmware's behaviour (2026-11-20), as
  the original module behaves the same; the comment in `engine/looper_engine.h` now says so.
* Codex next week: the audit list and these fixes, together with the open
  [CODEX_PRUEFAUFTRAG_0.2.8.md](CODEX_PRUEFAUFTRAG_0.2.8.md) and
  [CODEX_PRUEFAUFTRAG_0.2.7.md](CODEX_PRUEFAUFTRAG_0.2.7.md).

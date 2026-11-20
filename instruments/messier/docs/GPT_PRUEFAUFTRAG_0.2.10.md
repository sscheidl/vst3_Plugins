# ChatGPT review brief: Messier 0.2.10 (last check before 1.0 beta 1)

The last static check before the closed beta 1.0.0-beta.1. It covers the fixes of 0.2.10
([GPT_REVIEW_0.2.9.md](GPT_REVIEW_0.2.9.md), rounds 1 and 2) and a last look for P1/P2
risks on the paths a tester will use. Paste **one part per chat**, A first, and give the
answers to Claude. The beta goes ahead only if no P1 or P2 is confirmed.

Every link points to the fixed commit `9361014` of the public repository. The diff of
0.2.10 against 0.2.9:
<https://github.com/sscheidl/vst3_Plugins/commit/93610146769294641cd9229cfd641301c830d7c0.diff>

---

## Part A: the four fixes of 0.2.10

```text
You are reviewing code of the user's own audio plug-in (VST3, VSTGUI 4, C++20, Windows).
Read only; do not build or run anything. Commit 9361014 of the public repository.

The change to review (diff against the previous release):
https://github.com/sscheidl/vst3_Plugins/commit/93610146769294641cd9229cfd641301c830d7c0.diff

Whole files, for context:
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/93610146769294641cd9229cfd641301c830d7c0/instruments/messier/engine/looper_engine.cpp
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/93610146769294641cd9229cfd641301c830d7c0/instruments/messier/source/controller.cpp
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/93610146769294641cd9229cfd641301c830d7c0/instruments/messier/source/editor.cpp
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/93610146769294641cd9229cfd641301c830d7c0/instruments/messier/source/dialog_start.h

The fixes:
- GC.2, LooperEngine::FinishSwapSilently and the new CompleteSwap: while STOP rests the
  output, the swap is made as soon as the core acknowledges the mute, without rendering
  the new file's first quantum into nothing.
- GD.1, Controller::ShareSettings: the window settings go round the open windows once;
  a window that reports a corrected size during the round is stored (editor_) but not
  sent on. setState takes the same path.
- GA.1, PopUpMenu in editor.cpp: take_out uses menu->getFrame(), never the CFrame* given.
- GA.2, StartDialog: takes `alive` and writes `open` only while *alive.

Questions:
a) Does each fix do what it says, on every path through the function (early returns,
   exceptions, the 16-quantum bound)?
b) Can a fix break something that worked before? In particular: can a swap while
   playing now differ from before; can a window miss a settings change it should get
   (two windows, then a project load through setState); can sharing_settings_ stay true?
c) Anything allocating or locking on the audio thread in the engine change?

Rules: only these files; if you need another file, name it instead of guessing. Do not
cite line numbers: cite the function name and quote the code line. No style remarks.
For each question answer "OK" with one sentence of reasoning, or a finding:
HA.n · P1–P3 · title / evidence: function + quoted line / trigger / proven or suspected / fix in one sentence.
```

---

## Part B: the audio thread and the project state

```text
You are reviewing code of the user's own audio plug-in (VST3, C++20). Read only.
Files (commit 9361014, public repository):

https://raw.githubusercontent.com/sscheidl/vst3_Plugins/93610146769294641cd9229cfd641301c830d7c0/instruments/messier/source/processor.cpp
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/93610146769294641cd9229cfd641301c830d7c0/instruments/messier/source/processor.h
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/93610146769294641cd9229cfd641301c830d7c0/instruments/messier/source/state_io.h

Find and read in full: Processor::process, setState, getState, setActive,
setupProcessing, setBusArrangements, notify, TakeHandovers, TakeWindowFile,
RetireReleased; the state read and write functions in state_io.h (the format the
processor and the controller share).

This is a last check before a public beta. Look only for P1 and P2: a crash, a hang, a
data race on the audio thread, memory freed while still in use, a project that saves
or loads wrongly, or the audio thread allocating or taking a lock.

Questions:
a) Can process() crash, block or allocate, for any block size (0 included), any number
   of channels or buses the host may give, or with a null pointer the VST3 interface
   allows?
b) Can setState or getState lose or corrupt a project: a truncated or foreign stream,
   a stream from an older version, a host calling them while process() runs?
c) Can a file handed to the audio thread be freed while the engine still reads it?

Rules: only these files; name another file instead of guessing. Do not cite line
numbers: cite the function name and quote the code line. No style remarks. For each
question answer "OK" with one sentence of reasoning, or a finding:
HB.n · P1–P2 · title / evidence: function + quoted line / trigger / proven or suspected / fix in one sentence.
```

---

## Part C: the window's life and the file loader

```text
You are reviewing code of the user's own audio plug-in (VST3, VSTGUI 4, C++20, Windows).
Read only. Files (commit 9361014, public repository):

https://raw.githubusercontent.com/sscheidl/vst3_Plugins/93610146769294641cd9229cfd641301c830d7c0/instruments/messier/source/controller.cpp
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/93610146769294641cd9229cfd641301c830d7c0/instruments/messier/source/editor.cpp
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/93610146769294641cd9229cfd641301c830d7c0/instruments/messier/source/file_loader.cpp
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/93610146769294641cd9229cfd641301c830d7c0/instruments/messier/source/file_loader.h

Find and read in full: in controller.cpp Controller::initialize, notify, createView,
MakeEditor, editorAttached, editorRemoved, editorDestroyed, ForEachEditor; in editor.cpp
Editor::OpenUnchecked, Editor::close, Editor::~Editor, Editor::notify (the timer);
in file_loader.cpp the constructor, the destructor, Run, Request, LoadProject.

This is a last check before a public beta. Look only for P1 and P2: a crash, a hang,
memory freed while still in use, a thread that outlives what it uses.

Questions:
a) The host may open and close the window many times, destroy the view without
   closing it, or release the controller while a window is open. Can any of these
   reach freed memory or a dangling editor pointer?
b) Can the timer (Editor::notify) or a message from the processor (Controller::notify)
   arrive after the editor or controller is gone, and touch it?
c) Can the loader's worker thread outlive the loader, hang the plug-in's removal, or
   hand a file to a processor that no longer exists?

Rules: only these files; name another file instead of guessing. Do not cite line
numbers: cite the function name and quote the code line. No style remarks. For each
question answer "OK" with one sentence of reasoning, or a finding:
HC.n · P1–P2 · title / evidence: function + quoted line / trigger / proven or suspected / fix in one sentence.
```

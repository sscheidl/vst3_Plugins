# ChatGPT review brief, round 2: Messier 0.2.9

Round 1 ([GPT_REVIEW_0.2.9.md](GPT_REVIEW_0.2.9.md)) left most questions open: GPT's
reader numbers lines without the blank lines, so the line ranges of round 1 did not fit.
This round names **functions** and links **whole files**. Paste **one part per chat**,
most important first, and give the answers to Claude.

Every link points to the fixed commit `50fe057` of the public repository; the raw files
open without GitHub's page around them.

---

## Part A: file dialog and menus after the window closed (possible crash)

```text
You are reviewing code of the user's own audio plug-in (VST3, VSTGUI 4, C++20, Windows).
Read only; do not build or run anything. Files (commit 50fe057, public repository):

https://raw.githubusercontent.com/sscheidl/vst3_Plugins/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/editor.cpp
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/dialog_start.h
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/widgets.cpp

Find these functions by name and read them in full: in editor.cpp Editor::Later,
Editor::ChooseFile, the free function PopUpMenu, Editor::ShowPresetMenu,
Editor::ShowSizeMenu, Editor::close, Editor::~Editor, and the three header buttons in
Editor::OpenUnchecked that call Later (size, preset, LOAD...); StartDialog in
dialog_start.h; ToggleButton::onMouseDownEvent in widgets.cpp.

Background: the file dialog and the menus run their own nested message loop. The host may
close the plug-in window meanwhile (Editor::close releases the CFrame) or destroy the
editor. Since 0.2.9 they are started by Editor::Later from VSTGUI's main queue after the
click, guarded by alive_ (a weak_ptr) and window_serial_.

Questions:
a) After the dialog or menu returns, can any code touch freed memory: this, frame, the
   menu, the file selector, a button? Look especially at what PopUpMenu does with its
   CFrame* after popup() returns when the window was closed during the menu.
b) Can a task scheduled by Later run after the editor was destroyed?
c) Can two LOAD dialogs be open at the same time (double click, or LOAD while a dialog
   is starting)?

Rules: only these files; if you need another file, name it instead of guessing. Do not
cite line numbers: cite the function name and quote the code line. No style remarks.
For each question answer "OK" with one sentence of reasoning, or a finding:
GA.n · P1–P3 · title / evidence: function + quoted line / trigger / proven or suspected / fix in one sentence.
```

---

## Part B: skin files from an untrusted folder

```text
You are reviewing code of the user's own audio plug-in (VST3, VSTGUI 4, C++20, Windows).
Read only. File (commit 50fe057, public repository):

https://raw.githubusercontent.com/sscheidl/vst3_Plugins/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/gui_skin.cpp

Find and read in full: ReadLimited, BitmapFromFolders, ReadSkinTable, SkinPngSize,
ParseHex, ApplyColourTable, and the constructor Skin::Skin(GuiPreset, const
std::vector<std::filesystem::path>&).

Background: Documents\Messier-Skin may hold any files. A PNG is read up to
kMaxSkinFileBytes (32 MiB), its signature and IHDR are checked against the exact size, and
it is decoded from memory. A colour table is read up to kMaxSkinTableBytes (64 KiB);
lines are "name = #RRGGBB" or "#RRGGBBAA".

Questions:
a) ReadLimited: is a file of exactly `limit` bytes read and one of limit + 1 refused? Can
   "got > limit - bytes.size()" underflow? What if the file is a directory, a link, or
   grows while it is read?
b) ParseHex and ApplyColourTable: any out-of-range index or length for odd lines (empty,
   only "=", "#", 3 or 7 hex digits, a very long line, a byte-order mark)?
c) SkinPngSize: any read past the end of `bytes`?

Rules: only this file; name another file instead of guessing. Do not cite line numbers:
cite the function name and quote the code line. No style remarks. For each question
answer "OK" with one sentence of reasoning, or a finding:
GB.n · P1–P3 · title / evidence: function + quoted line / trigger / proven or suspected / fix in one sentence.
```

---

## Part C: a new file while STOP rests the output

```text
You are reviewing code of the user's own audio plug-in (VST3, C++20). Read only.
Files (commit 50fe057, public repository):

https://raw.githubusercontent.com/sscheidl/vst3_Plugins/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/engine/looper_engine.cpp
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/engine/looper_engine.h
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/processor.cpp

Find and read in full: in looper_engine.cpp LooperEngine::SwapFile, FinishSwapSilently,
Render, Release, TakeReleased, Reset; in processor.cpp Processor::process,
TakeHandovers, TakeWindowFile, RetireReleased, Render.

Background: a file swap fades the old file out in the core (256 samples), the core
acknowledges, then the engine swaps. While STOP or Bypass rests the output, Processor::Render
renders nothing. New in 0.2.9: if the output rests at the start of a block and a swap is
running, FinishSwapSilently renders up to 16 quanta (64 samples each) into nothing so the
swap completes. All of this runs on the audio thread.

Questions:
a) If SwapFile is called several times while resting (several files loaded during STOP),
   can a file be lost (never released) or released twice?
b) If STOP is released in the same block in which a new file arrives, is anything of the
   old file heard afterwards? Check the order in Processor::process.
c) Can FinishSwapSilently allocate or take a lock (follow Render as far as these files go)?

Rules: only these files; name another file instead of guessing. Do not cite line
numbers: cite the function name and quote the code line. No style remarks. For each
question answer "OK" with one sentence of reasoning, or a finding:
GC.n · P1–P3 · title / evidence: function + quoted line / trigger / proven or suspected / fix in one sentence.
```

---

## Part D: window settings across several windows

```text
You are reviewing code of the user's own audio plug-in (VST3, VSTGUI 4, C++20). Read only.
Files (commit 50fe057, public repository):

https://raw.githubusercontent.com/sscheidl/vst3_Plugins/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/controller.cpp
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/editor.cpp

Find and read in full: in controller.cpp Controller::MakeEditor, ForEachEditor,
editorAttached, editorRemoved, editorDestroyed, setState; in editor.cpp
Editor::ApplySettings, getSize, OpenUnchecked (its first lines), UserChanged, Resize,
SetKeyboardVisible, SetDetailsVisible.

Background: the controller holds the window settings (editor_). Since 0.2.9 a change in
one window goes through callbacks.changed to the other registered windows
(ApplySettings), and a view without an open window takes the controller's settings in
getSize and when it opens (callbacks.current).

Questions:
a) Can ApplySettings on another window lead back into callbacks.changed (a loop, or a
   resize ping-pong between two windows)?
b) The lambda in MakeEditor captures a shared pointer `self` to the editor pointer.
   Can it be used after that editor was destroyed?
c) Can getSize report a size to the host that does not match the settings the window
   then opens with?

Rules: only these files; name another file instead of guessing. Do not cite line
numbers: cite the function name and quote the code line. No style remarks. For each
question answer "OK" with one sentence of reasoning, or a finding:
GD.n · P1–P3 · title / evidence: function + quoted line / trigger / proven or suspected / fix in one sentence.
```

---

## Part E: an overtaken file read

```text
You are reviewing code of the user's own audio plug-in (C++20). Read only.
Files (commit 50fe057, public repository):

https://raw.githubusercontent.com/sscheidl/vst3_Plugins/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/file_loader.cpp
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/file_loader.h
https://raw.githubusercontent.com/sscheidl/vst3_Plugins/50fe05710cd65c54bbca266d883062a6697c3dcb/instruments/messier/source/wave_overview.cpp

Find and read in full: FileLoader::Run, Request, LoadProject, Finish, FinishUnchecked,
the destructor; MakeOverview in wave_overview.cpp.

Background: a worker thread reads the WAV files the user picks in the window. Since 0.2.9
the read gives up (WavCancel) when a newer request or project load (newest_ticket_) or the
end (stopping_) comes. A finished read is dropped in FinishUnchecked when its ticket is
no longer the newest.

Questions:
a) Can a cancelled or overtaken read still be shown in the window or reach the engine?
b) Is there any path on which a newer request is lost or marked failed although it was
   never read?
c) Is newest_ticket_ always equal to ticket_ when the worker compares them?

Rules: only these files; name another file instead of guessing. Do not cite line
numbers: cite the function name and quote the code line. No style remarks. For each
question answer "OK" with one sentence of reasoning, or a finding:
GE.n · P1–P3 · title / evidence: function + quoted line / trigger / proven or suspected / fix in one sentence.
```

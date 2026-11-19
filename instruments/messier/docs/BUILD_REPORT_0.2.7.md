# Messier 0.2.7 build report

Base: 0.2.6 (uncommitted, on `6a2ce0c`) with Codex' skin-test changes of 2026-11-19
(build report 0.2.6, section "Codex handoff"). The user's orders of 2026-11-19 through the
main session: the file name in Studio One, the name set upright, and custom skins
in every build. The sound, the engine and the processor's state are unchanged; the
firmware comparison stays bit-exact.

## Codex' skin-test changes, taken over

Checked against the 0.2.6 work in progress and kept as Codex wrote them:

* `PanelView::DrawUnchecked` draws the chassis at the height of the fully open window
  (`TopHeight(true) + kKeyboardSection`), clipped to the panel, in all four section states.
* Plum's WAVE default is 200 %. `EditorSettings::wave_percent` is `kPresetWave` (0) for "the
  preset's default"; the controller's state is version 7 and accepts 0 there, versions 1
  to 6 are read as before.
* `kPresetMenuOrder`: the palette menu reads Slate, Silver, Gold, Plum; the stored IDs
  are unchanged (Plum 2, Gold 3).

## The file name in Studio One

The user saw the name beside LOAD stay at the project's file after another load.

**Cause.** The processor sent the file status's four texts (path, info, request,
detail) as binary attributes, empty ones with length 0. The controller refused the whole
status when any text was missing. Studio One's attribute lists keep no binary attribute
of length 0: the detail is empty unless a load failed, and before any load all four are.
So in Studio One every status was refused. The name came only from the project
(`Controller::setComponentState`), the information line stayed empty, and the waveform still
changed (it comes with the display message). This was found by reading the code; it was not
reproduced in Studio One.

**Fix**, as a class, in `source/message_text.h`, used by both halves:

* `SetText` sends no empty text.
* `GetOptionalText` reads a missing text as empty; the status's four texts use it.
* `GetText`, for texts that must be there (a load's path), refuses missing or empty ones.

The other messages carry no text that can be empty. Their integers stay required. The
display's overview was already optional.

**Test** `TestHostWithoutEmptyTexts`: a host whose messages, like Studio One's, keep no
binary attribute of length 0 (`EmptyDroppingHost`). It checks the first status (no file),
two loads with distinct names (`loop.wav`, `Another Name, Take 2.wav`) and a failed load. It
also checks that no empty attribute is sent any more. With the old behaviour put back
(empty texts sent, a missing one refused), it fails at its first check while the other
tests pass; I ran it that way once and then restored the fix.

## The file name upright

`skin.Sans(18)` (Bahnschrift, like the brand and the buttons) replaces `skin.Italic(19)`.
The name is shown as spelt on disk. A name too long for its place ends in ".."
(`kTruncateTail`) before the catalogue number; the tooltip has the whole path. New
preview `slate_long_name`.

## Custom skins in every build

The user: the skin import from the external folder may stay in the plug-in for good,
for custom skins. The CMake switch `MESSIER_SKIN_FOLDER` is gone.

* **Places**, for each file the first that has it wins:
  1. `Documents\Messier-Skin` (`UserSkinFolder`, the known folder, so a moved Documents
     folder is followed): writable without administrator rights.
  2. `Messier-Skin` beside `Messier.vst3` (`BundleSkinFolder`): for all users; the
     test build's place.

  Colour tables of both apply, the user's on top.
* **Fallback**, image by image: a bitmap is taken only at its exact pixel size (twice
  that for @2x). A file that is missing, a folder, empty, over 32 MB, not an image, or of
  another size or scale falls through to the next folder and then to the built-in
  bitmap. Reading never throws (`BitmapFromFile`, `ReadSkinTable`, tables up to 64 KB).
* **Menu**: **Reload skin folders** reads both again; **Open skin folder** creates
  `Documents\Messier-Skin` if needed, writes `Messier skin files.txt` (every bitmap's name and
  size, every colour's name, made from the built-in tables by `SkinFolderGuide`) once, and
  opens it in Explorer.
* The DETAILS bar shows `CUSTOM SKIN n` in place of the version while n bitmaps and
  colour values come from the folders.
* README: a section "Custom skins" with the file table.
* Tests and the preview program call `UseSkinFolders({})`, so a skin on the computer does
  not change them. `TestCustomSkin` checks:
  * a missing folder;
  * that the first folder wins;
  * a wrong-size file falling through to the second folder;
  * a broken PNG, an empty file and a 1x file under the 2x name giving the built-in bitmap;
  * the order of the colour tables;
  * that every image has both bitmaps.

  It compares the pixels as PNG encodings.

## Tests

Run on 2026-11-19 on exactly this state; logs in `out/messier-0.2.7/logs/`.

| Build | ctest (Messier tests) | Validator (all four plug-ins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 6/6 | 47/0 each |
| VS 2026 Debug | 6/6 | |
| VS 2022 (MSVC 19.44) Release | 6/6 | |
| VS 2026 Release, AddressSanitizer | 6/6, no reports | |

Firmware comparison: 0 of 960,000 samples differ, 0 allocations in process.

Previews: 48 pictures in `out/messier-0.2.7/preview/` (sixteen cases at 0.5x, 1x and 2x),
among them Plum with KEYS only and DETAILS only (Codex), and the long file name. I
looked at them: the name is upright and the long one ends before "No. 1"; Plum's WAVE
is 200 %.

Not tested here: Studio One itself (the user's DAW test, section 0.2.7 of
`DAW_TEST_0.1.0.md`), the Explorer window of "Open skin folder".

## Release build and the installed test build

The installed `Messier.vst3` is still Codex' skin-test build of 0.2.6. 0.2.7 needs no
special build: the normal build reads the folders. The folder
`C:\Program Files\Common Files\VST3\Messier-Skin\` can stay. It holds the user's Gold and
Plum experiments, which then override the built-in Gold and Plum bitmaps for every user.
To see the built-in look, move them out (or into `Documents\Messier-Skin` to keep them for
one user only). Once the user is happy with them, they can go into the plug-in itself
(`resource/gui/`), and the folder can be emptied.

## Open

* The user's DAW test, section 0.2.7.
* Codex' check of 0.2.5 to 0.2.7 together: `CODEX_PRUEFAUFTRAG_0.2.7.md` (replaces the 0.2.5 brief).

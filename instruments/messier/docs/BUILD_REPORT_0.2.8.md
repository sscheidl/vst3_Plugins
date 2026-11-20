# Messier 0.2.8 build report

Base: 0.2.7, commit `8332f66`. The user's order of 2026-11-20 through the main session:
"Alle fünf beheben", the five findings of Codex' short review of 0.2.7
([CODEX_REVIEW_0.2.7.md](CODEX_REVIEW_0.2.7.md), from the short brief
[CODEX_PRUEFAUFTRAG_0.2.7_KURZ.md](CODEX_PRUEFAUFTRAG_0.2.7_KURZ.md)). The sound, the engine
and the look are unchanged; the firmware comparison stays bit-exact.

## The fixes

* **MS-R58 (P3), incomplete file status.**
  * `FileStatus::IsCode` (`source/file_loader.h`) accepts only codes of known sound and
    request states.
  * `FileStatus::HasItsTexts` asks for the texts the states need. A sound other than
    "none" exactly when a path is there, as the loader keeps it (an empty path is "no
    file", and every other sound names its file). A running or failed request needs the
    file asked for.
  * `Controller::notify` checks both before the serial comparison. A refused status
    therefore cannot move the serial on.
  * `SetText` (`source/message_text.h`) now reports a non-empty text the host did not
    store. `SendFileStatusUnchecked` then sends no status; the window asks again on its
    next poll.
* **MS-R59 (P3), serial out of range.** `serial` must lie in 0 to 2^32 − 1 before the
  cast, as `user_loads` already did.
* **MS-R60 (P2), PNG decoded before its size was known.** `BitmapFromFolders`
  (`source/gui_skin.cpp`) works in three steps:
  1. `ReadLimited` reads the file from the open stream, at most `kMaxSkinFileBytes`
     (32 MiB), and refuses a larger one.
  2. `SkinPngSize` checks the PNG signature and that the first chunk is IHDR with data
     length 13. The width and height it declares must equal the exact @1x or @2x size.
  3. Only then are exactly these bytes decoded from memory (`createBitmapFromMemory`), and
     the size is checked once more.

  The file is never opened by path a second time, so a swap between check and decoding
  changes nothing. A file that fails a step falls through to the next folder or the
  built-in bitmap, as before.
* **MS-R61 (P3), colour table limit.** `ReadSkinTable` reads through `ReadLimited`, at
  most `kMaxSkinTableBytes` (64 KiB), from the open stream. A larger table is refused
  whole. The size check before opening is gone.
* **MS-R62 (P3), zoom clamped.** `Controller::setState` refuses the whole state when the
  zoom is finite but outside 0.5 to 2.0, before `editor_ = settings`; the value is no longer
  clamped. The other fields were already refused when out of range, not clamped: keyboard,
  DETAILS, transposition, preset, accent hues, the trail of version 5 and WAVE.

## Tests

Each fix has a test that fails without it. I undid each fix once and ran the plug-in
test (`mutate.py`, in the scratchpad):

| Fix undone | Test that failed |
| --- | --- |
| R58 `HasItsTexts` in the controller | `TestStatusChecks`: ready without a file taken |
| R58 `IsCode` | `TestStatusChecks`: sound state 6 taken |
| R58 `SetText` result ignored by the processor | `TestHostWithoutEmptyTexts`: a status without its info line taken |
| R59 serial range | `TestStatusChecks`: serial −1 taken |
| R62 zoom range | controller state: zoom 2.5 taken |
| R60 IHDR check (decode first, then compare) | `TestCustomSkin`: more files decoded than taken |
| R60 32 MiB limit raised | `TestCustomSkin`: the bezel padded past 32 MiB taken |
| R61 64 KiB limit raised | `TestCustomSkin`: a table of 64 KiB + 1 taken |

The new cases:
* **`TestStatusChecks`:** statuses built by hand. Refused: ready without a file, loading
  without the file asked for, none with a file, unknown states, serials −1 and 2^32. A
  regular one after them is taken.
* **`TestHostWithoutEmptyTexts`:** a host that does not take the info text. No status
  arrives; once the host takes it again, the status arrives.
* **`TestCustomSkin`:**
  * the real thumb with IHDR length 12;
  * a header declaring 100000 × 100000;
  * the real bezel padded to 32 MiB + 1;
  * `SkinFilesDecoded` shows that only the two files taken reach the decoder;
  * tables of exactly 64 KiB (read) and 64 KiB + 1 (refused).
* **Controller state:** zoom 2.5 and 0.25 refused, 2.0 and 0.5 taken.

In the three skin runs above, the test program ended with 0xC0000374 after its failed
check. CHECK calls `std::exit` while VSTGUI and COM are still set up and skins are alive;
the regular run ends cleanly.

| Build | ctest (Messier tests) | Validator (all four plug-ins) |
| --- | --- | --- |
| VS 2026 (MSVC 19.51) Release | 6/6 | 47/0 each |
| VS 2026 Debug | 6/6 | |
| VS 2022 (MSVC 19.44) Release | 6/6 | |
| VS 2026 Release, AddressSanitizer | 6/6, no reports | |

Firmware comparison: 0 of 960,000 samples differ, 0 allocations in process. Logs in `out/messier-0.2.8/logs/`. No previews: nothing
visible changed.

## Open

* Codex' check of the five fixes: [CODEX_PRUEFAUFTRAG_0.2.8.md](CODEX_PRUEFAUFTRAG_0.2.8.md)
  (short, no build). The full [CODEX_PRUEFAUFTRAG_0.2.7.md](CODEX_PRUEFAUFTRAG_0.2.7.md)
  stays open.
* The user's DAW test, section 0.2.7 of `DAW_TEST_0.1.0.md` (0.2.8 adds nothing to see).

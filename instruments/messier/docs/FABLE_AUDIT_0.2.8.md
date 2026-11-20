# Messier 0.2.8 — full audit by Fable

On 2026-11-20 Codex was unavailable. At the user's request, five Fable reviewers (model
`claude-fable-5-2`) read Messier 0.2.8 (commit `ba3a71d`) as a whole for the first time:
until then, every review covered one version's diff. They worked read-only, with no build
and no tests. Four took one area each:

* A: messages and state;
* B: threads and the file loader;
* C: window and skins;
* D: engine and adapter.

A fifth (E) looked for bugs where these areas meet. The reviewers got the code, the README
and the list of known findings (MS-R01 to MS-R62), but no build reports and no reasoning
from Claude Opus. The vendored firmware core and the tests were left out.

This is **not an independent check**: Fable and Opus are models of the same family. Codex
should go through this list next week.

Claude Opus then checked every finding in the code. "Confirmed" means read and traced
there. A test that reproduces a finding is written together with its fix, and fails
without it, as before. Nothing is fixed yet.

## Result

20 distinct findings: no P0, one suspected P1 (C1), the rest P3; one P2 rating (A1) was
lowered to P3. Three were found independently by two or three reviewers (F1, F2, F6).

| ID | Sev. | Finding | Found by | Verdict |
| --- | --- | --- | --- | --- |
| C1 | P1? | Closing the window while LOAD's dialog or a header menu is open leaves the button and VSTGUI frames freed on the call stack | C | Confirmed in code (the button touches itself after the modal call); not reproduced in a host |
| F1 | P3 | A project refused for lack of memory sends `kFailed` without a request; the 0.2.8 check (MS-R58) then refuses every status | B1, A2, E4 | Confirmed; **regression of 0.2.8** |
| F2 | P3 | A file loaded during STOP/Bypass is swapped in only when the sound resumes: the window shows the new name with the old waveform, and on resume about 5 ms of the old file can be heard | A1 (P2), D1, E2 | Confirmed (no quantum is rendered while resting) |
| F3 | P3 | Window settings reach only attached editors; two windows of one instance overwrite each other's settings, and a reused view reopens with old ones | E1, C4 | Confirmed in code; reuse depends on the host |
| E3 | P3 | A failed window load stays on the file line after a later successful project load | E | Confirmed (`LoadProject` resets only `kLoading`) |
| E5 | P3 | A latched screen key stays lit after a restart without notes; KEYS hides a latched note with no way to let it go | E5, C10 | Confirmed |
| C10 | P3 | LATCH survives closing the window if the host reuses the view | C | Confirmed in code; depends on the host |
| F6 | P3 | A running file read cannot be cancelled: removing the plug-in waits for the whole read, and overtaken reads hold memory | B3, E6 | Confirmed |
| C2 | P3 | After LOAD... or a header menu the window keeps the mouse capture; the next click elsewhere is swallowed | C | Confirmed in code (no `ignoreFollowUpMoveAndUpEvents`) |
| C3 | P3 | Choosing a size or preset menu entry sends `beginEdit`/`endEdit` for parameter 0xFFFFFFFF to the host | C | Confirmed (`Editor::beginEdit` filters only HUE and WAVE) |
| C5 | P3 | A secondary knob keeps keyboard focus after DETAILS closes; arrow keys change the hidden parameter | C | Confirmed in code |
| C6 | P3 | Ctrl+click on HUE or WAVE sets 143 or 113 % (VSTGUI's default value 0.5), not the preset's | C | Confirmed in code |
| C7 | P3 | A custom PNG with a valid header but broken pixel data is accepted and then drawn as nothing | C | Suspected: VSTGUI decodes lazily; not run |
| C8 | P3 | `negative` in a colour table is documented and counted but no drawing code reads it | C | Confirmed |
| C9 | P3 | A colour table saved with a UTF-8 BOM loses its first line | C | Confirmed |
| C11 | P3 | The catalogue number's tooltip never shows (the file zone covers it) | C | Confirmed in code |
| A3 | P3 | The class is declared `kDistributable`, but the message protocol assumes one process and one clock | A | Confirmed; no known host distributes |
| B2 | P3 | A window load finishing within microseconds of an off-thread `setState` leaves the sound and the saved path apart | B | Traced; needs concurrent host calls, a gap of microseconds |
| D2 | P3 | After every restart the six secondary controls glide up from 0 for about 60 ms, so the first grains are hardly randomised | D | Confirmed; the firmware core does the same at power-on (`Engine::Init`) |
| D3 | P3 | Speed at 12 o'clock settles near 2e-6, not exactly 0 | D | Suspected (calculated, not measured); inaudible |

Areas the reviewers found sound include:
* the WAV parser on hostile files;
* the triple buffer, queue and sequence lock;
* allocation and locking on the audio thread;
* project/file pairing in every order traced;
* controller states 1 to 7;
* display data range checks;
* the resampler at the validator's odd rates;
* the event timing;
* the exception boundaries at host entries.

## Recommendation for 0.2.9

* **Fix:**
  * F1 (my own regression);
  * C1 (start the dialog and menus from the main queue after the mouse event, as
    `AnimateLater` does; this also fixes C2);
  * F2 (complete a pending swap silently while resting);
  * F3, E3, E5 with C10;
  * C3, C5, C6, C7 (force one decode before accepting);
  * C8 (document as unused, like `wave`, `wave_outside`, `loop_edge`);
  * C9, C11;
  * A3 (drop `kDistributable`);
  * F6 (a cancel check in the WAV reader, per chunk and per MiB).
* **Leave, document:**
  * B2 (needs concurrent host calls within microseconds);
  * D3 (inaudible; reword "exactly 0x").
* **The user's decision:** D2 changes the firmware core (seed the secondaries in
  `Engine::Init`). The bit-exact comparison with the firmware would then need the same
  patch in its reference, or the behaviour stays as the firmware has it.

Each fix gets a test that fails without it, where a test can show it. C1 and C2 need a
host or a real window; they are checked by reading and in the DAW test.

## Cost

Five reviews of about 11,000 lines; between about 184,000 and 309,000 tokens each, about
1.2 million in total.

## Outcome (0.2.9)

The user chose "as recommended" (2026-11-20). 0.2.9 fixes every finding listed under "Fix";
C7 only in part (see [BUILD_REPORT_0.2.9.md](BUILD_REPORT_0.2.9.md)). B2 and D3 are left
and documented; D2 waits for the user.

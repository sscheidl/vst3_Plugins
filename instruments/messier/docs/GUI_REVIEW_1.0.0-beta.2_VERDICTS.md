# Messier 1.0.0-beta.2 GUI review: verdicts

Commit `768cea1`. Sources:
- part 0, `CLAUDE_REVIEW_1.0.0-beta.2_GUI.md`: MS-R63 to MS-R70, an independent Claude session standing in for Codex, with measurements;
- GPT parts A, B and C, `GPT_REVIEW_1.0.0-beta.2_GUI.md`: B2A.1 to B2C.5, static.

Each GPT finding was checked by Claude Opus (messier-gui session) in the code of 768cea1. Part 0 was checked by messier-gui-beta2 (all eight hold).

## Duplicates

| GPT | Same as part 0 |
| --- | --- |
| B2A.4 (full bitmap turn on the GUI thread) | MS-R63, MS-R64 (measured) |
| B2A.5 (whole view invalidated every frame) | MS-R66 (measured) |
| B2A.6 (paths and gradients per star) | MS-R65 (measured) |
| B2A.7 (2x galaxy copy at 1x) | part of MS-R63 |
| B2C.4 (skin loaded per window) | MS-R70 |
| B2C.5 (full HUE recolour while dragging) | MS-R67 (measured) |

## Bugs to fix (small, local fixes)

| ID | GPT / part 0 | Verdict | Fix |
| --- | --- | --- | --- |
| B2B.2 | P2 | **Confirmed, P2.** `Editor::ApplySettings` ends with `if(frame && !Resize()) LayOut(...)`, without the second branch of `Editor::ShowSections` (host agreed, `onSize` still pending). This is the root cause of the round-4 KEYS bug (R4.2), which was fixed only in `SetKeyboardVisible`/`SetDetailsVisible`. A project load or another window's change of KEYS, DETAILS or zoom keeps the old layout in Studio One. | Call `ShowSections()` there. |
| B2B.1 | P2 | **Confirmed, P3.** `ApplySettings` hides the keyboard without `keyboard_view_->ReleaseHeld()`, unlike `SetKeyboardVisible` (E5/C10 of 0.2.9). Trigger: window B toggles KEYS off (shared to A via `ShareSettings`), or a controller-only state load, while A holds a latched key; A's note sounds on with its keyboard hidden until KEYS is shown again. Existed since 0.2.9. | Release held keys when `settings.keyboard` turns false. |
| MS-R68 | P3 | Holds (proven by the part-0 harness): caches keyed by a raw `Skin*` can match a new skin at a freed address and show the old nebula. | Key by `std::shared_ptr<const Skin>` or a skin serial, as `DrawWave` does. |
| MS-R69 | P3 | Holds: BLEND's square knob box takes clicks on the glass ring. | Round `hitTest` for `MetalKnob`. |
| B2A.3 | P2 | **Confirmed, P3.** `MapView::ShowAt` drops an ended grain when `audio - left >= kTrail` (0.45 s), the WAVE trail; GALAXY stars glow up to `kStarMinLife` + `kStarGlow` (about 1.65 s). Only after a display gap above 0.45 s (large host buffer, stall). | Keep ended grains for the longer of the two; WAVE still cuts its trail at `kTrail` when drawing. |
| B2A.8 | P3 | **Confirmed, P3 (hardening).** `TurnedNow` sets `layer.source` before `ReadPixels`; a failed read is never retried, so the layer stays empty until the source changes. Same in `DrawDustLayer` with `layer.skin`. | Take the key only after a successful read. |
| B2B.4 | P3 | **Confirmed, P3.** The octave tooltips still say "lowest: C2 to C4" / "highest: C6 to C8" (two octaves); the keybed is one octave now. | Derive the text from the one-octave limits. |
| B2C.2 | P3 | **Confirmed, P3.** `SkinFolderGuide` writes `messier_nebula_<nebula>_@1x.png` for the painted nebula (empty stem; `SkinImageFileName` writes no underscore) and lists only spiral, barred, ring, cloud, not blue, orange, red, violet. | Build the guide's names with `SkinImageFileName`; list all eight keys. |
| B2A.1 | P2 | **Confirmed, P3 (cosmetic).** `StarStart` takes the eased tempo (`drift_speed_ / kDriftPerTempo`, ease 0.7 s); stars born while it is still below 0.05 stand anywhere instead of starting at the centre. At 1x this lasts about 40 ms; at 0.1x about 0.5 s. | Take the direction from the target tempo (`DriftTarget`), not the eased one. |
| B2A.2 | P2 | **Not reachable in practice; cheap hardening.** Grain ids come from `next_grain_id` in `LooperEngine::Impl`, which `Reset` does not set back, so ids keep rising across files and restarts within one processor; an old `stars_` entry never matches a new grain. Only a replaced processor or a 2^32 wrap could repeat an id. | Clear `stars_` on a change of `sequence`/`frames`, with `ended_count_`. |
| B2B.5 | P3 | **Unreachable; optional hardening.** The `begin` callbacks are guarded and do not throw. Same pattern as beta 1's START. | Track a successful begin; optional. |

## Performance (measured in part 0; software rasteriser, so on-screen numbers are lower)

| ID | Verdict | Recommendation |
| --- | --- | --- |
| MS-R63 (+ B2A.4, B2A.7) | PERF-high, the largest cost. GALAXY turns the painted nebula on the CPU (`TurnPixels`, 24 ms at 720 px), every 0.2 degrees, i.e. up to every frame at 4x; it takes the 2x copy also at 100 %. | First pick 1x/2x by `DeviceScale`. Then stop turning pixels: draw the unturned bitmap through a rotation transform of the draw context (VSTGUI `CDrawContext::Transform` with `CGraphicsTransform().rotate(angle, centre)`), which Direct2D does in hardware on screen. Keep the CPU turn only as a fallback, or turn in coarser steps. |
| MS-R64 | PERF-medium. The dust masks are turned the same way, faster. | Same transform; the masks are soft, so coarse angles also suffice. |
| MS-R66 (+ B2A.5) | PERF-medium. The timer invalidates the whole 436 x 436 view every frame, also when nothing moves, and repaints the panel and five controls with it (about 70 % of each idle repaint). | Invalidate only while something moves, and only the glass square plus the triangle's box (`invalidRect`). Cheapest win. |
| MS-R65 (+ B2A.6) | PERF-medium. About 12 Direct2D objects per star per frame. | One pre-rendered star sprite per tint (rebuilt on PITCH/HUE change), drawn scaled and faded. |
| MS-R67 (+ B2C.5) | PERF-medium. Dragging HUE rebuilds full-size bitmaps per step (about 50 ms at 200 %). | Rebuild at most every ~100 ms while held, and on release. |
| MS-R70 (+ B2C.4) | PERF-low. Each window decodes all bitmaps, also unused ones (about 36 MB of pixels per painted preset). | Dust masks at 1x and only the chosen kind; a shared bitmap cache only if needed. |
| B2B.6 | PERF-low, harmless. Identical START/END values sent again while dragging. | Optional: skip unchanged values. |

## Not changed

| ID | Reason |
| --- | --- |
| B2B.3 | Suspicion only. Part 0 answered it (VSTGUI's linear mode keeps its own unsnapped entry state; fine-drag still moves). Change only if the user hears or sees a problem. |
| B2B.7 | Linear parameter-info search over 17 parameters per value text, since beta 1; negligible. |
| B2C.1 | States before version 9 accept preset ids 4 to 7, which they never contain; such a (corrupted) state opens a valid preset. Harmless. |

## For the user to decide

* **B2C.3:** beta 1's custom colour table `messier_slate_colours.txt` is applied under every new preset, including the painted galaxies. Options: (a) only under the Spiral preset (ID 0, Slate's successor), recommended; (b) not at all; (c) keep as is and document it.

## Still open from the brief

The full test matrix (Debug, VS 2022, ASan) has not been run since round 1 of beta 2; run it after these fixes. There is no on-screen (hardware) measurement yet; compare before/after in Studio One with the GALAXY view at 4x.

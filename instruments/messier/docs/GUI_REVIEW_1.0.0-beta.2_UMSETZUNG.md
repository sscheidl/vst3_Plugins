# Messier 1.0.0-beta.2 GUI review: implementation

Fixes of the verdicts in `GUI_REVIEW_1.0.0-beta.2_VERDICTS.md`, made on 768cea1 in worktree
`messier-beta2-117e35` and **not committed**. Done by messier-gui-beta2, 2026-11-21.

## Bugs

| ID | Done |
|---|---|
| B2B.2 | `Editor::ApplySettings` ends with `ShowSections()`, as KEYS and DETAILS do. |
| B2B.1 | `ApplySettings` calls `keyboard_view_->ReleaseHeld()` when the settings hide the keyboard. |
| MS-R68 | `turned_skin_`, `tinted_skin_` and `DustLayer::skin` are `std::shared_ptr<const Skin>`. |
| MS-R69 | `MetalKnob::hitTest` accepts the round cap only (distance from the centre ≤ half the box). Tested. |
| B2A.3 | Ended grains are kept for max(`kTrail`, `kStarMinLife` + `kStarGlow`); `DrawTrail` still cuts WAVE's trail at `kTrail`. |
| B2A.8 | `TurnedNow` takes the source only after a successful `ReadPixels`. `DrawDustLayer` takes the skin only once the mask is made (an empty mask counts). |
| B2B.4 | The octave tooltips are built from `kKeyboardLowestTranspose`/`kKeyboardHighestTranspose`: C2 to C3 and C7 to C8. |
| B2C.2 | The guide builds the names with `SkinImageFileName` and lists all eight keys. Tested. |
| B2A.1 | New stars are born by `DriftTarget()` (the heading); they fly with the eased tempo. |
| B2A.2 | `stars_` and `next_star_` are cleared with `ended_count_` on a change of sequence or frames. |
| B2B.6 | `StartBar::DragTo` skips a value equal to the last one sent. |
| B2C.3 | User decision "only under Spiral": `messier_slate_colours.txt` is read only for `GuiPreset::kSpiral`. Tested; the custom-skin tests moved to the spiral. |
| B2B.5 | Not done (optional, unreachable). |

## Performance

| ID | Done |
|---|---|
| MS-R66 | `MapView::Tick` calls `invalidRect(GlassBox())` only while `Moving(now)`: turn or rate, drift, a pending recolour, a loop preview, grains or a moving playhead, afterglows, or a non-zero drift target. Tested. |
| MS-R63 | The coloured nebula takes x2 only when `DeviceScale > 1.01`. `TurnPixels` is rewritten: 16.16 fixed point, stepping along each row, only the inscribed disc, `to` reused (`Turned::turned`). Measured 3.5 ms at 720² (part 0: 24 ms). Tested: 0° identity, exact 90°, clear corners. **The rotation transform recommended in the verdicts was not used.** VSTGUI's Direct2D `drawBitmap` computes the clip from two transformed corners of the destination, so turned bitmaps are cut into wrong rectangles; that was the user's Studio One bug in the first beta 2 build and the reason for the CPU turn. |
| MS-R64 | Dust layers turn in `kDustTurnStep` = 0.6° steps (nebula 0.2°), with the faster `TurnPixels`. |
| MS-R65 | `UpdateSprites` makes halo and glow (radial, 32 units) and the rays (across and down, 64 × 2 units), once per tint and device scale. `DrawSprite` draws them through an axis-aligned scale transform, which is not affected by the clip issue. No `createGraphicsPath` or `CGradient` per star any more. Both views share one ray colour (core + 45 % white; WAVE had 30 %). |
| MS-R67 | `Recolour()`: a change of HUE alone rebuilds the coloured nebula, the tinted layers or the waveform at most every 100 ms; meanwhile the last one is drawn and `Moving` keeps redrawing until the pending one is made. |
| MS-R70 | `Skin` loads the four dust masks at 1x only. Tested. The unchosen dust kinds are still loaded at 1x: the skin does not know the choice. |
| B2B.6 | See bugs. |

## Checks

- All 6 `messier.*` ctests pass in VS 2026 Release; this includes the firmware comparison in `messier.engine`.
- Validator 47/0 for Messier2.
- Mutation check: 6 mutants, all caught. They covered `TurnPixels` (wrong rotation sign), `Moving` (always true), the round knob hit test (always true), the slate-table rule (off), the guide name (extra underscore) and the dust 2x load (back).
- The full matrix (VS 2026 Debug, VS 2022 Release, ASan, the validator for all four plug-ins) is running; its results go into the build report.
- Seen in previews:
  - WAVE grains look as before; the rays are a touch whiter.
  - GALAXY in the single-frame preview shows new stars at the centre, because the eased tempo is still 0 there (B2A.1). In the host they fly out within the 0.7 s ease.

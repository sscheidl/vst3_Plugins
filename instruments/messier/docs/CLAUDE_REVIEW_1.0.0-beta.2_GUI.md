# Review of Messier 1.0.0-beta.2, GUI and visualisation (part 0 of the brief)

Reviewer: an independent Claude session standing in for Codex (Codex unavailable until Monday).
(Kept in the session notes as `CODEX_REVIEW_1.0.0-beta.2_GUI.md`, the brief's name for part 0.)
Commit reviewed: `768cea1eb210fe6ad3708cb86b3c3230f6743fb5` (branch `claude/messier-beta2-117e35`),
baseline `a6823d6` (tag `messier-v1.0.0-beta.1`). Review only: nothing fixed, committed, pushed or
installed. Scope and out-of-scope list as in `GUI_PRUEFAUFTRAG_1.0.0-beta.2.md`, part 0.

## What was run

- Own build folder in the review worktree, VS 2026 (`Visual Studio 18 2026`, x64, Release), SDK of the
  main checkout. `ctest -R '^messier\.'`: **6/6 passed** (vocoder 21.4 s, engine 2.4 s, adapter 4.3 s,
  loader 0.5 s, plugin 6.4 s, provenance 0.1 s). Debug, VS 2022 and ASan were **not** run.
- `preview_messier_window`: 111 PNGs (37 cases × 1x/2x/0.5x) rendered and spot-checked
  (layout open, WAVE dense, 0.5x); no drawing fault seen.
- A throw-away measuring harness (not in the repository, removed again) linked against
  `messier_shell`: it draws a `MapView` (436 × 436, the real size) into a `COffscreenContext` every
  25 ms for 5 s with displays every 30 ms (grains of 0.5 s life, ids, playhead moving), and reports
  the per-frame time after 2.5 s of warm-up; plus timings of `TurnPixels`, `MakeBitmap`,
  `HueTurnedCopy`, `Skin()`, and of `frame->drawRect` over the map's rect in a real opened `Editor`.
- **Caveat on the numbers:** VSTGUI's offscreen context on Windows draws through a WIC bitmap
  render target, i.e. Direct2D's *software* rasteriser. Gradient and path costs on the host's
  on-screen (hardware) target are likely lower. `TurnPixels`, `HueTurnedCopy`, `ReadPixels` and
  the bitmap uploads are plain CPU work and cost the same on screen. The real timer ticks every
  30 ms (`kDisplayInterval`), not 25 ms.

## Findings

### MS-R63 · PERF-high · GALAXY turns the painted nebula on the CPU inside draw(), at 2x resolution also at 100 %

- **Evidence:** `MapView::DrawGalaxy`:
  `CBitmap* source = turned_nebula_.x2 ? turned_nebula_.x2.get() : turned_nebula_.x1.get();`
  then `paint(TurnedNow(nebula_turned_[0], source, galaxy_angle_));`. Without HUE (`std::abs(by) < 0.5`)
  `turned_nebula_ = coloured`, which has both sizes, so the 720 × 720 copy is taken whatever the
  device scale (the arms branch's `pick` does choose by `two`). `MapView::TurnedNow`:
  `if(!layer.drawn || moved >= step) { PixelBuffer turned; TurnPixels(layer.pixels, angle, turned); if(auto bitmap = MakeBitmap(turned)) ...`
  with `kTurnStep = 0.2` degrees, so the rebuild rate grows with SPEED: 1.5 °/s at 1x (7.5 rebuilds/s),
  6 °/s at 4x (30/s, nearly every frame). `TurnPixels` is a double-precision bilinear loop over every
  pixel of the square (bounds-checked lambda, `std::lround` per channel), on the host's UI thread.
- **Trigger:** GALAXY with any of the four painted presets (Blue, Orange, Red, Violet) while playing;
  worse at higher |SPEED|. The spiral/barred/ring/cloud presets turn two layers (arms and haze).
- **Proven (measurement):** `TurnPixels` 720 × 720: 24 ms; 360 × 360: 6 ms; `MakeBitmap` 720²: 0.6 ms.
  MapView frames, Blue galaxy, no grains, dust 50 %:

  | scale | SPEED | mean | p95 | max (ms/frame) |
  |---|---|---|---|---|
  | 100 % | STOP (no turn) | 4.1 | 7.0 | 7.1 |
  | 100 % | 1x | 9.2 | 28.1 | 29.7 |
  | 100 % | 4x | 24.9 | 37.5 | 40.2 |
  | 200 % | STOP | 4.8 | 5.5 | 6.3 |
  | 200 % | 1x | 11.4 | 30.4 | 31.3 |
  | 200 % | 4x | 28.9 | 40.0 | 42.1 |

  At 4x the turn alone uses the whole frame budget; every frame with a rebuild overruns it. The
  spiral preset (two layers) measured mean 47–53 ms per frame at 200 % with 20 grains at 1x, against
  35 ms for the Blue galaxy under the same conditions.
- **Fix:** take the 1x or 2x copy by `DeviceScale` as the arms branch does, and make the turn itself
  cheap (fixed-point bilinear over the disc only, buffers and platform bitmap reused) or move it off
  the draw call (a worker that prepares the next angle, or a small ring of pre-turned angles).

### MS-R64 · PERF-medium · The two dust masks are turned the same way, at a higher rate than the nebula

- **Evidence:** `MapView::DrawDustLayer`: `if(CBitmap* drawn = TurnedNow(layer.turned, layer.mask, layer.angle))`;
  the layers turn at `360 / kDustTurn` = 2.4 °/s (layer 0) and 0.4 × that (layer 1) per 1x, while
  `TurnedNow` keeps `kTurnStep` scaled by the *galaxy's* `turn_speed_`, so layer 0 is rebuilt 12×/s at
  1x and 48×/s (every frame) at 4x. Each rebuild is a 360² `TurnPixels` (4.9 ms) plus `MakeBitmap`.
- **Trigger:** GALAXY with dust (default 50 %), playing.
- **Proven (measurement):** 100 %, no grains, 4x: mean 24.9 ms with dust, 15.6 ms without; 1x: 9.2 vs
  7.7 ms. 200 %, 4x: 28.9 vs 17.2 ms.
- **Fix:** the masks are soft (the code says their 1x size serves at every zoom), so turn them in
  coarser steps (e.g. 1°) or at a capped rate, with the cheaper `TurnPixels` of MS-R63.

### MS-R65 · PERF-medium · Every star and grain builds its paths and gradients anew in every frame

- **Evidence:** `MapView::DrawStarfield`, the `star` lambda: `if(auto path = owned(context->createGraphicsPath()))`
  … `owned(CGradient::create(0.0, 1.0, WithAlpha(tail_ink_, light * (0.6 - 0.25 * out)), ...))` for the
  halo, the same for the glow, then `DrawRays(...)`, which makes four more path + gradient pairs
  (`auto path = owned(context->createGraphicsPath()); auto gradient = owned(CGradient::create(...))` in
  its `ray` lambda): 12 Direct2D objects per star per frame. WAVE's grains: halo + rays = 10 per grain,
  plus up to 24 `drawLine` per trail. With 20 grains of 0.5 s, about 40–70 stars glow at once
  (`kStarMinLife` + `kStarGlow` = 1.65 s), i.e. several hundred gradient objects per frame.
- **Trigger:** GALAXY or WAVE with a dense patch (DENSITY high, many afterglows).
- **Proven on the software target, suspected magnitude on screen:** STOP (no turn), Blue galaxy:
  0 grains 4.1 ms, 5 grains 11.8 ms, 20 grains 30.9 ms per frame at 100 % (200 %: 4.8 / 12.1 / 30.8 ms).
  WAVE: 0 grains 4.0 ms, 20 grains 11.8 ms (200 %: 5.6 / 12.8 ms). Playing at 1x with 20 grains the
  GALAXY frame averaged 35–38 ms (p95 56–60 ms), WAVE 11.6–12.6 ms.
- **Fix:** draw a star from one pre-rendered sprite per tint (halo, glow and rays in a small bitmap,
  rebuilt on PITCH/HUE change) scaled and faded with `drawBitmap(..., alpha)`, instead of making
  gradients per star.

### MS-R66 · PERF-medium · The timer repaints the whole map rect every frame, also when nothing moves, and drags the panel and five controls along

- **Evidence:** `MapView::Tick`: `if(Clock::now() - drawn_at_ >= kFrameInterval) Guarded([&] { invalid(); });`
  has no test for motion, and `invalid()` covers the 436 × 436 view, which overlaps the panel, the BLEND,
  DENSITY and VOLUME knobs with their name/value labels and the `ViewSwitch`. `PanelView::DrawUnchecked`
  draws the chassis, the rules, the brand (`DrawTracked(context, "MESSIER", ...)`) and the version on
  every call; only its labels test the clip (`if(!CRect(box).extend(4, 4).rectOverlap(clip) ...`).
- **Trigger:** any open window: no file, STOP, a resting engine, FREEZE with no grains.
- **Proven (measurement, real Editor, idle, no file):** `frame->drawRect(map rect)` 7.35 ms at 100 %
  (8.99 ms at 200 %), the map view alone 1.67 ms (3.47 ms), the glass square 360 × 360 2.50 ms
  (4.38 ms): about 70 % of each idle repaint is spent on views that did not change. While the window
  is hidden, `invalid()` produces no paint, so then only the 30 ms poll remains.
- **Fix:** in `Tick`, invalidate only while something moves (turn or drift speed ≠ 0, sounding grains or
  afterglows, a loop preview, a drop state), and only the glass square plus the position triangle's
  box (`invalidRect`), not the whole view.

### MS-R67 · PERF-medium · Dragging HUE rebuilds full-size bitmaps for every step

- **Evidence:** `Editor::SetAccent` → `skin_->SetAccent(hue); ApplySkin();` (which ends in
  `frame->invalid()`). `MapView::DrawGalaxy` then makes
  `turned_nebula_ = std::abs(by) < 0.5 ? coloured : HueTurnedCopy(coloured, by, two);`, after which
  `TurnedNow` sees a new source (`if(layer.source != source)` → `ReadPixels`) and turns it again.
  The arms branch's `TintedCopy` tints both sizes (`tinted.x1 = TintedBitmap(white.x1, tint); tinted.x2 = ...`).
  In WAVE, `Skin::SetAccent` turns `plate_wave`, which is part of `DrawWave`'s key
  (`cached_colour_ == c.plate_wave`), so the whole waveform bitmap is drawn again.
- **Trigger:** moving the HUE slider (DETAILS open) in either view.
- **Proven per part, suspected as a sum:** `HueTurnedCopy` 24.2 ms at 2x, 6.2 ms at 1x; `TurnPixels`
  24 / 6 ms; the first WAVE frame (bitmap build) 26–55 ms. Per HUE step at 200 % that is roughly
  50 ms in GALAXY (≈ 12 ms at 100 %) and 50 ms in WAVE, on top of a full-window repaint.
- **Fix:** while the slider is held, rebuild at most every ~100 ms (or on release) and keep drawing
  the last bitmap in between.

### MS-R68 · P3 · A stale nebula after preset changes made in WAVE (caches keyed by a raw `Skin*`)

- **Evidence:** `MapView::DrawGalaxy`:
  `if(turned_skin_ != skin_.get() || turned_by_ != by || turned_two_ != two || !turned_nebula_)`; the same
  pattern in `if(tinted_skin_ != skin_.get() || ...)` and in `MapView::DrawDustLayer`
  `if(layer.skin != skin_.get())`. The cache does not hold the skin, so once the old skin is freed a
  new `Skin` can be allocated at its address and the key matches. (`DrawWave` does it right: it keeps
  `std::shared_ptr<const Skin> cached_skin_`.)
- **Trigger:** GALAXY drawn with preset A; switch to WAVE; pick preset B, then preset C from the menu
  (A is freed once every view has B; C may land on A's address); switch back to GALAXY: A's nebula
  is shown with C's colours, until HUE, the preset or the device scale changes. Also possible after
  "Reload skin folders".
- **Proven (harness test):** MapView alone, exactly that sequence with Blue → Orange → Red: Red's skin
  got Blue's address on the first try; the glass then drawn was pixel-identical to a fresh Blue-galaxy
  view (mean byte difference 0.00) and differed from a fresh Red one (22.6 per byte); two fresh Red
  views differed by 0.00.
- **Fix:** key these caches by a held `std::shared_ptr<const Skin>` (as `cached_skin_` in `DrawWave`) or
  by a serial number given to each new skin.

### MS-R69 · P3 · BLEND's square knob box takes clicks on the glass ring

- **Evidence:** `Editor::OpenUnchecked` adds `map_view_ = Add(new MapView(...))` before
  `auto* knob = Add(new MetalKnob(knob_box, this, ...))`, so the knobs lie on top; `MetalKnob`/`CKnob`
  keep `CView::hitTest`, a rectangle. `layout::KnobBox(8)` is 404..476 × 454..526, while
  `MapView::hitTest` accepts points up to `GlassRadius() + L::kRingWidth` (196) from (440, 272).
- **Trigger:** a click on the lower edge of the ring just left or right of BLEND's cap (about 460 window
  units², e.g. (408, 458) at 100 %): it begins a BLEND edit (and a drag turns BLEND) instead of setting
  START. (The `ViewSwitch` is fine: its `hitTest` gives the middle back to the knob.)
- **Proven (geometry from the code; not clicked in a host).**
- **Fix:** give `MetalKnob` a round `hitTest` (distance to the centre ≤ half the box) as `ViewSwitch` and
  `MapView` have.

### MS-R70 · PERF-low · Bitmaps decoded per window that are never drawn

- **Evidence:** `Skin::Skin` loads every `kImages` entry in both sizes; `MapView::DrawDustLayer` uses only
  `CBitmap* source = mask.x1 ? mask.x1.get() : mask.x2.get();` and only the chosen dust kind plus
  `kDustClouds`.
- **Trigger:** every open window (each `Editor` makes its own `Skin`; `Skin()` took 11–14 ms).
- **Suspected (size from the PNG headers, decoding may be lazy):** the four dust masks at 2x are 8.3 MB of
  pixels per window; the two unused dust kinds add 2.6 MB at 1x; a painted preset's skin is about
  36 MB of pixels in all.
- **Fix:** load the dust masks at 1x only and only the chosen kind.

## Answers to the questions

**1a fish-eye — OK.** `lens::ToGlass`/`FromGlass` are exact inverses: over a 201 × 201 grid of the plate
including the rim and the corners the worst round-trip error was 7.6e-15 (computed); points outside
the glass go radially to the rim, which for a circle is the nearest rim point and matches what is drawn
there. `PlateToView`, `ShareAt`, `hitTest` and the mouse handlers all work in view-relative units
(`event.mousePosition - getViewSize().left/top`), and VSTGUI removes the frame zoom before the views see
the mouse (`CFrame`: `getTransform ().inverse ().transform (...)`), so 50 % and 200 % agree as well (0.5x/2x previews checked). The wave bitmap's `at` lambda uses
the same `ToGlass` with the glass' radius. Only MS-R69 lets another control take a click on the ring.

**1b StartBar — OK.** `Share` and `SizeAt` clamp to [0, 1]; END cannot cross START (`SizeAt` ≥ 0 gives a
loop of one frame, `LoopFrames` ≥ 1), START at 100 % sits on the last frame (`StartX`/`EndX` clamp as the
core does). Press → `begin`/`size_begin` then the value, drag → values, `Release` → the matching `end`;
cancel and `Editor::FinishEdits` end a drag in progress. No value leaves [0, 1].

**1c view switch and PITCH snap — OK except MS-R69.** `ViewSwitch::hitTest` answers only on the arcs and
names. `SnappedByKnob` is applied only in `Editor::valueChanged`; host automation and MIDI reach
`ParameterChanged`, which never snaps. With the knob held, VSTGUI's linear mode continues from its own
`entryState` (unsnapped), circular mode is absolute, so fine-drag still moves; wheel and arrow keys (0.01)
always leave a snap band: computed from `parameters.h`, ±0.01 moves SPEED ≥ 0.07x away from 0x/±1x and
PITCH by 0.96–1.44 of a 50-cent step. Double-click defaults (PITCH 0, SPEED's default) are on the grid.

**1d keybed — OK.** `ClampKeyboardTranspose` keeps −24..+36, i.e. notes 36..108; `SetTranspose` releases
before it moves; KEYS off releases a latched key (`SetKeyboardVisible`), close releases it
(`FinishEdits`), a project load clears it through `ForgetHeldKey` in `setComponentState`. Resizing keeps a
latched key on purpose. (Gaps between the upper pads release and re-strike during a glissando, by design.)

**1e window size — OK.** `SetKeyboardVisible`/`SetDetailsVisible` keep the choice and lay the content
into the current window when the host refuses or answers later (`ShowSections`); `OnSizeUnchecked`
recomputes the zoom for the sections in force; `SetZoom`/`GripDrag` roll back when the host refuses.
`checkSizeConstraint` uses the same sections. No disagreement found.

**1f controller state 8 and 9 — OK.** V1–V7 read four hues (`kLegacyGuiPresetCount`), V8 adds the GALAXY
byte, V9 the count (0..256; hues beyond `kGuiPresetCount` are checked and dropped); every field outside
its range refuses the whole state as before. Notes, not findings: a beta-1 preset ID 0..3 now opens the
spiral/barred/ring/cloud nebula with that slot's hue, and a beta-1 hue off the new red–violet–blue arc is
shown at the nearer end (`AccentOnScale`: e.g. a green 120 becomes blue 230); a V8 state from a build of
rounds 1–2 that wrote more than four hues without the count would be misread, which could not be checked
(those builds are inside the one commit); beta 1 refuses a V9 state as a whole (forward compatibility).

**1g WaveOverview — OK.** The only reader of the bytes, `ReadOverviewBytes`, checks
`size != sizeof(WaveOverview)` (with the RMS) before copying; `display_data.cpp` asserts the new size;
`DrawWave` bounds the RMS by the peaks (`std::min(0.84 * body / 32768.0, 0.84 * std::max(high, -low) / 32768.0)`),
so an unvalidated `rms` value cannot draw outside the column. `MakeOverview` guards `count > 0`.

**1h lifetime — OK except MS-R68.** `Later` and `AnimateLater` check `alive_` and `window_serial_`; the
offscreen and turned bitmaps are owned by the `MapView` and die with it; `ForgetViews` drops the skin;
`TurnedNow`'s raw `source` cannot dangle because `turned_nebula_`/the skin images hold the bitmap.

**2a frame cost** (MapView alone, 25 ms frames, software target; first frame = cache builds):

| view | scale | grains | SPEED | first | mean | p95 | max |
|---|---|---|---|---|---|---|---|
| GALAXY | 100 % | 5 | 1x | 76–106 | 17.1–17.4 | 40.2 | 45–47 |
| GALAXY | 100 % | 5 | 4x | 60–64 | 27.8–29.7 | 39–40 | 40–42 |
| GALAXY | 100 % | 20 | 1x | 66–68 | 37.1–37.9 | 60.0 | 62–66 |
| GALAXY | 100 % | 20 | 4x | 65–69 | 41.7–42.0 | 46–47 | 47–49 |
| GALAXY | 200 % | 5 | 1x | 74–82 | 18.5–19.0 | 40–42 | 46 |
| GALAXY | 200 % | 20 | 4x | 77–78 | 43.1 | 48 | 48–49 |
| WAVE | 100 % | 5 | 1x | 31–41 | 6.3 | 8.2–8.5 | 8–12 |
| WAVE | 100 % | 20 | 1x | 29–31 | 11.6–11.7 | 13.3–15.1 | 14.6–15.6 |
| WAVE | 200 % | 5 | 1x | 52–55 | 8.0 | 9.3–10.0 | 10–11 |
| WAVE | 200 % | 20 | 4x | 49–52 | 12.4 | 13.8–14.2 | 15–17 |

Where the time goes: the CPU turn of nebula and dust (MS-R63, MS-R64), the per-star gradients
(MS-R65), and in an open window the overlapping views (MS-R66). WAVE's waveform is cached well; its
first frame (26–55 ms) is the bitmap build.

**2b rebuilt although unchanged:** the star and grain paths/gradients (MS-R65); in WAVE `FillShares`
(veil, loop, spray: 3–6 paths of ~100 points) and the grid's `StrokeDown` paths, which only change with
the loop, SPRAY, the file or the size; the axis labels (`TrackedWidth` + `DrawTracked` per major tick,
character by character) and the panel's brand/version texts on every map repaint (MS-R66). Cached
correctly: the wave bitmap (file, skin, scale, size, colour), the hue-turned nebula (skin, turn, scale;
but see MS-R68), the dust masks, the turned bitmaps (0.2° steps). A cache for the static ring + axis +
grid would be invalidated by the file (seconds), the loop/spray (WAVE), the skin/HUE and the size.

**2c allocations per frame:** GALAXY 12 D2D objects per visible star; WAVE 10 per grain plus 2–6
`FillShares` paths, ~6–10 grid paths, 6 loop-edge paths and 2 playhead paths; each turn rebuild
allocates and zeroes a new `PixelBuffer` (`to.bytes.assign(...)`, 2 MB at 720²) and a new platform
bitmap (`MakeBitmap`), which Direct2D has to upload again. No offscreen context is made per frame.

**2d invalidation and timer:** see MS-R66. The VSTGUI timer runs while the editor is attached; on a
hidden window `invalid()` causes no paint, so the remaining cost is `callbacks_.poll` every 30 ms.

**2e several instances:** all windows share the host's UI thread, so MS-R63–R66 add up per window
(two GALAXY windows at 4x already exceed the frame budget on the turn alone); each window decodes its
own skin (MS-R70) and keeps its own turn buffers (~4–6 MB). No global state scales badly
(`g_dust_choice` is written only by the preview program).

## Not covered

Not run: Debug, VS 2022, ASan matrices; no host (Studio One) test, so on-screen hardware timings are not
measured; MS-R69 not clicked in a host. Out-of-scope code (engine, core, adapter, MIDI, processor,
loader thread) was not reviewed.

# WAVE view redesign ("Orbital Scope" plus elements of "Spectral Cloud"): feasibility assessment

Answer to `GPT_WAVE_REDESIGN_1.0.0-beta.2.md`, written 2026-11-21 against the Messier2 build of 13:17
(worktree `messier-beta2-117e35`, branch `claude/messier-beta2-117e35`, not committed). Read
only: no code changed, nothing built. File references are under `instruments/messier/source/`.

## Verdict

**Feasible, without architectural changes, in small steps.** The concept fits the current
renderer: the waveform is already drawn once into a bitmap cache, separate from everything
that moves per frame. A blurred depth layer, a crisp peak layer, colour gradients, an
amplitude texture and a nebula-tinted dome rim can all be built into that cache once per
file, skin, HUE and size. The frame cost stays about the same.

Only two items need a data change: **spectral colouring** (Spectral Cloud) and a **true
stereo waveform**. Both need a richer overview from the loader, which also changes the
message that carries the overview from processor to controller. They are optional and come
last.

## The six questions

### 1. Which renderer draws the waveform, and how is it cached?

- **Renderer:** VSTGUI 4 `CDrawContext`, Direct2D on Windows, in `MapView::DrawWave`
  (`map_view.cpp`).
- **What gets drawn:**
  - Each column of the file's overview (`WaveOverview`: 2048 columns of min, max and,
    since R4.6, RMS) is drawn as short vertical line pieces.
  - Every point goes through the fisheye mapping `lens::ToGlass(share, down)` (`fisheye.h`).
  - Each piece's alpha comes from the edge fades of R3.5: dark at both ends of the file and
    towards the peaks, bright in the middle and at the equator.
  - R4.6 adds two layers: translucent peaks (38 %), and over them a dense, brighter RMS body
    round the equator.
  - Then come a bright equator band (two strokes) and a soft central glow.
- **Cache:**
  - All of that goes **once** into an offscreen bitmap (`COffscreenContext`, `wave_cache_`).
  - The bitmap is made again only when its key changes: the overview, the skin, the device
    scale (in quarter steps), the glass size, or the wave colour `plate_wave`. HUE turns that
    colour, so a HUE change also rebuilds the cache.
  - A bitmap that could not be made is retried after 2 s at the earliest (MS-R47).
- **Per frame:** the cache is drawn as one bitmap, with alpha 0.62 × WAVE. Everything else is
  vector drawing on top, every frame (a timer of about 25 ms, so about 40 fps):
  - the veil outside the loop;
  - the grid and its crosses;
  - the loop and spray ranges and the loop edges;
  - the grains and their trails;
  - the playhead meridian, L/R, the dome and ring bitmaps, and the seconds axis.

### 2. Can the waveform geometry be reused for several passes?

Yes. The geometry is cheap and deterministic: per drawn column, its share of the file and
three heights (top peak, bottom peak, RMS), all mapped by `lens::ToGlass`. Today it is
computed inside the drawing loop. A small refactor would compute it once into a column list
per cache key, then run any number of passes over it:
- fill;
- outline;
- body;
- glow;
- texture.

Each pass can go into the same offscreen bitmap or into its own. Nothing in the data model
changes.

### 3. Can the diffuse layer be cached apart from the playhead?

Yes, and it already works this way. The waveform bitmap knows nothing of the playhead, the
loop or the grains; they are drawn over it per frame.

A blurred layer would be made **once per cache key**, on the CPU:
- **How:** either VSTGUI's own `BitmapFilter` box blur (`vstgui/lib/cbitmapfilter.h`, a CPU
  filter), or our own pass over the pixel buffer (`ReadPixels`/`MakeBitmap` in
  `gui_skin.cpp`, already used to turn the nebula bitmaps).
- **No per-frame cost:** no blur happens per frame, and no GPU effects or shaders are needed.
  Per frame it is one more `drawBitmap` of the glass size.

One detail: the blur radius must be set in window units, so that the depth layer looks the
same at 50 % and 200 %.

### 4. What would the Spectral Cloud texture need?

There are two parts.

- **An amplitude texture**, fine grain inside the body that follows loudness:
  - It needs **no new data**: it comes from the existing min, max and RMS per column.
  - A deterministic noise pattern, scaled by the column's RMS, is baked into the cache. The
    same file always gives the same picture.
  - It does not drift. A drifting texture would need a per-frame pass, which the brief rules
    out.
- **Spectral colour**, a tint that follows the sound's brightness or timbre per column:
  - It **needs new data from the loader**: per column a brightness value, for example 8 bits
    for 2048 columns, which adds 2 KB to the overview message.
  - The cheapest proxy is the zero-crossing rate, or the energy ratio after a one-pole
    high-pass. Neither needs an FFT.
  - The loader computes the overview off the audio thread when a file loads. Ten minutes of
    stereo take 56 ms today; a zero-crossing count adds little.
  - Changes needed:
    - `WaveOverview` gets one field;
    - `MakeOverview` computes it;
    - the `static_assert` on the message size is updated;
    - the loader test compares the new values (as for the RMS in R4.6).
  - Processor and controller always ship together, so the larger message is safe. It is
    still a change to the data path and must be tested as one.
  - No DSP or audio change.

### 5. What is the impact on frame rate and on scaling (50 to 200 %)?

- **Frame rate:**
  - Practically none per frame: one or two more full-glass `drawBitmap` calls per layer
    beyond the waveform. The vector overlays stay as they are.
  - The costly part is the **cache rebuild**: multiple passes plus a blur. At 2x device
    scaling the glass is about 720 × 720 pixels, at 200 % zoom on a 2x display about
    1440 × 1440.
  - A box blur over 1440² pixels costs a few tens of ms, once per key.
- **Where the rebuild becomes visible:**
  - **Dragging HUE:** today every HUE step rebuilds the cache. With a blur this could
    stutter while dragging.
    - Mitigation: bake the layers neutral (white with alpha) and only tint them, which is
      cheap.
    - Alternatively, rebuild after HUE has rested for about 100 ms and draw the old bitmap
      meanwhile.
  - **Resizing the window** with the grip: the cache is made again at each quarter step of
    the scale.
    - Mitigation: build the blurred layer at a capped resolution. A blurred image can be
      scaled up without visible loss.
- **Memory:** each cached layer is width × height × 4 bytes, about 2 MB at 720² and 8 MB at
  1440². With three layers it stays well under 30 MB.
- **Scaling 50 to 200 %:** everything is drawn in window units and cached per device scale,
  so lines stay sharp at every size. Only the blur radius and the texture grain must be
  given in window units, so they look the same at 50 % and 200 %.

### 6. What can be done without architectural changes?

Everything in the concept except spectral colour and true stereo:

| Effect from the concept | How | Data change |
|---|---|---|
| Precise peak layer (crisp transients) | A 1 px outline of the top and bottom peaks over the translucent fill | none |
| Soft, blurred depth layer behind it | A CPU-blurred copy of the peak fill, slightly taller, low opacity, cached | none |
| Bright equator line | Exists since R3.5; can be made thinner and brighter, and run across the whole glass as in the concept | none |
| Layered body (RMS) and translucent peaks | Exists since R4.6 | RMS since R4.6 |
| Colour accents from the nebula and HUE | Colours from the skin's `nebula_a`/`nebula_b` and `plate_wave`, which already follow HUE and the preset | none |
| Colour gradient over amplitude (body bright and warm, peaks cooler) | Per piece, in the existing pieces loop | none |
| Fine amplitude texture | Baked noise, scaled by RMS | none |
| Nebula-tinted dome rim (the warm clouds at the rim in the concept image) | The chosen preset's nebula bitmap, drawn in WAVE through a ring mask (about r 140 to 176) at low opacity; still, or turning with the existing CPU rotation | none |
| Spectral colouring (Spectral Cloud) | A per-column brightness value from the loader | **overview + message** |
| Stereo in the waveform (L above, R below) | A per-channel overview; today it merges the channels | **overview + message** |

Note on "preserve stereo information": today the **waveform** has no stereo. The overview
merges both channels, and the vertical axis means pan only for the grains (R3.6). Showing
stereo in the waveform itself would be a new feature, not something to keep, and it would
make the shape asymmetric about the equator, unlike the concept image. I suggest leaving it
out.

## Risks

- **Readability:**
  - A blurred layer and texture can soften transients, and the loop and playhead must stay
    clear.
  - Mitigation: the crisp outline is drawn last in the cache. The veil outside the loop and
    the playhead stay as overlays on top.
  - Check each step at 50 % and with a busy file.
- **HUE and resize stutter** from cache rebuilds: see question 5. Introduce the neutral bake
  or the delayed rebuild together with the blur.
- **Too much at once:** the concept has many effects. Taken together they can look muddy, and
  the GALAXY view could lose its contrast to WAVE. Hence one effect per step, each tested by
  the user.
- **Data path** (only the optional steps): an overview field changes the processor-to-
  controller message. It is covered by the loader and display tests, as for the RMS of R4.6.
- **The Direct2D clip bug** affects only turned bitmaps. The waveform never turns. The
  nebula rim would use the CPU rotation we already have.
- **Automated tests** can check data, cache keys and layout, not the look. The look is the
  user's DAW and preview test, as before.

## Proposed rendering architecture

```text
per cache key (overview, skin and HUE colours, device scale, glass size):
  columns = [ {share, top, bottom, body, brightness?} ... ]  mapped once by lens::ToGlass
  layer "depth"  : peak fill, blurred (CPU, radius in window units), capped resolution
  layer "wave"   : translucent peaks + RMS body + amplitude gradient + texture (+ spectral tint)
  layer "detail" : crisp peak outline + equator line + central glow
  layer "rim"    : nebula of the preset through a ring mask (WAVE only; per preset, HUE)
per frame (unchanged order):
  glass -> rim -> depth -> wave -> detail (each drawBitmap, alpha x WAVE) ->
  veil outside the loop -> grid -> loop/spray -> grains -> playhead -> dome -> ring -> axis
```

- **One cache owner:** a small `WaveLayers` holder in `MapView`, with the key and the up to
  four bitmaps. It replaces the single `wave_cache_`.
- **WAVE as brightness:** stays one factor. It can weigh the layers differently, so that the
  depth layer does not grow too strong at 200 %.

## Incremental plan (each step a testable build; what the user sees)

| Step | What changes | What the user sees | Tests |
|---|---|---|---|
| 0 | Refactor only: the column list computed once; the cache split into named layers | Nothing (the same picture as 13:17) | Cache keys and layer count; previews identical |
| 1 | Crisp peak outline over the translucent fill | Sharper contour, transients readable as in the concept | Preview at 50/100/200 % |
| 2 | Blurred depth layer (cached, capped resolution) and neutral bake or delayed rebuild for HUE | A soft halo round the waveform, more depth; HUE drags smoothly | Preview, a HUE drag in the host |
| 3 | Colour gradient over amplitude from the skin's colours | More colour dynamics: a bright, warm core and cooler, darker peaks, following HUE and the preset | Previews with four presets and HUE |
| 4 | Nebula-tinted dome rim in WAVE | Coloured clouds at the glass rim as in the concept, matching the chosen galaxy | Previews per preset |
| 5 | Amplitude texture inside the body | A fine "cloud" grain in loud passages | Preview, a busy and a quiet file |
| 6 (optional) | Spectral brightness per column (loader, overview, message) | The colour follows the timbre: darker for deep sounds, lighter for bright ones | Loader test for the new values, message size, load time |
| 7 (optional, advised against) | Per-channel waveform | L above and R below the equator | Overview and message tests |

Steps 1 to 5 need no data change and can each be tried in a day's test round. Step 6 comes
only once 1 to 5 please the user.

## Compared with R4.6 (what is built already)

**Already covered:**
- the layered representation: a dense, bright RMS body and translucent peaks (R4.6);
- the edge-on galaxy look: fades at the ends and towards the peaks, a bright equator band and
  a central glow (R3.5);
- colours that follow HUE (`plate_wave`);
- the spherical mapping, grid, L/R, loop, veil, playhead and grains as in the concept.

**Not covered yet:**
- the soft blurred depth layer (the concept's tall, misty halo above and below the wave);
- a crisp precise peak outline;
- the colour gradient over amplitude and the nebula accents;
- the nebula-tinted rim of the dome;
- the fine texture;
- spectral colour;
- a thin, bright equator line across the whole glass. The concept draws one; ours fades
  towards the ends since R3.5, which the user decided.

So R4.6 is the first of the concept's layers. Steps 1 to 4 bring most of the visible
difference.

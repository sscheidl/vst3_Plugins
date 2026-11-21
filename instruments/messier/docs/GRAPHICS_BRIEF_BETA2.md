# Graphics brief: bitmaps for the Messier 1.0 beta 2 window

Date: 2026-11-20. For an image model (GPT with image generation). Claude checks the delivered
images and builds them into the plug-in. Supersedes [GRAFIKAUFTRAG_GUI.md](GRAFIKAUFTRAG_GUI.md)
(beta 1) for everything below.

## What changed since beta 1

* **One design only.** There are no GUI presets any more (no Slate, Silver, Plum, Gold). The
  window is dark graphite and black metal. Colour comes from the plug-in's HUE control,
  which tints the nebula and a few accents (red over violet to blue). **All surfaces must
  therefore be colour-neutral**: graphite, black, steel grey. No violet, blue or warm cast
  in metal, glass, knobs or buttons.
* **A round glass in the middle** replaces the wide rectangular plate. A spiral nebula turns
  slowly behind it (view GALAXY), or the waveform lies on it (view WAVE, drawn by code).
* **Nebula variants instead of presets.** The user picks one of four objects for the glass.
  Each is delivered as two white layers that the plug-in tints and turns.

The user's render of 2026-11-20 is the visual reference for mood and material (dark brushed
graphite, black ribbed knob caps, a deep round window with a violet spiral nebula). It is a
draft, not a pixel template. Sizes and positions below are binding.

## Rules for every image

* **No text**, letters, numbers, logos or brand names. No scales, ticks, pointers or values.
* **No controls that do not exist**: no switches, LEDs, meters, menus, sliders, screws other
  than those listed.
* **No point-like stars or small bright dots** anywhere, also not in the nebulae: every dot
  on the glass is a sounding grain, drawn by the plug-in. Nothing bright smaller than 6 px
  across at 1x.
* **Not in the style of any existing product, manufacturer or artist**, and not a copy or
  trace of any specific photograph (telescope images included). Describe and paint in your
  own way.
* **Light from the top left**, soft, no hard specular hot spots.
* **PNG, sRGB, 8 bits per channel.** RGBA where the table says alpha, else RGB.
  Straight (not premultiplied) alpha. Transparent pixels may have any RGB.
* **Every image in two sizes**: `@1x` at the size given and `@2x` at exactly twice that,
  composed natively at that size (finer, not upscaled). The plug-in uses @2x from 125 %
  zoom on.
* **File names** exactly as given: `messier_<name>@1x.png` and `messier_<name>@2x.png`.
* Everything that carries information (texts, scales, value arcs, pointers, ticks, the
  seconds axis, the waveform, grains, loop, playhead, the GALAXY/WAVE switch) is drawn by
  the plug-in. Paint only surfaces and materials.

## Coordinates

All in window units at 1x (= pixels of the @1x images), origin top left.

* Window width **880**. Heights: **624** (base), **720** with DETAILS open, **804** with
  DETAILS and the keyboard open. With a section closed, the plug-in simply crops the chassis
  at the bottom.
* Header: 0–52 (a fine rule at y 52 is drawn by code).
* **Glass centre (440, 272)**, glass radius **180** (diameter 360), ring from radius 180 to
  **196**. The seconds axis labels sit just outside the ring on its upper arc (code).
* Primary knob centres (72 × 72 each): (177, 128), (142, 238), (150, 348), (205, 458) on the
  left; (703, 128), (738, 238), (730, 348), (675, 458) on the right; (440, 490) below the
  glass.
* START row: y 566–598 (slider track x 90–564, value box x 576–680), FREEZE x 692–766,
  RESET x 778–852, buttons y 569–595.
* Info line: y 602–624. DETAILS section: y 624–720. Keyboard section: y 720–804 (or 624–708
  when DETAILS is closed).

## Images

| No. | Name | Size @1x | Alpha | Content |
| --- | --- | --- | --- | --- |
| 1 | `chassis` | 880 × 804 | no | The whole front panel. See below. |
| 2 | `ring` | 432 × 432 | yes | The metal ring around the glass, centred in the canvas. See below. |
| 3 | `glass` | 360 × 360 | yes | The dark glass disc behind everything on the plate. See below. |
| 4 | `dome` | 360 × 360 | yes | Faint reflections of a slightly domed glass, drawn over everything on the plate. See below. |
| 5 | `nebula_<variant>_arms`, `nebula_<variant>_haze` | 360 × 360 | yes | The nebula, two layers per variant, four variants (8 images). See below. |
| 6 | `knob_l` | 72 × 72 | yes | Large knob cap, 48 across, centred. See below. |
| 7 | `knob_s` | 48 × 48 | yes | Small knob cap, 30 across, centred. Same make as `knob_l`. |
| 8 | `thumb` | 24 × 24 | yes | Grip of the START slider: a small round metal dome, 22 across, centred. |
| 9 | `button`, `button_down` | 48 × 28 | yes | Key face as a nine-part image (borders 8 px, corners radius 5): raised and pressed. No text. |
| 10 | `screw` | 14 × 14 | yes | Optional. A small dark countersunk screw with a hex or slot head, seen from the front. |
| 11 | `pad`, `pad_lit` | 40 × 40 | yes | Optional, for a touch-pad keyboard (only if the user picks it). Nine-part, borders 10 px, corner radius 6: a flat, slightly recessed pad of matt black glass; `pad_lit` the same with a soft white inner glow (the plug-in tints it). |

40 images per size with the optional ones (36 without), 80 files in all.

### 1. chassis (880 × 804, RGB)

* Dark graphite metal, about `#1B1D22` to `#24272D`, very fine horizontal brushing, faint
  irregular wear, a soft vignette towards the outer edges. Neutral grey: no colour cast.
* Around the glass, a **shallow circular well**: a soft, wide depression centred at (440, 272)
  from radius 196 out to about 214, darker towards the ring, as if the ring were set into the
  panel. Nothing else is recessed or raised.
* No rules, lines, frames, panels, grooves, holes or screws (screws come from image 10).
* Below y 560 the surface is plain and even: the START row, the info line, DETAILS and the
  keyboard are drawn by code on it, and the bottom is cropped at 624, 708 or 720 depending
  on what is open. No edge, bevel or shadow along any horizontal line below y 560.

### 2. ring (432 × 432, RGBA)

* Centred in the canvas: its centre is the canvas centre (216, 216).
* **Fully transparent inside radius 180** (alpha 0 for r < 179.5, anti-aliased at 180): the
  glass shows through.
* The ring proper from radius 180 to 196: turned dark steel, fine concentric lathe lines,
  a bevelled inner edge (a thin dark seam right at r 180) and a slightly brighter outer
  edge at r 196. Lit from the top left, so its top-left quarter is a little brighter.
* Outside radius 196: only a soft drop shadow onto the panel, black, alpha at most 35 %,
  fading to 0 by radius 214 and offset 2 px to the bottom right. Alpha 0 from radius 214 on.
* No ticks, numbers, marks or screws: the seconds axis and the position marker are drawn on
  it by code.

### 3. glass (360 × 360, RGBA)

* A disc filling the canvas: opaque (alpha 255) inside radius 179, anti-aliased edge at 180,
  alpha 0 outside.
* Very dark, neutral blue-black, about `#0A0C12`, with extremely fine grain and a slight
  darkening towards the rim (the last 30 px). Uneven only at the level of a few percent.
* Completely empty: no stars, dots, grid, scratches, reflections or nebula.

### 4. dome (360 × 360, RGBA)

* White (RGB 255) only; all shape in alpha. Alpha 0 outside radius 180.
* One soft, curved reflection band near the upper left rim (as of a window light on a
  slightly domed glass), alpha at most 18 %; a fainter counter-reflection near the lower
  right rim, at most 8 %; a hairline highlight just inside the rim, at most 12 %.
* The middle of the glass (radius < 110) stays clear: alpha at most 4 % there. The waveform
  and the grains must stay readable through it.

### 5. nebula variants (360 × 360 each, RGBA)

Four variants; `<variant>` is one of `spiral`, `barred`, `ring`, `cloud`.

| Variant | Object (in own words) | arms layer | haze layer |
| --- | --- | --- | --- |
| `spiral` | A grand-design spiral galaxy with two well-defined arms | the two arms, winding about 1.5 turns, with dark lanes between them and a soft, bright, round core | a broad, soft glow around the core and along the arms, wider and smoother than the arms |
| `barred` | A barred spiral galaxy | a straight, soft central bar (about a third of the diameter) with an arm starting from each end and curling about one turn | as for `spiral`, a little elongated along the bar |
| `ring` | A planetary ring nebula, seen face on | a bright, slightly elliptic, textured annulus (inner radius about 55, outer about 115) with filaments; the inside darker but not empty; no central point | a faint diffuse halo out to about radius 165, with a few soft lobes |
| `cloud` | A diffuse emission nebula | irregular bright clumps and wisps, crossed by dark dust lanes, no rotational symmetry | a large, soft, cloudy background glow with gentle variations |

For every nebula image:

* **RGB pure white (255, 255, 255) everywhere; all structure in the alpha channel.** The
  plug-in tints the arms layer with one colour and the haze with another, both following
  HUE. Brightness = alpha.
* Centre of the object at the canvas centre. **Alpha 0 for every pixel beyond radius 176**,
  fading out smoothly from about radius 140: the layers are turned at any angle, and nothing
  may reach the ring or show the square's corners.
* Alpha ranges: the brightest part (a core or the brightest clump) at most 85 %; arms and
  filaments 25–65 %; dark lanes and gaps 0–10 %; the haze layer at most 45 % anywhere.
* Soft, gaseous, with fine filaments; **no stars, no points, no hard edges**. The two layers
  of a variant fit each other when not turned (the plug-in turns them at slightly different
  speeds, so they drift against each other slowly).

### 6. and 7. knob caps

* A black anodised (`#121316` to `#1C1D21`) turned cap seen from straight above: concentric
  lathe grooves on the top face, a finely ribbed (knurled) side ring, a soft top-left
  highlight and a faint contact shadow round the cap (alpha at most 40 %, within 3 px).
* `knob_l`: cap diameter 48 centred in 72 × 72; `knob_s`: diameter 30 centred in 48 × 48.
  Everything outside the cap and its 3 px shadow is alpha 0: the plug-in draws the scale
  ticks (radius about 34 on the large knob) and the coloured value arc (radius 28) there.
* **Rotationally symmetric**: turned by 90° it looks the same (apart from the light). No
  pointer, dot, notch or marking: the pointer is drawn by the plug-in.

### 8. thumb, 9. buttons

* `thumb`: a small round dome of satin steel, 22 across centred in 24 × 24, soft highlight top
  left, contact shadow within the canvas, alpha 0 at the corners.
* `button`: a dark graphite key face (`#23252B`), rounded corners (radius 5), a 1 px lighter
  top edge, a 1 px darker bottom edge; `button_down`: the same pressed in (darker, the light
  edge at the bottom instead). Nine-part: the plug-in stretches the 8 px borders and the
  middle, so the middle must be even. Alpha 0 outside the rounded rectangle.

## Second delivery (2026-11-20, after the user's DAW test)

The nebula stands still now; dark clouds drift over it. Two more kinds of image:

| No. | Name | Size @1x | Alpha | Content |
| --- | --- | --- | --- | --- |
| 12 | `nebula_<variant>` (`nebula_spiral`, `nebula_barred`, `nebula_ring`, `nebula_cloud`) | 360 × 360 | yes | The nebula **in full colour**, as in the user's four examples of 2026-11-20 (a barred spiral, a ring, a cloud with a dark lane, a spiral): violet and magenta, a soft lighter core where the object has one. The plug-in turns its hue with HUE and keeps saturation and lightness. **Transparent background**, no white: alpha 0 beyond radius 176, fading out from about 150. No stars or points. |
| 13 | `dust` | 360 × 360 | yes | Dark, diffuse dust clouds for drifting over the nebula: **white** (the plug-in tints it dark), structure in alpha, at most 60 %, soft lanes and patches, alpha 0 beyond radius 176. It moves a little and fades in and out, so it must not have hard edges or a recognisable centre. |

The arms and haze layers of the first delivery stay as the fallback.

## Delivery

* One ZIP with the 80 (or 72) PNGs and a `SHA256SUMS.txt`.
* In addition, **for review only**: one mock-up PNG of the whole window (880 × 804) put
  together from the images, with a nebula and invented labels where the plug-in draws text.
  Text is allowed only in this mock-up.

## Acceptance by Claude

Checked by script and by eye before anything is built in:

* Sizes exact, @2x exactly twice @1x and not an upscaled @1x.
* Alpha: `ring` 0 inside r 179.5 and beyond r 214; `glass` and `dome` 0 beyond r 180;
  every nebula layer 0 beyond r 176; knob caps 0 outside cap plus 3 px.
* Dome: alpha at most 4 % within r 110; haze at most 45 %; arms at most 85 %.
* Nebulae: RGB white wherever alpha > 0; no bright feature under 6 px (a local-maximum check).
* Knob caps: a 90° turn differs only in the light.
* Neutral colour: the mean saturation of chassis, ring, glass, caps, thumb and buttons below
  8 %.
* Contrast: the plug-in's texts (light grey `#F2F1EF` and dim `#B4BED0`) stay readable on the
  chassis, at least 4.5 : 1 behind every text place.
* Size: all images together under 12 MB in the bundle.

## Notes

* **Copyright:** protection of AI-made images is uncertain. Using them is usually allowed by
  the terms of use; others might in some cases use them freely too. If the user wants
  protection of their own, the final versions should be reworked by hand.
* **Scaling:** @2x covers up to 200 % on a normal display. At 200 % on a high-resolution
  display @2x is upscaled a little; the nebulae tolerate that. If it shows on the metal, a
  `@3x` set can follow.
* **Custom skins** use the same names in the `Messier-Skin` folder; the plug-in writes the
  list of names and sizes into that folder.

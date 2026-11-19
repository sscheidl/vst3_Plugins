"""Makes the window's metal bitmaps of a GUI preset: the knob caps (large and small), the
START slider's thumb and the key faces (raised and pressed in), at 1x and 2x.

Interim versions (0.2.2, the user's decision of 2026-11-19): drawn by this script until
final renderings arrive. The same parameters always give the same files (fixed seeds, no
time stamps), so the bitmaps in resource/gui can be checked against the script.

    python instruments/messier/tools/make_metal_bitmaps.py --preset slate \
        --out instruments/messier/resource/gui

Each preset is a set of parameters below (PRESETS); another preset's metal is another
entry. The logical sizes are those gui_skin.cpp expects (docs/GRAFIKAUFTRAG_GUI.md):
knob_l 72 x 72 with a cap of 48, knob_s 48 x 48 with a cap of 30, thumb 16 x 28,
button and button_down 48 x 28 (nine parts, edge 8). Needs numpy and Pillow.
"""

import argparse
import math
import pathlib

import numpy as np
from PIL import Image

SUPERSAMPLE = 4  # drawn this much larger, then reduced: smooth edges

PRESETS = {
    "slate": {
        # Warm satin silver for the caps and the thumb, the small caps a step darker.
        "knob_large": {"base": (208, 204, 196), "rings": 0.035, "sheen": 0.10, "dome": 0.07, "seed": 11},
        "knob_small": {"base": (186, 182, 174), "rings": 0.035, "sheen": 0.09, "dome": 0.07, "seed": 12},
        "knob_rim": {"light": 0.16, "outline": (34, 34, 33), "knurl": 0.05},
        "shadow": 0.38,
        "thumb": {"base": (200, 196, 188), "index": (44, 44, 46), "seed": 13},
        # The keys: dark satin metal; pressed in, darker, with a shadow under the upper
        # edge and a lit lower edge.
        "button": {"base": (68, 72, 74), "outline": (14, 16, 17), "seed": 14},
        "button_down": {"base": (43, 46, 48), "outline": (12, 13, 14), "lit": (196, 204, 236), "seed": 15},
    },
}


def clamp01(a):
    return np.clip(a, 0.0, 1.0)


def smooth_noise(rng, n, width):
    """1-D noise of n values, smoothed over about `width` samples, mean 0, peak about 1."""
    raw = rng.standard_normal(n + 4 * width)
    kernel = np.hanning(max(3, 2 * width + 1))
    kernel /= kernel.sum()
    out = np.convolve(raw, kernel, mode="same")[2 * width:2 * width + n]
    return out / (np.abs(out).max() + 1e-9)


def to_image(rgb, alpha, size):
    """Floats (0..1) at SUPERSAMPLE x size to an RGBA image of `size`."""
    rgba = np.dstack([clamp01(rgb), clamp01(alpha)[..., None]])
    big = Image.fromarray((rgba * 255.0 + 0.5).astype(np.uint8), "RGBA")
    return big.resize(size, Image.LANCZOS)  # Pillow reduces RGBA with premultiplied alpha


def knob(units, cap_units, p, rim, shadow, scale):
    n = units * scale * SUPERSAMPLE
    px = np.arange(n) + 0.5
    x, y = np.meshgrid(px, px)
    c = n / 2.0
    radius = cap_units / 2.0 * scale * SUPERSAMPLE
    dx, dy = x - c, y - c
    r = np.hypot(dx, dy) / radius  # 1 at the cap's edge
    theta = np.arctan2(dy, dx)
    rng = np.random.default_rng(p["seed"])

    base = np.array(p["base"], float) / 255.0
    # Lathe rings: fine brightness changes along the radius, the same all round.
    ring_count = 2048
    ring_noise = smooth_noise(rng, ring_count, 2) * 0.6 + smooth_noise(rng, ring_count, 9) * 0.4
    rings = ring_noise[np.clip((r * (ring_count - 1)).astype(int), 0, ring_count - 1)]
    # The turned top's sheen: two bright sectors towards the light (top left) and away.
    sheen = np.cos(2.0 * (theta + math.radians(135.0)))
    shade = 1.0 + p["rings"] * rings + p["sheen"] * sheen - p["dome"] * r**2
    rgb = base[None, None, :] * shade[..., None]

    # The rim: a bevel lit from the top left, fine knurling, a dark outline.
    rim_zone = clamp01((r - 0.86) / 0.06)
    light = -(dx * 1.0 + dy * 1.0) / (np.hypot(dx, dy) + 1e-9) / math.sqrt(2.0)
    knurl = np.cos(theta * (90 if cap_units > 40 else 60))
    bevel = 1.0 + rim["light"] * light * rim_zone + rim["knurl"] * knurl * rim_zone
    rgb = rgb * bevel[..., None]
    outline = np.array(rim["outline"], float) / 255.0
    edge = clamp01((r - 0.955) / 0.045)
    rgb = rgb * (1.0 - edge[..., None]) + outline[None, None, :] * edge[..., None]

    feather = 1.0 / radius  # one supersampled pixel
    cap_alpha = clamp01((1.0 - r) / feather + 0.5)
    # A short soft shadow below, inside the scale's ring (the value arc lies further out).
    sy = (y - c - 0.8 * scale * SUPERSAMPLE)
    rs = np.hypot(dx, sy) / radius
    shadow_alpha = shadow * clamp01((1.08 - rs) / 0.10) ** 2
    alpha = cap_alpha + shadow_alpha * (1.0 - cap_alpha)
    # Premultiplied mix of the cap over black shadow, then back to straight alpha.
    premul = rgb * cap_alpha[..., None]
    out_rgb = np.where(alpha[..., None] > 1e-6, premul / np.maximum(alpha, 1e-6)[..., None], 0.0)
    return to_image(out_rgb, alpha, (units * scale, units * scale))


def thumb(p, scale):
    w_units, h_units = 16, 28
    s = scale * SUPERSAMPLE
    w, h = w_units * s, h_units * s
    x, y = np.meshgrid(np.arange(w) + 0.5, np.arange(h) + 0.5)
    rng = np.random.default_rng(p["seed"])
    u = x / w  # 0..1 across
    base = np.array(p["base"], float) / 255.0
    # A rounded bar: light from the left, darker to the right, a fine grain down its length.
    across = 1.0 + 0.10 * np.cos((u - 0.32) * math.pi * 1.25) - 0.05
    grain = smooth_noise(rng, w, 2)[None, :] * 0.025
    rgb = base[None, None, :] * (across + grain)[..., None]
    # Light upper edge, dark lower edge.
    v = y / h
    rgb = rgb * (1.0 + 0.10 * clamp01((0.08 - v) / 0.08) - 0.18 * clamp01((v - 0.9) / 0.1))[..., None]
    # Grip ridges across the middle third.
    ridge = (np.sin(y / s * 2 * math.pi / 2.5) > 0.55) & (v > 0.30) & (v < 0.70)
    rgb = rgb * np.where(ridge, 0.90, 1.0)[..., None]
    # The index line down the middle, where START stands.
    index = np.array(p["index"], float) / 255.0
    line = clamp01(1.0 - np.abs(x - w / 2) / (0.6 * s))
    rgb = rgb * (1.0 - line[..., None]) + index[None, None, :] * line[..., None]
    alpha = rounded_alpha(x, y, w, h, 2.5 * s, s)
    rgb = outline(rgb, x, y, w, h, 2.5 * s, s, np.array((30, 30, 30), float) / 255.0, 0.8)
    return to_image(rgb, alpha, (w_units * scale, h_units * scale))


def rounded_distance(x, y, w, h, radius):
    """Signed distance to the outline of a rounded rectangle, negative inside."""
    qx = np.abs(x - w / 2) - (w / 2 - radius)
    qy = np.abs(y - h / 2) - (h / 2 - radius)
    outside = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0))
    inside = np.minimum(np.maximum(qx, qy), 0)
    return outside + inside - radius


def rounded_alpha(x, y, w, h, radius, s):
    return clamp01(0.5 - rounded_distance(x, y, w, h, radius) / (0.5 * s))


def outline(rgb, x, y, w, h, radius, s, colour, strength):
    d = rounded_distance(x, y, w, h, radius)
    ring = clamp01(1.0 - np.abs(d + 0.5 * s) / (0.55 * s)) * strength
    return rgb * (1.0 - ring[..., None]) + colour[None, None, :] * ring[..., None]


def button(p, scale, down):
    w_units, h_units = 48, 28
    s = scale * SUPERSAMPLE
    w, h = w_units * s, h_units * s
    x, y = np.meshgrid(np.arange(w) + 0.5, np.arange(h) + 0.5)
    rng = np.random.default_rng(p["seed"])
    base = np.array(p["base"], float) / 255.0
    yu = y / s  # window units from the top
    # Brushed along its length (the nine parts stretch the centre sideways, so the grain
    # runs that way), a gentle light from above.
    brushed = smooth_noise(rng, h, 1)[:, None] * 0.03
    if down:
        light = -0.04 + 0.05 * (yu / h_units)  # darker at the top: lying lower
    else:
        light = 0.07 - 0.12 * (yu / h_units)
    rgb = base[None, None, :] * (1.0 + light + brushed)[..., None]
    d = rounded_distance(x, y, w, h, 3.5 * s) / s  # window units, negative inside
    depth = -d
    if down:
        # Pressed in: a shadow under the upper edge and along the sides, a lit lower edge.
        top_shadow = clamp01(1.0 - yu / 5.0) ** 1.6 * 0.55
        side_shadow = clamp01(1.0 - depth / 3.0) ** 2 * 0.30
        rgb = rgb * (1.0 - np.maximum(top_shadow, side_shadow))[..., None]
        lit = np.array(p["lit"], float) / 255.0
        lower = clamp01(1.0 - np.abs(yu - (h_units - 2.1)) / 1.1) * clamp01((depth - 1.0) / 1.2) * 0.62
        rgb = rgb * (1.0 - lower[..., None]) + lit[None, None, :] * lower[..., None]
    else:
        # Raised: a light upper edge, a darker lower one.
        upper = clamp01(1.0 - np.abs(yu - 1.6) / 0.8) * clamp01((depth - 1.0) / 1.5) * 0.16
        rgb = rgb + upper[..., None]
        lower = clamp01(1.0 - np.abs(yu - (h_units - 1.6)) / 0.9) * 0.22
        rgb = rgb * (1.0 - lower[..., None])
    rgb = outline(rgb, x, y, w, h, 3.5 * s, s, np.array(p["outline"], float) / 255.0, 0.9)
    alpha = rounded_alpha(x, y, w, h, 3.5 * s, s)
    return to_image(rgb, alpha, (w_units * scale, h_units * scale))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--preset", default="slate", choices=sorted(PRESETS))
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()
    p = PRESETS[args.preset]
    args.out.mkdir(parents=True, exist_ok=True)
    for scale in (1, 2):
        images = {
            "knob_l": knob(72, 48, p["knob_large"], p["knob_rim"], p["shadow"], scale),
            "knob_s": knob(48, 30, p["knob_small"], p["knob_rim"], p["shadow"], scale),
            "thumb": thumb(p["thumb"], scale),
            "button": button(p["button"], scale, False),
            "button_down": button(p["button_down"], scale, True),
        }
        for stem, image in images.items():
            path = args.out / f"messier_{args.preset}_{stem}@{scale}x.png"
            image.save(path, optimize=True)
            print(path.name, image.size)


if __name__ == "__main__":
    main()

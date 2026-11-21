"""Acceptance check of the Messier beta 2 bitmaps (docs/GRAPHICS_BRIEF_BETA2.md).

    python -I tools/check_gui_bitmaps.py <folder>

<folder> holds the delivered PNGs (messier_<name>@1x.png and @2x.png). Prints one line per
check and a summary; exit code 0 if nothing failed. Optional images (screw, pad, pad_lit)
may be missing. Needs Pillow and NumPy. Reads only; changes nothing.
"""

import hashlib
import sys
from pathlib import Path

import numpy as np
from PIL import Image

# name: (width, height at 1x, alpha?, optional?)
IMAGES = {
    "chassis": (880, 804, False, False),
    "ring": (432, 432, True, False),
    "glass": (360, 360, True, False),
    "dome": (360, 360, True, False),
    "knob_l": (72, 72, True, False),
    "knob_s": (48, 48, True, False),
    "thumb": (24, 24, True, False),
    "button": (48, 28, True, False),
    "button_down": (48, 28, True, False),
    "screw": (14, 14, True, True),
    "pad": (40, 40, True, True),
    "pad_lit": (40, 40, True, True),
    # The dust masks (the experiment of 2026-11-20): black, the shape in alpha.
    "dust_wisps_dark": (360, 360, True, True),
    "dust_veil_dark": (360, 360, True, True),
    "dust_bands_dark": (360, 360, True, True),
    "dust_clouds": (360, 360, True, True),  # the second layer: white, alpha only
}
for variant in ("spiral", "barred", "ring", "cloud"):
    # Second delivery (the user's wish of 2026-11-20): each nebula in colour, standing.
    IMAGES[f"nebula_{variant}"] = (360, 360, True, True)
    for layer in ("arms", "haze"):
        IMAGES[f"nebula_{variant}_{layer}"] = (360, 360, True, False)
for variant in ("blue", "orange", "red", "violet"):
    # Round 3 (2026-11-21): the painted galaxies, one GUI preset each, in colour only.
    IMAGES[f"nebula_{variant}"] = (360, 360, True, False)

NEUTRAL = ("chassis", "ring", "glass", "knob_l", "knob_s", "thumb", "button", "button_down")
TEXT_COLOURS = ((0xF2, 0xF1, 0xEF), (0xB4, 0xBE, 0xD0))

failures = 0
warnings = 0


def report(ok, what, detail="", warn_only=False):
    global failures, warnings
    if ok:
        print(f"  ok    {what}")
    elif warn_only:
        warnings += 1
        print(f"  WARN  {what}: {detail}")
    else:
        failures += 1
        print(f"  FAIL  {what}: {detail}")


def radius_grid(width, height, scale):
    """Distance of each pixel centre from the canvas centre, in 1x units."""
    y, x = np.mgrid[0:height, 0:width]
    return np.hypot(x + 0.5 - width / 2, y + 0.5 - height / 2) / scale


def max_alpha(alpha, mask):
    return float(alpha[mask].max()) / 255 if mask.any() else 0.0


def luminance(rgb):
    def channel(c):
        c = c / 255
        return np.where(c <= 0.03928, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)

    r, g, b = (channel(rgb[..., k].astype(float)) for k in range(3))
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def check(folder):
    total_bytes = 0
    for name, (w, h, has_alpha, optional) in IMAGES.items():
        files = {s: folder / f"messier_{name}@{s}x.png" for s in (1, 2)}
        if not files[1].exists() and not files[2].exists():
            report(optional, f"{name}", "missing", warn_only=optional)
            continue
        print(name)
        loaded = {}
        for scale, file in files.items():
            if not file.exists():
                report(False, f"@{scale}x present", "missing")
                continue
            total_bytes += file.stat().st_size
            image = Image.open(file)
            report(image.size == (w * scale, h * scale), f"@{scale}x size", f"{image.size}, want {(w * scale, h * scale)}")
            report(image.mode == ("RGBA" if has_alpha else "RGB") or (not has_alpha and image.mode == "RGBA"),
                   f"@{scale}x mode", image.mode)
            loaded[scale] = image.convert("RGBA")
        if 1 in loaded and 2 in loaded and loaded[2].size == (w * 2, h * 2):
            # Not an upscaled @1x: the @2x halved must not equal the @2x's own detail.
            small = np.asarray(loaded[2].resize((w, h), Image.LANCZOS).resize((w * 2, h * 2), Image.BICUBIC), float)
            big = np.asarray(loaded[2], float)
            detail = np.abs(big - small)[..., :3].mean()
            report(detail > 0.6, "@2x has its own detail", f"mean difference to a re-upscale {detail:.2f}", warn_only=True)
        for scale, image in loaded.items():
            if image.size != (w * scale, h * scale):
                continue
            pixels = np.asarray(image)
            alpha = pixels[..., 3]
            r = radius_grid(*image.size, scale)
            tag = f"@{scale}x"
            if name == "ring":
                report(max_alpha(alpha, r < 179.5 - 1 / scale) == 0, f"{tag} transparent inside r 179.5",
                       f"max alpha {max_alpha(alpha, r < 179.5 - 1 / scale):.2f}")
                report(max_alpha(alpha, r > 214 + 1 / scale) == 0, f"{tag} transparent beyond r 214",
                       f"max alpha {max_alpha(alpha, r > 214 + 1 / scale):.2f}")
                shadow = (r > 197.5) & (r < 214)
                report(max_alpha(alpha, shadow) <= 0.36, f"{tag} shadow at most 35 %", f"{max_alpha(alpha, shadow):.2f}")
            if name in ("glass", "dome") or name.startswith("nebula_") or name.startswith("dust_"):
                limit = 180 if name in ("glass", "dome") else 176
                outside = r > limit + 1 / scale
                report(max_alpha(alpha, outside) == 0, f"{tag} transparent beyond r {limit}",
                       f"max alpha {max_alpha(alpha, outside):.2f}")
            if name == "glass":
                inside = r < 179 - 1 / scale
                report(alpha[inside].min() == 255, f"{tag} opaque inside r 179", f"min alpha {alpha[inside].min()}")
            if name == "dome":
                middle = r < 110
                report(max_alpha(alpha, middle) <= 0.045, f"{tag} at most 4 % within r 110", f"{max_alpha(alpha, middle):.3f}")
                # Round 3: the user's own highlight (neu_glass_highlight) reaches 35 %.
                report(max_alpha(alpha, r >= 110) <= 0.36, f"{tag} at most 35 % anywhere", f"{max_alpha(alpha, r >= 110):.3f}")
            if name.startswith("dust_"):
                visible = alpha > 0
                rgb = pixels[..., :3][visible]
                one_colour = rgb.size == 0 or rgb.max() <= 5 or rgb.min() >= 250
                report(one_colour, f"{tag} black or white where visible", f"RGB {rgb.min() if rgb.size else '-'}..{rgb.max() if rgb.size else '-'}")
                report(max_alpha(alpha, alpha >= 0) <= 0.31, f"{tag} alpha at most 30 %", f"{max_alpha(alpha, alpha >= 0):.2f}")
            if name.startswith("nebula_") and (name.endswith("_arms") or name.endswith("_haze")):
                visible = alpha > 0
                rgb = pixels[..., :3][visible]
                report(rgb.size == 0 or rgb.min() >= 250, f"{tag} white where visible", f"min RGB {rgb.min() if rgb.size else '-'}")
                ceiling = 0.46 if name.endswith("_haze") else 0.86
                report(max_alpha(alpha, alpha >= 0) <= ceiling, f"{tag} alpha at most {ceiling - 0.01:.0%}",
                       f"{max_alpha(alpha, alpha >= 0):.2f}")
                # No small bright points: a pixel much brighter than its 6 px neighbourhood.
                a = alpha.astype(float)
                size = max(3, int(round(6 * scale)))
                pad = np.pad(a, size, mode="edge")
                windows = np.lib.stride_tricks.sliding_window_view(pad, (2 * size + 1, 2 * size + 1))
                ring_mean = windows.mean(axis=(-1, -2))[: a.shape[0], : a.shape[1]]
                points = int(np.count_nonzero((a - ring_mean > 70) & (a > 120)))
                report(points == 0, f"{tag} no small bright points", f"{points} pixels stand out", warn_only=True)
            if name in ("knob_l", "knob_s"):
                cap = 24 if name == "knob_l" else 15
                outside = r > cap + 3 + 1 / scale
                report(max_alpha(alpha, outside) == 0, f"{tag} transparent beyond cap + 3 px",
                       f"max alpha {max_alpha(alpha, outside):.2f}")
                turned = np.rot90(pixels)
                diff = np.abs(turned.astype(float) - pixels.astype(float))[..., 3].mean()
                report(diff < 6, f"{tag} rotationally symmetric (alpha)", f"mean alpha difference {diff:.1f}")
            if name in NEUTRAL:
                rgb = pixels[..., :3].astype(float) / 255
                visible = alpha > 64
                spread = (rgb.max(axis=-1) - rgb.min(axis=-1))[visible]
                report(spread.size == 0 or spread.mean() < 0.08, f"{tag} colour-neutral",
                       f"mean saturation {spread.mean() if spread.size else 0:.3f}")
            if name == "chassis" and scale == 1:
                # The texts drawn on it stay readable: 4.5 : 1 against its lightest part.
                lightest = luminance(pixels[..., :3]).max()
                for colour in TEXT_COLOURS:
                    text = luminance(np.array(colour, float).reshape(1, 1, 3))[0, 0]
                    ratio = (max(text, lightest) + 0.05) / (min(text, lightest) + 0.05)
                    report(ratio >= 4.5, f"contrast of #{colour[0]:02X}{colour[1]:02X}{colour[2]:02X}", f"{ratio:.2f} : 1")
    report(total_bytes < 12 * 2**20, "all files under 12 MB", f"{total_bytes / 2**20:.1f} MB")
    sums = folder / "SHA256SUMS.txt"
    if sums.exists():
        bad = []
        for line in sums.read_text(encoding="utf-8").splitlines():
            parts = line.split()
            if len(parts) >= 2:
                file = folder / parts[-1].lstrip("*")
                if not file.exists() or hashlib.sha256(file.read_bytes()).hexdigest() != parts[0].lower():
                    bad.append(parts[-1])
        report(not bad, "SHA256SUMS.txt matches", ", ".join(bad))
    else:
        report(False, "SHA256SUMS.txt", "missing", warn_only=True)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    folder = Path(sys.argv[1])
    if not folder.is_dir():
        print(f"not a folder: {folder}")
        return 2
    check(folder)
    print(f"\n{failures} failed, {warnings} warnings")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

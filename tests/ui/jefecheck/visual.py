"""Pixel-diff helpers for visual regression tests.

The diff strategy:
  1. Pillow loads `actual` (PNG bytes from Appium) and `baseline`
     (committed PNG on disk) into RGBA Images.
  2. If sizes differ, fail loudly — that means window chrome geometry
     changed (likely a Qt or macOS update) and the baseline is invalid.
  3. pixelmatch compares pixel-by-pixel with a perceptual `threshold`
     (YIQ delta) and returns the count of differing pixels.
  4. We allow up to `max_diff_ratio` of pixels to differ before failing,
     to absorb sub-pixel font hinting jitter and GL rasterizer noise
     between runs (the rest of the window chrome is bit-identical).

When `--update-baselines` is passed to pytest, we write the actual
screenshot as the new baseline and skip the comparison.
"""
from __future__ import annotations

import io
import os
from pathlib import Path

import pytest
from PIL import Image
from pixelmatch.contrib.PIL import pixelmatch

# Per-pixel threshold (YIQ space, 0..1). 0.1 ≈ "barely perceptible";
# 0.05 already catches most LUT/gamma regressions but tolerates the
# vendor-specific GL rasterizer noise between runs.
DEFAULT_PIXEL_THRESHOLD = 0.1

# Up to 0.5% of pixels may differ. Window chrome is bit-identical;
# this budget covers font hinting around the layout-status label and
# any antialiased borders Qt redraws on focus changes.
DEFAULT_MAX_DIFF_RATIO = 0.005


def baselines_dir() -> Path:
    """Repo path where committed baseline PNGs live."""
    return Path(__file__).resolve().parent.parent / "baselines"


def _decode(png_bytes: bytes) -> Image.Image:
    return Image.open(io.BytesIO(png_bytes)).convert("RGBA")


def assert_matches_baseline(
    actual_png: bytes,
    baseline_name: str,
    *,
    update: bool = False,
    pixel_threshold: float = DEFAULT_PIXEL_THRESHOLD,
    max_diff_ratio: float = DEFAULT_MAX_DIFF_RATIO,
) -> None:
    """Compare `actual_png` against `baselines/<baseline_name>`.

    `update=True` writes the baseline instead of comparing — used by
    `pytest --update-baselines` when intentionally regenerating after
    a UI change. Comparing while updating is intentionally skipped:
    the new baseline is the new ground truth.

    A failed match writes the actual screenshot and a diff image
    next to the baseline (suffix `.actual.png` and `.diff.png`) so
    the developer can eyeball the regression.
    """
    baseline_path = baselines_dir() / baseline_name
    baseline_path.parent.mkdir(parents=True, exist_ok=True)

    if update or not baseline_path.exists():
        baseline_path.write_bytes(actual_png)
        if not update:
            pytest.skip(
                f"Baseline {baseline_name} did not exist — wrote it. "
                f"Re-run to verify."
            )
        return

    actual = _decode(actual_png)
    baseline = _decode(baseline_path.read_bytes())

    if actual.size != baseline.size:
        # Save the actual so the developer can see what changed.
        actual_out = baseline_path.with_suffix(".actual.png")
        actual_out.write_bytes(actual_png)
        raise AssertionError(
            f"Screenshot size changed for {baseline_name}: "
            f"baseline={baseline.size} actual={actual.size}. "
            f"If this is intentional (Qt/macOS update changed window "
            f"chrome), regenerate with `pytest --update-baselines`. "
            f"Actual saved to {actual_out}."
        )

    diff_img = Image.new("RGBA", actual.size)
    n_diff = pixelmatch(
        baseline, actual, diff_img,
        threshold=pixel_threshold,
        includeAA=False,
    )

    total = actual.size[0] * actual.size[1]
    ratio = n_diff / total
    if ratio <= max_diff_ratio:
        return

    actual_out = baseline_path.with_suffix(".actual.png")
    diff_out = baseline_path.with_suffix(".diff.png")
    actual_out.write_bytes(actual_png)
    diff_img.save(diff_out)
    raise AssertionError(
        f"Visual regression: {baseline_name}\n"
        f"  diff ratio: {ratio:.4%} ({n_diff} of {total} px)\n"
        f"  budget:     {max_diff_ratio:.4%}\n"
        f"  baseline:   {baseline_path}\n"
        f"  actual:     {actual_out}\n"
        f"  diff image: {diff_out}\n"
        f"If this change is intentional, regenerate with "
        f"`pytest --update-baselines`."
    )


def fixtures_dir() -> Path:
    """Repo path where committed input fixture images live."""
    return Path(__file__).resolve().parent.parent / "fixtures"


def make_test_pattern(out_path: Path, size: int = 64) -> Path:
    """Generate the canonical 4-quadrant test pattern.

    Top-left red, top-right green, bottom-left blue, bottom-right
    yellow. This makes flip/flop/rotate regressions trivially visible
    in a visual diff: a flip swaps top and bottom rows, flop swaps
    left and right columns, and any of those rotates show up as a
    very large diff ratio rather than sub-pixel jitter.
    """
    if out_path.exists():
        return out_path
    out_path.parent.mkdir(parents=True, exist_ok=True)
    img = Image.new("RGB", (size, size))
    px = img.load()
    half = size // 2
    for y in range(size):
        for x in range(size):
            top = y < half
            left = x < half
            if top and left:
                px[x, y] = (220, 40, 40)     # red
            elif top and not left:
                px[x, y] = (40, 200, 60)     # green
            elif not top and left:
                px[x, y] = (40, 80, 220)     # blue
            else:
                px[x, y] = (220, 200, 40)    # yellow
    img.save(out_path, "PNG")
    return out_path

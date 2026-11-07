"""Pixel-diff helpers for visual regression tests.
Pillow RGBA load, differing sizes fail loudly; pixelmatch counts differing pixels with up to `max_diff_ratio` allowed to absorb hinting/GL jitter."""
from __future__ import annotations

import io
import os
from pathlib import Path

import pytest
from PIL import Image
from pixelmatch.contrib.PIL import pixelmatch

# Per-pixel threshold (YIQ space, 0..1). 0.1 ≈ "barely perceptible"; 0.05
# catches most LUT/gamma regressions but tolerates GL rasterizer noise.
DEFAULT_PIXEL_THRESHOLD = 0.1

# Up to 0.5% of pixels may differ (font hinting around the layout-status
# label, antialiased borders Qt redraws on focus changes).
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
    """Compare `actual_png` against `baselines/<baseline_name>`; `update=True` writes the baseline instead.
    A failed match writes `.actual.png` and `.diff.png` next to the baseline."""
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
    """Generate the canonical 4-quadrant test pattern: red, green, blue, yellow
    (top-left, top-right, bottom-left, bottom-right), so flip/flop/rotate show up
    as a large diff ratio."""
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

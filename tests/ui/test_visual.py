"""Visual regression tests: pixel-diff the main window against committed baselines.
Regenerate with `pytest tests/ui/test_visual.py --update-baselines`; a fresh checkout writes the baseline and skips."""
import time

import pytest

from jefecheck import locators
from jefecheck.visual import assert_matches_baseline


@pytest.fixture
def settled_visual_app(visual_app):
    """visual_app + a short settle for the deferred --open-file load (the AX query pumps the event loop)."""
    visual_app.by_object_name(locators.STATUSBAR_LAYOUT)
    time.sleep(0.5)
    return visual_app


def test_default_layout_with_pattern(settled_visual_app, update_baselines):
    """Single-plate layout, test pattern loaded — window matches baseline."""
    png = settled_visual_app.window_screenshot()
    assert_matches_baseline(
        png, "default_layout_with_pattern.png", update=update_baselines)


def test_quad_layout_with_pattern(settled_visual_app, update_baselines):
    """Cmd+4 → quad layout — empty plates 1-3 stay black, plate 0 holds."""
    settled_visual_app.send_shortcut("cmd+4")
    # Layout switch retriggers paintGL; same settle as the launch path.
    settled_visual_app.by_object_name(locators.STATUSBAR_LAYOUT)
    time.sleep(0.3)
    png = settled_visual_app.window_screenshot()
    assert_matches_baseline(
        png, "quad_layout_with_pattern.png", update=update_baselines)

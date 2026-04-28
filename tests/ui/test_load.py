"""Image-loading behavioral tests.

Boots JefeCheck with `--open-file` (Phase D), waits for the deferred
load to finish, then asserts on AX-visible state changes that prove
the load reached gfcSequence — not just the GUI value object.

These tests complement the visual-diff tests in `test_visual.py`:
visual diffs catch *rendering* regressions, these catch *plumbing*
regressions (load path, status surfaces, sequence-name reporting).
"""
import time

import pytest

from jefecheck import locators


def _loaded_label(app) -> str:
    """Status-bar 'Loaded: …' label for the active plate.

    QLabel exposes its text via the AX `value` attribute (not `title`,
    which is for buttons / menu items). Same convention as
    statusbar.layout.label.
    """
    label = app.by_object_name(locators.STATUSBAR_LOADED)
    return label.get_attribute("value") or ""


def _wait_for_load(app, timeout: float = 5.0) -> str:
    """Spin until the loaded-label transitions out of the empty state.

    --open-file dispatches via QTimer::singleShot(0) after paintGL has
    fired (so GLAD is initialised before the bridge tries to upload a
    texture). On a cold launch that's typically <500ms, but the WDA
    handshake adds variance — give it a few seconds.
    """
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        last = _loaded_label(app)
        if last and last != "Loaded: -":
            return last
        time.sleep(0.1)
    return last


def test_default_active_plate_loaded_label_starts_empty(app):
    """No --open-file → status bar shows 'Loaded: -' (no sequence)."""
    # Give the tick loop a couple of frames to populate the label
    # for the first time, then assert it stayed empty.
    time.sleep(0.2)
    assert _loaded_label(app) == "Loaded: -"


def test_visual_app_loads_test_pattern_into_plate_zero(visual_app):
    """visual_app fixture preloads test_pattern_64.png into plate 0.

    Asserts:
      1. The status-bar 'Loaded:' label transitions out of the empty
         state (proves the QTimer::singleShot path actually fired).
      2. The reported basename matches the fixture filename (proves
         we reached gfcSequence::filenameGeneric, not just the GUI's
         parallel state).
    """
    label = _wait_for_load(visual_app)
    assert label == "Loaded: test_pattern.png", (
        f"Expected 'Loaded: test_pattern.png', got {label!r}"
    )

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


def test_depth_combo_default_is_16_half(app):
    """Fresh launch (per-module config_dir, no saved Engine/defaultTextureFormat)
    shows the spec's default of 16-half on the status-bar depth combo.

    QComboBox exposes its currently-selected text via `title`, not
    `value` — same convention as TRANSPORT_LOOP. Verified in PR-30's
    layer-combo tests.

    Must run before any test that spawns a separate JefeCheck instance
    (e.g. `visual_app`): Mac2 driver is single-session, so a second
    launch invalidates this module's `app` WDA session.
    """
    combo = app.by_object_name(locators.STATUSBAR_DEPTH)
    assert combo.get_attribute("title") == "16-half"


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


@pytest.fixture
def reusable_config_dir(tmp_path):
    """A config_dir that survives across two JefeCheckApp.launch calls
    inside one test. Returns a Path; both launches must pass it to
    JefeCheckApp.launch(config_dir=...).
    """
    d = tmp_path / "jefecheck-config"
    d.mkdir()
    return d


def test_depth_combo_persists_across_launch(
        appium_server, jefecheck_binary, reusable_config_dir):
    """Set depth to 8, quit, relaunch with the same config_dir, read it back."""
    from jefecheck import JefeCheckApp
    from selenium.webdriver.common.by import By

    # First launch — change the combo to "8" and quit.
    first = JefeCheckApp.launch(
        binary=jefecheck_binary,
        appium_url=appium_server,
        config_dir=reusable_config_dir,
    )
    try:
        combo = first.by_object_name(locators.STATUSBAR_DEPTH)
        # Qt's QComboBox shows its dropdown via QComboBoxListView, which
        # Mac2 exposes as an XCUIElementTypeMenuButton with descendant
        # XCUIElementTypeStaticText rows (not MenuItem — that's reserved
        # for native NSMenu items). Scope the lookup to the combo's
        # listview identifier so a stray '8' label elsewhere doesn't win.
        combo.click()
        opt_8 = first.driver.find_element(
            By.XPATH,
            "//XCUIElementTypeStaticText"
            "[contains(@identifier, 'depth.combo.QComboBoxListView') "
            "and @title='8']")
        opt_8.click()
        # Confirm the change took before quitting.
        assert combo.get_attribute("title") == "8"
    finally:
        first.quit()

    # Second launch — same config_dir, depth should still be "8".
    second = JefeCheckApp.launch(
        binary=jefecheck_binary,
        appium_url=appium_server,
        config_dir=reusable_config_dir,
    )
    try:
        combo = second.by_object_name(locators.STATUSBAR_DEPTH)
        assert combo.get_attribute("title") == "8", (
            "Depth combo did not persist across launch — check that the "
            "QSettings key 'Engine/defaultTextureFormat' is being written "
            "on combo change and read on construction.")
    finally:
        second.quit()

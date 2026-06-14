"""Smoke test — proves the harness can launch the app and see widgets.

Phase B intentionally ships this as the only test; Phase C adds the
real per-feature suite.
"""
from jefecheck import locators


def test_app_launches_and_main_window_visible(app):
    window = app.main_window()
    assert window.get_attribute("title") == "JefeCheck"


def test_plate_zero_card_widgets_resolvable(app):
    """All plate-0 widgets should resolve via the objectName predicate.

    Title-vs-value mapping varies by XCUITest element type (buttons
    return label, combos return selected value), so this test only
    asserts on locator resolution. Per-widget attribute checks live in
    Phase C's feature tests.
    """
    expected = [
        locators.plate(0, "gamma.spin"),
        locators.plate(0, "exposure.spin"),
        locators.plate(0, "contrast.spin"),
        locators.plate(0, "brightness.spin"),
        locators.plate(0, "saturation.spin"),
        locators.plate(0, "lut.combo"),
        locators.plate(0, "flip.button"),
        locators.plate(0, "flop.button"),
        # crop.button moved into the aspect combo's popup (not in the AX
        # tree when closed); the always-visible combo is asserted instead.
        locators.plate(0, "aspect.combo"),
    ]
    for object_name in expected:
        app.by_object_name(object_name)


def test_plate_button_titles_reflect_accessible_name(app):
    """For button widgets, AXTitle should match the Qt accessibleName.

    This verifies the Phase A naming pass actually reaches the AX layer
    (and that Appium can read it) for the widget type that maps cleanly.
    """
    # crop.button moved into the aspect combo popup (AspectCropCombo_Qt)
    # and is no longer always-visible; it is covered by test_plate_ops.
    expected = {
        locators.plate(0, "flip.button"): "Flip",
        locators.plate(0, "flop.button"): "Flop",
    }
    for object_name, title in expected.items():
        widget = app.by_object_name(object_name)
        assert widget.get_attribute("title") == title, object_name


def test_transport_play_button_present(app):
    play = app.by_object_name(locators.TRANSPORT_PLAY)
    assert play.get_attribute("title") == "Play / Pause"


def test_lut_panel_apply_button_present(app):
    apply_btn = app.by_object_name(locators.LUT_APPLY)
    assert apply_btn.get_attribute("title") == "Apply LUT to active plate"

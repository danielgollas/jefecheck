"""Smoke test — proves the harness can launch the app and see widgets.

Phase B intentionally ships this as the only test; Phase C adds the
real per-feature suite.
"""
from jefecheck import locators


def test_app_launches_and_main_window_visible(app):
    window = app.main_window()
    assert window.get_attribute("title") == "JefeCheck"


def test_plate_zero_card_widgets_present(app):
    """A handful of representative widgets across plate 0 should be findable.

    If any of these break, our locator strategy or the accessibility
    naming pass regressed — which is exactly what the test is here to
    catch.
    """
    expected_titles = {
        locators.plate(0, "gamma.spin"): "Gamma",
        locators.plate(0, "exposure.spin"): "Exposure",
        locators.plate(0, "lut.combo"): "LUT",
        locators.plate(0, "flip.button"): "Flip",
        locators.plate(0, "flop.button"): "Flop",
    }
    for object_name, expected_title in expected_titles.items():
        widget = app.by_object_name(object_name)
        assert widget.get_attribute("title") == expected_title, object_name


def test_transport_play_button_present(app):
    play = app.by_object_name(locators.TRANSPORT_PLAY)
    assert play.get_attribute("title") == "Play / Pause"


def test_lut_panel_apply_button_present(app):
    apply_btn = app.by_object_name(locators.LUT_APPLY)
    assert apply_btn.get_attribute("title") == "Apply LUT to active plate"

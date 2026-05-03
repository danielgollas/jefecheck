"""Transport controls (play / step / FPS spin) drive playback state."""
from jefecheck import locators


def test_play_button_present_and_default_paused(app):
    play = app.by_object_name(locators.TRANSPORT_PLAY)
    # The play button shows a play glyph when paused; pause glyph when
    # playing. Title is "Play / Pause" in either state, but the button's
    # text/value differs. The accessible name proves it's the right widget.
    assert play.get_attribute("title") == "Play / Pause"


def test_fps_spin_default_is_24(app):
    fps = app.by_object_name(locators.TRANSPORT_FPS)
    val = fps.get_attribute("value")
    assert val == "24.00" or val == "24"


def test_loop_mode_combo_default_is_once(app):
    """Once / Loop / Bounce — three loop modes (UIConstants).

    Combos in XCUITest report the currently-displayed value via
    `title`, not the AX label, so we assert on the default selection
    instead of the accessibleName.
    """
    combo = app.by_object_name(locators.TRANSPORT_LOOP)
    assert combo.get_attribute("title") == "Once"


def test_in_out_spinboxes_present(app):
    in_spin = app.by_object_name(locators.TRANSPORT_IN)
    out_spin = app.by_object_name(locators.TRANSPORT_OUT)
    assert in_spin.get_attribute("title") == "In point"
    assert out_spin.get_attribute("title") == "Out point"

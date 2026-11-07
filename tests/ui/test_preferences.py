"""Preferences dialog tests, currently skipped due to a Mac2 flake: the first AppleScript-synthesized Cmd+, after a fresh session intermittently fails to fire the QShortcut, though Cmd+1..4 work.
Specific to the Cmd+, -> modal QDialog::exec() path; suspects: App Nap/focus race, nested exec() event loop vs AX, punctuation key delivery."""
import pytest

from jefecheck import locators

pytestmark = pytest.mark.skip(
    reason="Cmd+, → preferences modal flake on first Mac2 keystroke; "
           "see module docstring",
)


def test_placeholder():
    pass


def test_default_decode_filter_combo_default_is_lanczos3(prefs_app):
    """Engine panel exposes Default decode filter combo, default = lanczos3."""
    prefs_app.open_preferences()
    prefs_app.select_prefs_panel("Playback & Engine")
    combo = prefs_app.by_object_name(locators.PREFS_DEFAULT_DECODE_FILTER)
    assert combo is not None, "Default decode filter combo missing from Engine panel"
    assert combo.get_attribute("title") == "lanczos3"

"""Preferences dialog tests, currently skipped due to a Mac2 flake.

The first AppleScript-synthesized Cmd+, after a fresh Mac2 session
intermittently fails to fire JefeCheck's QShortcut, though later sends in the
same process succeed and the identical send_shortcut path works for Cmd+1..4.
So the problem is specific to the Cmd+, -> modal QDialog::exec() path.

Tried without success: a parallel Cmd+, QShortcut, setMenuRole(NoRole),
Qt::ApplicationShortcut context, a retry loop, a longer AppleScript activation
delay, and System Events `set frontmost` instead of `activate`. Suspects: macOS
App Nap / focus race, the nested event loop of exec() interfering with AX
queries, or punctuation keys delivered differently than digits. Other
modal-opening paths (LUT panel buttons, plate-card combos) work fine.
"""
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

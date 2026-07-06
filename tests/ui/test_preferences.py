"""Preferences dialog tests — currently skipped due to a Mac2 flake.

The first AppleScript-synthesized Cmd+, after a fresh Mac2 session
intermittently fails to fire JefeCheck's QShortcut, even though
subsequent sends in the same process succeed. Layout shortcuts
(Cmd+1..4) using the identical send_shortcut path work consistently,
so the issue is specific to the Cmd+, → modal QDialog::exec() path
(possibly the modal blocking the main event loop while AX queries
race against it).

Diagnosis attempted but unresolved:
- Adding parallel Cmd+, QShortcut alongside the menu's Cmd+P: no
- setMenuRole(NoRole) on the prefs QAction: no
- Qt::ApplicationShortcut context instead of WindowShortcut: no
- Retry loop inside the test (re-sending Cmd+,): no
- Bumping AppleScript activation delay 0.1s → 0.4s: no
- Switching to System Events `set frontmost` instead of `activate`: no

Plausible root causes: macOS App Nap / Universal Access focus race;
QDialog::exec()'s nested event loop interfering with AX queries;
synthesized punctuation keys delivered differently than digit keys.

Tracked as a follow-up. The harness covers other modal-opening paths
fine (LUT panel buttons, plate-card combos), so this isn't a structural
gap in Phase C.
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

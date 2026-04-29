"""Plate-card track combo binds to the rendered track.

The plate-card's track combo (A/B/C/D) used to update only the GUI
value object — gfcPlate::track stayed at its constructor default,
so all four plates rendered Track A regardless of what the cards
displayed. PR-22 routes the combo through the bridge so the plate's
internal track field actually changes, and adds a status-bar label
that mirrors the active plate's plate-side track for testability.

Status-bar label format: 'Track: A', 'Track: B', etc.
"""
from jefecheck import locators


def _track_label(app) -> str:
    label = app.by_object_name(locators.STATUSBAR_TRACK)
    # XCUITest exposes a static-text widget's content via the `value`
    # attribute (matching the layout label).
    return label.get_attribute("value") or ""


def test_default_active_plate_shows_track_a(app):
    """Plate 0 is active by default and bound to Track A."""
    assert _track_label(app) == "Track: A"


def _activate_plate(app, plate_id: int) -> None:
    """Click on plate `plate_id`'s card to make it the active plate.

    Mac2 / XCUITest doesn't expose the QFrame itself as an AX element
    (no built-in interaction model), but the plate's big-number QLabel
    inside the frame does surface, and QLabel doesn't consume mouse
    presses — they propagate up to PlateCard_Qt::mousePressEvent which
    emits the clicked signal.
    """
    label = app.by_object_name(locators.plate(plate_id, "id.label"))
    label.click()


def test_clicking_plate_1_switches_label_to_track_b(app):
    """Active-plate change reflects in the status-bar track readout.

    This catches the init-time half of the bug: each plate is bound to
    its index-matching track at startup (plate 0→A, 1→B, 2→C, 3→D), so
    activating plate 1 means the status bar should now read 'Track: B'.
    Before PR-22, plate 1 (and 2 and 3) had `track=0` because the GUI's
    setTrackChoice(i) call didn't propagate to gfcPlate::track.
    """
    _activate_plate(app, 1)
    assert _track_label(app) == "Track: B"


def test_each_plate_starts_on_its_matching_track(app):
    """Plates 0..3 default to tracks A..D respectively."""
    expected = ["Track: A", "Track: B", "Track: C", "Track: D"]
    for plate_id, want in enumerate(expected):
        _activate_plate(app, plate_id)
        assert _track_label(app) == want, (
            f"plate {plate_id} should map to {want}, "
            f"got {_track_label(app)}"
        )

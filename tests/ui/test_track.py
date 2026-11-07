"""Plate-card track combo binds to the rendered track: the status-bar label mirrors the
active plate's gfcPlate::track, as 'Track: A'."""
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
    """Click plate `plate_id`'s "Track" caption to activate it (a QLabel doesn't consume the
    press, so it reaches PlateCard_Qt::mousePressEvent)."""
    label = app.by_object_name(locators.plate(plate_id, "track.label"))
    label.click()


def test_clicking_plate_1_switches_label_to_track_b(app):
    """Activating plate 1 reads 'Track: B': each plate starts bound to its index-matching track."""
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

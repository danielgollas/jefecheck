"""Track timeline widget presence.

The per-track timeline rows (range / offset / loaded fill) are painted
directly on a single QWidget rather than child widgets, so Appium can only
assert the lane container is present and addressable. Behavior (drag-offset,
alt-load, popup, drop) is verified manually per the implementation plan.
"""
from jefecheck import locators


def test_track_timeline_widget_present(app):
    """The timeline tracks lane container exists and is addressable."""
    tracks = app.by_object_name(locators.TIMELINE_TRACKS)
    assert tracks is not None

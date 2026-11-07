"""Track timeline widget presence. The per-track rows (range / offset / loaded
fill) are painted on a single QWidget rather than child widgets, so Appium can
only assert the lane container is present and addressable. Behavior (drag-offset,
alt-load, popup, drop) is verified manually."""
from jefecheck import locators


def test_track_timeline_widget_present(app):
    """The timeline tracks lane container exists and is addressable."""
    tracks = app.by_object_name(locators.TIMELINE_TRACKS)
    assert tracks is not None


def test_thumbnails_toggle_present(app):
    """The timeline thumbnails toggle button is present and addressable."""
    btn = app.by_object_name(locators.TIMELINE_THUMBS_TOGGLE)
    assert btn is not None

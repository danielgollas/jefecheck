"""Plate-card toggle operations (flip, flop, crop) drive plate state."""
from jefecheck import locators


def _checked(widget) -> bool:
    """Returns True if a toggle button is in the 'on' state.

    XCUITest exposes a button's pressed/selected state via the `value`
    attribute, which is '0'/'1' (or 'true'/'false') depending on driver
    version.
    """
    val = widget.get_attribute("value")
    return val in ("1", "true", True)


def test_flip_button_toggles_when_clicked(app):
    btn = app.by_object_name(locators.plate(0, "flip.button"))
    assert not _checked(btn)
    btn.click()
    assert _checked(btn)
    btn.click()
    assert not _checked(btn)


def test_flop_button_toggles_when_clicked(app):
    btn = app.by_object_name(locators.plate(0, "flop.button"))
    assert not _checked(btn)
    btn.click()
    assert _checked(btn)


def test_crop_button_toggles_when_clicked(app):
    btn = app.by_object_name(locators.plate(0, "crop.button"))
    initial = _checked(btn)
    btn.click()
    assert _checked(btn) != initial


def test_each_plate_has_independent_flip_state(app):
    """Toggling plate 0's flip should not change plate 1's."""
    p0_flip = app.by_object_name(locators.plate(0, "flip.button"))
    p1_flip = app.by_object_name(locators.plate(1, "flip.button"))
    assert not _checked(p0_flip)
    assert not _checked(p1_flip)
    p0_flip.click()
    assert _checked(p0_flip)
    assert not _checked(p1_flip)

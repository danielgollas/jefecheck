"""Plate-card toggle operations (flip, flop, crop) drive plate state."""
import pytest

from jefecheck import locators


def _checked(widget) -> bool:
    """True if a toggle button is in the 'on' state (XCUITest `value` is '0'/'1'
    or 'true'/'false' depending on driver version)."""
    val = widget.get_attribute("value")
    return val in ("1", "true", True)


@pytest.fixture(autouse=True)
def _reset_plate_toggles_after(app):
    """Restore every plate's flip/flop/crop to off after each test (the `app` fixture is module-scoped).
    Crop's checkbox is only in the AX tree while the aspect combo's Qt::Popup is open, so reopen it per plate."""
    yield
    for plate_id in range(4):
        for role in ("flip.button", "flop.button"):
            btn = app.by_object_name(locators.plate(plate_id, role))
            if _checked(btn):
                btn.click()
        # Crop lives in the aspect combo popup — open it, then read/clear.
        app.by_object_name(locators.plate(plate_id, "aspect.combo")).click()
        crop = app.by_object_name(locators.plate(plate_id, "crop.button"))
        if _checked(crop):
            crop.click()


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
    # Crop lives in the aspect combo's popup: open the combo first so the
    # checkbox is in the AX tree. Toggling it leaves the popup open.
    app.by_object_name(locators.plate(0, "aspect.combo")).click()
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

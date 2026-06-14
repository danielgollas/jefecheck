"""Plate-card toggle operations (flip, flop, crop) drive plate state."""
import pytest

from jefecheck import locators


def _checked(widget) -> bool:
    """Returns True if a toggle button is in the 'on' state.

    XCUITest exposes a button's pressed/selected state via the `value`
    attribute, which is '0'/'1' (or 'true'/'false') depending on driver
    version.
    """
    val = widget.get_attribute("value")
    return val in ("1", "true", True)


@pytest.fixture(autouse=True)
def _reset_plate_toggles_after(app):
    """Restore every plate's flip/flop/crop to off after each test.

    The `app` fixture is module-scoped, so a test that leaves a toggle
    on (e.g. `test_flop_button_toggles_when_clicked` only clicks once)
    would leak state into the next test in the file. The cleanup
    iterates every plate × every toggle but only clicks when actually
    on, so the cost is one AX read per (plate, toggle) when nothing
    has been changed — typically <100ms total.

    Flip/flop are always-visible buttons. Crop, however, moved into the
    aspect combo's drop-down popup (AspectCropCombo_Qt, a QToolButton whose
    popup is a Qt::Popup frame), so its checkbox is only in the AX tree
    while that popup is open. Reading or clearing crop therefore requires
    opening each plate's aspect combo first — we pay that open cost
    unconditionally for crop because the checked state can't be read without
    the popup. Clicking the crop checkbox is INSIDE the Qt::Popup grab, so it
    leaves the popup open. We re-open per plate; opening the NEXT plate's
    combo is an outside-click relative to the prior plate's popup, which
    dismisses it (the Qt::Popup click-outside-to-dismiss behavior). The last
    plate's popup is left open but harmless — the next test's first AX click
    dismisses it.
    """
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
    # Crop moved into the aspect combo's drop-down popup, so the combo
    # must be opened first before the crop checkbox is resolvable in the
    # AX tree. Toggling the checkbox leaves the popup open by design.
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

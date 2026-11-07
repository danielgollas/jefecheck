"""Plate-reset shortcuts (Cmd+R, Cmd+Alt+R, Shift+R, Shift+Alt+R): Cmd resets all per-plate overrides, Shift resets only color correction (Alt = all plates).
Flip/flop are the observable proxy; CC is tested negatively (Shift+R must NOT clear flip) since Mac2 has no reliable spinbox setter."""
import pytest

from jefecheck import locators


def _checked(widget) -> bool:
    val = widget.get_attribute("value")
    return val in ("1", "true", True)


@pytest.fixture(autouse=True)
def _restore_plate_toggles_after(app):
    """Same defensive cleanup as test_plate_ops: the `app` fixture is
    module-scoped and these tests leave toggles flipped, so a failure before
    Cmd+R can't leak into the next test.
    """
    yield
    for plate_id in range(4):
        for role in ("flip.button", "flop.button"):
            btn = app.by_object_name(locators.plate(plate_id, role))
            if _checked(btn):
                btn.click()


def test_cmd_r_clears_flip_on_active_plate(app):
    """Cmd+R must clear the active plate's flip toggle."""
    flip = app.by_object_name(locators.plate(0, "flip.button"))
    flip.click()
    assert _checked(flip)
    app.send_shortcut("cmd+r")
    assert not _checked(flip)


def test_cmd_alt_r_clears_flips_on_all_plates(app):
    """Cmd+Alt+R should reset every plate, not just the active one. Toggle plate
    0 and plate 1 to prove it reaches a non-active plate (clicking a card's flip
    button doesn't make it active).
    """
    flip0 = app.by_object_name(locators.plate(0, "flip.button"))
    flip1 = app.by_object_name(locators.plate(1, "flip.button"))
    flip0.click()
    flip1.click()
    assert _checked(flip0)
    assert _checked(flip1)
    app.send_shortcut("cmd+alt+r")
    assert not _checked(flip0)
    assert not _checked(flip1)


def test_shift_r_leaves_flip_alone(app):
    """Shift+R is color-correction-only and must NOT clear flip: negative-case
    proof that resetActiveColorCorrection runs a different path from resetActivePlate.
    """
    flip = app.by_object_name(locators.plate(0, "flip.button"))
    flip.click()
    assert _checked(flip)
    app.send_shortcut("shift+r")
    # Color-correction reset doesn't touch the flip transform.
    assert _checked(flip)

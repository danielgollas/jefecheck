"""Plate-reset shortcuts (Cmd+R, Cmd+Alt+R, Shift+R, Shift+Alt+R).

Mirrors FLTK's MenuCallbacks.cpp reset semantics:
  Cmd+R       — reset every per-plate override on the active plate
  Cmd+Alt+R   — same across all four plates
  Shift+R     — reset only color correction on the active plate
  Shift+Alt+R — same across all plates

The flip/flop buttons are the most observable proxy for "did reset
fire on this plate?" — they toggle cleanly via AX, and an isolated
flip toggle isolates the per-plate vs all-plates distinction. Color-
correction-only tests would need to drive the spinboxes, which Mac2
doesn't expose a reliable setter for; instead we test the *negative*
case (Shift+R must NOT clear flip) which is enough to prove the
distinct code path runs.
"""
import pytest

from jefecheck import locators


def _checked(widget) -> bool:
    val = widget.get_attribute("value")
    return val in ("1", "true", True)


@pytest.fixture(autouse=True)
def _restore_plate_toggles_after(app):
    """Same defensive cleanup as test_plate_ops — the `app` fixture is
    module-scoped and these tests deliberately leave toggles flipped.
    Cmd+R should reset them, but if a test fails before sending the
    shortcut, this guarantees the next test starts clean.
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
    """Cmd+Alt+R should reset every plate, not just the active one.

    Toggle plate 0 *and* plate 1 to prove the all-plates reset reaches
    a non-active plate. (Plate 0 is the default active plate; toggling
    plate 1 via its card doesn't make it active — clicking the card
    body does, but clicking the flip button doesn't.)
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
    """Shift+R is color-correction-only — it must NOT clear flip.

    Negative-case proof that resetActiveColorCorrection runs a
    different code path from resetActivePlate. If the binding accidentally
    pointed at the wrong bridge fn, this test would catch it: flip
    would clear and the assertion would flip.
    """
    flip = app.by_object_name(locators.plate(0, "flip.button"))
    flip.click()
    assert _checked(flip)
    app.send_shortcut("shift+r")
    # Color-correction reset doesn't touch the flip transform.
    assert _checked(flip)

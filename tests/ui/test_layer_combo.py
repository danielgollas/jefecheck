"""Per-plate EXR layer combo on a 4-frame multi-view EXR sequence (layers Main, right).
Mac2: combo selection is the AX `title`; popup items enumerate the whole system tree, so use keyboard."""
from __future__ import annotations

import time

from jefecheck import locators


def _loaded_label(app) -> str:
    label = app.by_object_name(locators.STATUSBAR_LOADED)
    return label.get_attribute("value") or ""


def _wait_for_load(app, timeout: float = 5.0) -> str:
    """Poll until the loaded label transitions out of the empty state."""
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        last = _loaded_label(app)
        if last and last != "Loaded: -":
            return last
        time.sleep(0.1)
    return last


def _layer_combo(app, plate_id: int = 0):
    return app.by_object_name(locators.plate(plate_id, "layer.combo"))


def _layer_combo_optional(app, plate_id: int = 0):
    return app.by_object_name_optional(locators.plate(plate_id, "layer.combo"))


def _combo_text(combo) -> str:
    """Read the combo's current text (Mac2 exposes it as the `title` attribute)."""
    return (combo.get_attribute("title") or "").strip()


def _wait_for_layer_combo(app, plate_id: int = 0, timeout: float = 5.0):
    """Poll for the layer combo to carry a selection (sub-layers appear after loadPreview)."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        combo = _layer_combo_optional(app, plate_id)
        if combo is not None and combo.is_displayed():
            if _combo_text(combo):
                return combo
        time.sleep(0.1)
    raise AssertionError(
        f"Layer combo on plate {plate_id} never appeared with a value")


def test_multiview_load_populates_layer_combo(multiview_app):
    """Loading a multi-layer EXR populates the layer combo on plate 0."""
    _wait_for_load(multiview_app)
    combo = _wait_for_layer_combo(multiview_app)
    assert combo.is_displayed(), "Layer combo is present but hidden"


def test_multiview_layer_combo_defaults_to_main_layer(multiview_app):
    """Initial selection is the first discovered layer, "Main"."""
    _wait_for_load(multiview_app)
    combo = _wait_for_layer_combo(multiview_app)
    current = _combo_text(combo)
    assert current == "Main", (
        f"Expected default layer 'Main', got {current!r}"
    )


def test_multiview_layer_arrow_navigation_reveals_right_layer(multiview_app):
    """Down+Return in the combo popup selects the second layer ('right').

    The 0.4s sleep is needed: Mac2 returns before the popup finishes animating."""
    _wait_for_load(multiview_app)
    combo = _wait_for_layer_combo(multiview_app)
    combo.click()
    time.sleep(0.4)
    multiview_app.driver.execute_script(
        "macos: keys",
        {"keys": [{"key": "\uf701"},   # NSDownArrowFunctionKey
                  {"key": "\r"}]})      # Return
    time.sleep(0.4)

    combo = _layer_combo(multiview_app)
    current = _combo_text(combo)
    assert current == "right", (
        f"Expected combo to land on 'right' after Down+Return, "
        f"got {current!r}"
    )


def test_multiview_layer_switch_keeps_sequence_loaded(multiview_app):
    """Switching layers (setChannel, loadPreview, startLoadingSequence) keeps the sequence loaded."""
    label_before = _wait_for_load(multiview_app)
    assert label_before.startswith("Loaded: multiview"), (
        f"Multi-view sequence didn't reach the loaded label: {label_before!r}"
    )

    combo = _wait_for_layer_combo(multiview_app)
    combo.click()
    time.sleep(0.4)
    multiview_app.driver.execute_script(
        "macos: keys",
        {"keys": [{"key": "\uf701"},
                  {"key": "\r"}]})

    # Give the next tick a chance to update the loaded label (the re-decode is synchronous).
    time.sleep(0.5)
    label_after = _loaded_label(multiview_app)
    assert label_after == label_before, (
        f"Layer switch broke the loaded label: "
        f"before={label_before!r} after={label_after!r}"
    )
    combo = _layer_combo(multiview_app)
    current = _combo_text(combo)
    assert current == "right", (
        f"Combo didn't reflect the layer switch: {current!r}"
    )


def test_unloaded_plate_shows_main_layer_default(multiview_app):
    """Plates without a previewed sequence still show the combo with the "Main" default."""
    _wait_for_load(multiview_app)
    combo = _layer_combo(multiview_app, plate_id=1)
    assert combo.is_displayed(), (
        "Layer combo on plate 1 should be visible with the Main default"
    )
    assert _combo_text(combo) == "Main", (
        f"Unloaded plate should default to 'Main', got "
        f"{_combo_text(combo)!r}"
    )

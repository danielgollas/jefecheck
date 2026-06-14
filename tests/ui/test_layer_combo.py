"""Per-plate EXR layer combo (PR-30).

Drops a 4-frame multi-view EXR sequence into plate 0 via --open-file,
then asserts the layer combo on plate 0 actually drives the OIIO
loader's channel selection. The fixture EXRs ship two layers
(`Main` — the prefix-less R,G,B group — and `right`) discovered
from the channel names `R, G, B, right.R, right.G, right.B`.

The layer combo is always visible; with no loaded layers (or a plain
single-layer file) it shows a single "Main" default.

These exercise the full plumbing on a real multi-frame, multi-layer
sequence: setChannelOptions → channelOptions_ → getLayersOnPlate →
combo populate, plus combo selection → setLayerOnPlate → loadPreview
→ startLoadingSequence. The async-reload branch (getNumPreviewFrames
> 1) only fires for actual sequences, so the test fixture is a 4-frame
sequence rather than a single multi-layer EXR.

Mac2 / AX quirks worth knowing:
- QComboBox's current selection surfaces as the AX `title`, not `value`
  (which is empty). element.text returns the AX `label` which Qt fills
  from the tooltip — useless here.
- Opening a QComboBox popup and enumerating AXMenuItems returns the
  entire system AX tree (Apple menu, app menus from every running
  process), not just the combo's items. So we drive selection via
  keyboard arrow navigation, not popup-item enumeration.
"""
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
    """Read the current selection text from a QComboBox AX element.

    Mac2's WDA exposes the combo's currentText as the `title`
    attribute. `value` is always empty for QComboBox; `text` returns
    the tooltip (label) which is unrelated to the selection.
    """
    return (combo.get_attribute("title") or "").strip()


def _wait_for_layer_combo(app, plate_id: int = 0, timeout: float = 5.0):
    """Poll for the layer combo to be populated with the multi-layer list.

    The combo is always visible (showing "Main" by default), but the
    EXR sub-layers only appear after setChannelOptions runs on the sequence
    GUI during loadPreview. Callers pair this with _wait_for_load so the
    deferred load has landed and refreshAllCards has rebuilt the item list
    before the AX query reads it.
    """
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
    """Loading a multi-layer EXR populates the layer combo on plate 0.

    The combo is always displayed; the signal here is that it gains the
    discovered layers — i.e. the OIIO loader's discovery flowed through
    gfcSequenceGUI_Qt's channelOptions_ to the widget. _wait_for_layer_combo
    polls until it carries a selection.
    """
    _wait_for_load(multiview_app)
    combo = _wait_for_layer_combo(multiview_app)
    assert combo.is_displayed(), "Layer combo is present but hidden"


def test_multiview_layer_combo_defaults_to_main_layer(multiview_app):
    """Initial selection is the first OIIO-discovered layer.

    discoverLayers walks channels in spec order: the empty-prefix group
    (R, G, B) is named "Main" and is layer 0; "right" (3 channels
    with the `right.` prefix) is layer 1. loadPreview's first-time path
    calls setChannel(0), so the combo's currentText should land on
    "Main".
    """
    _wait_for_load(multiview_app)
    combo = _wait_for_layer_combo(multiview_app)
    current = _combo_text(combo)
    assert current == "Main", (
        f"Expected default layer 'Main', got {current!r}"
    )


def test_multiview_layer_arrow_navigation_reveals_right_layer(multiview_app):
    """Picking the second layer via popup navigation updates the combo.

    Click opens the popup, Down highlights the second item ('right'),
    Return commits the selection. Two purposes: confirms the second
    layer made it into the popup, and exercises the full selection
    path without enumerating AXMenuItems (Mac2 returns the entire
    system AX tree there, not just popup contents).

    The 0.4s sleep after click is necessary: Mac2 returns before the
    popup has finished animating in, so keystrokes sent immediately
    land on the closed combo button (a no-op).
    """
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
    """Switching layers must not break the loaded sequence.

    The bridge calls setChannel(name) → loadPreview() →
    startLoadingSequence(track). If any of those fail (e.g. the new
    channel name doesn't match any OIIO-discovered layer, or the
    re-decode crashes the loader thread), gfcSequence's previewFrame.
    loaded flips false and the loaded label drops back to "Loaded: -".
    A successful re-decode keeps the label on the same sequence
    basename — which is what we assert.
    """
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

    # The bridge runs the re-decode synchronously; give the next tick
    # a chance to update the loaded label, then assert it stayed
    # pointed at the same sequence and the combo reflects the switch.
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
    """Plates without a previewed sequence still show the combo.

    Only plate 0 was --open-file'd; plates 1..3 have empty
    channelOptions_ on their sequence GUIs. The combo is always visible,
    so an unloaded plate shows the single "Main" default rather
    than hiding the control.
    """
    _wait_for_load(multiview_app)
    combo = _layer_combo(multiview_app, plate_id=1)
    assert combo.is_displayed(), (
        "Layer combo on plate 1 should be visible with the Main default"
    )
    assert _combo_text(combo) == "Main", (
        f"Unloaded plate should default to 'Main', got "
        f"{_combo_text(combo)!r}"
    )

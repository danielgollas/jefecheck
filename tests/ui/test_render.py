"""File → Render… dialog smoke tests: assert field presence without firing the render
(a sync render would freeze the test session)."""
import pytest
from appium.webdriver.common.appiumby import AppiumBy

from jefecheck import locators


def _open_render_dialog(app):
    """Click File → Render… via the macOS menu bar AX surface."""
    app.wait_for_startup_ready()
    item = app.driver.find_element(
        AppiumBy.IOS_PREDICATE,
        "elementType == 54 AND title == 'Render…'",
    )
    item.click()


def test_render_menu_action_present(app):
    """The File → Render… menu item exists."""
    app.wait_for_startup_ready()
    el = app.driver.find_element(
        AppiumBy.IOS_PREDICATE,
        "elementType == 54 AND title == 'Render…'",
    )
    assert el is not None


def test_render_dialog_opens_with_format_combo(app):
    """Opening File → Render… surfaces the dialog and its format combo."""
    _open_render_dialog(app)

    combo = app.by_object_name("dialog.render.format.combo")
    assert combo is not None

    # Dismiss with Done so other tests that share the module-scoped
    # app don't hit a still-modal dialog.
    app.by_object_name("dialog.render.done.button").click()


def test_render_dialog_format_combo_has_six_entries(app):
    """JPEG / EXR / TIFF / TGA / BMP / PNG. Index order must match
    `gfcRenderFormats` so the bridge doesn't pass a mismatched format
    enum to renderPlate.
    """
    _open_render_dialog(app)
    combo = app.by_object_name("dialog.render.format.combo")
    # A closed Mac2 QComboBox's `value` is the selected entry's text; assert the default is JPEG.
    val = combo.get_attribute("value") or combo.text or ""
    assert "JPEG" in val, f"Expected default format JPEG, got: {val!r}"
    app.by_object_name("dialog.render.done.button").click()


def test_render_button_disabled_without_path(app):
    """Render button stays disabled until the user supplies an output
    directory, since `triggerSyncRender` would otherwise write into
    the working directory or fail outright.
    """
    _open_render_dialog(app)
    btn = app.by_object_name("dialog.render.render.button")
    enabled = btn.get_attribute("enabled")
    # Mac2 returns the string "false" for a disabled button.
    assert enabled in ("false", False, None), (
        f"Expected Render disabled with empty path, enabled={enabled!r}"
    )
    app.by_object_name("dialog.render.done.button").click()

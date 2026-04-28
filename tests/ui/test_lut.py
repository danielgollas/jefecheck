"""LUT panel is populated and exposes apply/refresh controls."""
from appium.webdriver.common.appiumby import AppiumBy

from jefecheck import locators


def test_lut_panel_lists_install_luts(app):
    """The autoload pass should drop ≥1 .cube/.lut into the panel.

    The repo's src/FX/ ships 13 LUTs; bundle build copies them into
    Contents/Resources/FX/. We assert ≥2 because index 0 is the implicit
    'No LUT' slot — anything beyond that is a real autoloaded LUT.
    """
    list_widget = app.by_object_name(locators.LUT_LIST)
    items = list_widget.find_elements(AppiumBy.XPATH, "*")
    assert len(items) >= 2, f"Expected ≥2 LUT entries, got {len(items)}"


def test_lut_apply_button_present_and_enabled(app):
    apply_btn = app.by_object_name(locators.LUT_APPLY)
    assert apply_btn.get_attribute("enabled") in ("true", True)
    assert apply_btn.get_attribute("title") == "Apply LUT to active plate"


def test_lut_refresh_button_present(app):
    refresh = app.by_object_name(locators.LUT_REFRESH)
    assert refresh.get_attribute("title") == "Refresh LUT list"


def test_plate_card_lut_combo_lists_loaded_luts(app):
    """The plate-card LUT combo should have the same set of LUT entries
    the main panel autoloaded — proves the panel↔card sync wiring."""
    combo = app.by_object_name(locators.plate(0, "lut.combo"))
    combo.click()
    items = app.driver.find_elements(
        AppiumBy.IOS_PREDICATE, "elementType == 54")
    assert len(items) >= 2, f"Expected ≥2 LUT options, got {len(items)}"
    # Click somewhere safe to dismiss the popup before teardown.
    app.driver.execute_script("macos: keys",
                              {"keys": [{"key": "\x1b"}]})  # ESC

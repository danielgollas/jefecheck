"""Help → System Specs dialog: smoke test on the menu wiring.

Qt's macOS NSMenuItem AX bridge does NOT propagate QAction::objectName
into the AX `identifier` attribute — every menu item shows up with
`identifier='qt_itemFired:'`. The reliable lookup is by `title`.
"""
from appium.webdriver.common.appiumby import AppiumBy


def test_help_menu_has_system_specs(app):
    """Help → System Specs is wired up with the expected title."""
    app.wait_for_startup_ready()
    el = app.driver.find_element(
        AppiumBy.IOS_PREDICATE,
        "elementType == 54 AND title == 'System Specs…'")
    assert el is not None

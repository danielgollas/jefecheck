"""Remote Session dialog smoke tests: it opens from File -> Remote Session… and
its widgets resolve via objectName. Actual connectivity needs a paired second
instance (see the headless --remote-test harness)."""
from appium.webdriver.common.appiumby import AppiumBy

from jefecheck import locators


def _open_remote_dialog(app):
    """Click File → Remote Session…; mirrors test_minspecs.py's pattern."""
    app.driver.find_element(
        AppiumBy.IOS_PREDICATE,
        "elementType == 54 AND title == 'Remote Session…'").click()


def _close_remote_dialog(app):
    app.by_object_name(locators.REMOTE_DONE).click()


def test_file_menu_has_remote_session(app):
    """File → Remote Session… is wired up with the expected title."""
    app.wait_for_startup_ready()
    el = app.driver.find_element(
        AppiumBy.IOS_PREDICATE,
        "elementType == 54 AND title == 'Remote Session…'")
    assert el is not None


def test_remote_dialog_fields_resolvable(app):
    """Server + client form widgets all resolve via the AX bridge."""
    app.wait_for_startup_ready()
    _open_remote_dialog(app)
    try:
        expected = [
            locators.REMOTE_SERVER_NAME,
            locators.REMOTE_SERVER_PORT,
            locators.REMOTE_SERVER_PASSWORD,
            locators.REMOTE_SERVER_START,
            locators.REMOTE_CLIENT_NAME,
            locators.REMOTE_CLIENT_IP,
            locators.REMOTE_CLIENT_PORT,
            locators.REMOTE_CLIENT_PASSWORD,
            locators.REMOTE_CLIENT_CONNECT,
            locators.REMOTE_DISCONNECT,
            locators.REMOTE_STATUS,
            locators.REMOTE_DONE,
        ]
        for object_name in expected:
            widget = app.by_object_name(object_name)
            assert widget is not None, object_name
    finally:
        _close_remote_dialog(app)


def test_remote_status_starts_not_connected(app):
    """Default state on a fresh launch is `Not connected`."""
    app.wait_for_startup_ready()
    _open_remote_dialog(app)
    try:
        status = app.by_object_name(locators.REMOTE_STATUS)
        text = (status.text or status.get_attribute("value") or "").strip()
        assert text == "Not connected", f"Got: {text!r}"
    finally:
        _close_remote_dialog(app)

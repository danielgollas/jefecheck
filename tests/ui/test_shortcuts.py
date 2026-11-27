"""Preferences → Shortcuts page: search, capture a key in place, the conflict bar, and
import / export. The same logic runs in process with `jefecheck --shortcuts-page-test`;
these tests drive the real window through Mac2.

Import…/Export… use JEFECHECK_SHORTCUTS_FILE (set at launch) instead of a file dialog.
"""
import time
from pathlib import Path

import pytest
from appium.webdriver.common.appiumby import AppiumBy

from jefecheck import locators
from jefecheck.app import JefeCheckApp


@pytest.fixture
def shortcuts_app(appium_server, jefecheck_binary, tmp_path, multiview_sequence):
    """A fresh app (own QSettings) with a 4-frame sequence, so a play key has frames to play."""
    instance = JefeCheckApp.launch(
        binary=jefecheck_binary,
        appium_url=appium_server,
        config_dir=tmp_path / "jefecheck-config",
        open_files=[multiview_sequence],
        env={"JEFECHECK_SHORTCUTS_FILE": str(tmp_path / "keys.jcshortcuts")},
    )
    instance.shortcuts_file = tmp_path / "keys.jcshortcuts"
    yield instance
    instance.quit()


def open_shortcuts_page(app: JefeCheckApp) -> None:
    app.send_shortcut("cmd+,")
    sidebar = app.by_object_name(locators.PREFERENCES_SIDEBAR, timeout=10.0)
    sidebar.find_element(
        AppiumBy.IOS_PREDICATE, "title == 'Shortcuts' OR value == 'Shortcuts' OR label == 'Shortcuts'"
    ).click()
    app.by_object_name(locators.PREFS_SHORTCUTS_SEARCH)


def list_rows_named(app: JefeCheckApp, name: str):
    tree = app.by_object_name(locators.PREFS_SHORTCUTS_LIST)
    return tree.find_elements(AppiumBy.IOS_PREDICATE, f"value == '{name}' OR title == '{name}'")


def key_cell(app: JefeCheckApp, command: str):
    """The Key cell on the same row as `command` (the cell right of the name)."""
    name = list_rows_named(app, command)[0]
    row = name.find_element(AppiumBy.XPATH, "..")
    cells = row.find_elements(AppiumBy.XPATH, "./*")
    return cells[1] if len(cells) > 1 else name


def capture(app: JefeCheckApp, command: str, key: str) -> None:
    cell = key_cell(app, command)
    app.driver.execute_script("macos: doubleClick", {"elementId": cell.id})
    app.by_object_name(locators.PREFS_SHORTCUTS_CAPTURE)
    app.send_keys(key)


def frame(app: JefeCheckApp) -> int:
    value = app.by_object_name(locators.TRANSPORT_FRAME).get_attribute("value") or "0"
    return int(float(value))


def test_rebind_play_forward_with_conflict(shortcuts_app):
    app = shortcuts_app
    open_shortcuts_page(app)
    app.by_object_name(locators.PREFS_SHORTCUTS_SEARCH).send_keys("play forward")
    assert list_rows_named(app, "Play Forward"), "search lost the Play Forward row"
    assert not list_rows_named(app, "Play / Pause"), "search did not filter the list"

    # P first: if it is free it binds straight away, so then bind a key that is taken.
    capture(app, "Play Forward", "p")
    replace = app.by_object_name_optional(locators.PREFS_SHORTCUTS_CONFLICT_REPLACE)
    if replace is None:
        capture(app, "Play Forward", " ")  # Space belongs to Play / Pause
        replace = app.by_object_name(locators.PREFS_SHORTCUTS_CONFLICT_REPLACE)
        key = " "
    else:
        key = "p"
    replace.click()
    app.by_object_name(locators.PREFERENCES_DONE).click()

    app.by_object_name(locators.VIEWPORT).click()
    start = frame(app)
    app.send_keys(key)
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline and frame(app) == start:
        time.sleep(0.1)
    assert frame(app) != start, "the rebound key did not start playback"


def test_export_change_import_restores(shortcuts_app):
    app = shortcuts_app
    open_shortcuts_page(app)
    app.by_object_name(locators.PREFS_SHORTCUTS_EXPORT).click()
    exported: Path = app.shortcuts_file
    assert exported.exists() and exported.stat().st_size > 0

    app.by_object_name(locators.PREFS_SHORTCUTS_SEARCH).send_keys("play forward")
    capture(app, "Play Forward", "\u0008")  # Backspace on the empty capture unbinds
    assert app.by_object_name_optional(locators.PREFS_SHORTCUTS_CUSTOM) is not None

    app.by_object_name(locators.PREFS_SHORTCUTS_IMPORT).click()
    notice = app.by_object_name(locators.PREFS_SHORTCUTS_NOTICE)
    assert (notice.get_attribute("value") or notice.text or "").startswith("Imported")
    reset = app.by_object_name(locators.PREFS_SHORTCUTS_RESET)
    assert reset.get_attribute("enabled") in ("false", False), "import did not restore the exported keys"
    app.by_object_name(locators.PREFERENCES_CANCEL).click()

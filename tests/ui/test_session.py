"""Session menu / locator presence.

macOS folds Qt's menu bar into the system menu bar (a separate AX tree), so
menu QActions aren't reliably addressable by objectName under Mac2 — these
constants document the canonical names. Functional save/open/recover and the
CC-favorites round-trip are verified manually per the implementation plan.
This test keeps the locators importable and parse-checked.
"""
from jefecheck import locators


def test_session_locator_constants_exist():
    assert locators.MENU_FILE_SAVE_SESSION == "menu.file.savesession"
    assert locators.MENU_FILE_OPEN_SESSION == "menu.file.opensession"
    assert locators.MENU_FILE_RECENT == "menu.file.recent"
    assert locators.MENU_VIEW_CCFAVORITES == "menu.view.ccfavorites"

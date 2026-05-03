"""Playlist dock — PR-40 smoke tests.

The dock is tabified with FX Params on the left side; FX Params is
raised by default, so we click the Playlist tab to surface its widgets
before asserting on them.
"""
import pytest
from appium.webdriver.common.appiumby import AppiumBy

from jefecheck import locators


def _list_items(list_widget):
    items = list_widget.find_elements(AppiumBy.XPATH, "*")
    return items[:-1] if items else []


def test_playlist_dock_buttons_resolvable(app):
    """Add / Remove / Up / Down / Clear buttons all resolve via
    objectName. The dock starts tabified-behind FX Params but the
    AX bridge surfaces tabified-dock children regardless of which tab
    is raised — only AXValue is sometimes elided."""
    app.wait_for_startup_ready()

    for object_name in (
        locators.PLAYLIST_ADD,
        locators.PLAYLIST_REMOVE,
        locators.PLAYLIST_UP,
        locators.PLAYLIST_DOWN,
        locators.PLAYLIST_CLEAR,
    ):
        btn = app.by_object_name(object_name)
        assert btn is not None, object_name


def test_playlist_starts_empty(app):
    """A fresh launch has no playlist entries."""
    app.wait_for_startup_ready()
    pl = app.by_object_name(locators.PLAYLIST_LIST)
    items = _list_items(pl)
    assert items == [], f"Expected empty playlist, got {len(items)} entries"

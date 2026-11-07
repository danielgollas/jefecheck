"""FX Stack panel: Available list -> active plate stack add/remove.
Tests wait for the startup label to reach 'Ready' first, or the assertion races the autoload and sees a partial list."""
import re

import pytest
from appium.webdriver.common.appiumby import AppiumBy

from jefecheck import locators


def _list_items(list_widget):
    """Enumerate visible rows of a QListWidget via AX.
    Qt exposes an AXList with N AXRow children plus a trailing always-present AXColumn placeholder, so slice off the last child (`*` xpath and XCUIElementType filters miscount)."""
    items = list_widget.find_elements(AppiumBy.XPATH, "*")
    return items[:-1] if items else []


@pytest.fixture
def ready_app(app):
    """An app whose autoload has finished. Failing here means the
    startup pipeline broke, which would otherwise show up as flaky
    'short FX list' failures in every dependent test."""
    app.wait_for_startup_ready()
    return app


@pytest.fixture(autouse=True)
def _clean_fx_stack(app):
    # Module-scoped `app` shares state across tests in this file; clear the
    # stack before each test so order doesn't matter (no-op when empty).
    app.wait_for_startup_ready()
    while True:
        stack = app.by_object_name(locators.FXSTACK_STACK)
        rows = _list_items(stack)
        if not rows:
            break
        rows[0].click()
        app.by_object_name(locators.FXSTACK_REMOVE).click()
    yield


def test_startup_status_reaches_terminal_state(app):
    """The autoload pipeline must reach a terminal status — Ready or
    Errors — within the timeout. Validates that the autoload doesn't
    hang or crash mid-load, regardless of whether all files parsed.
    """
    final = app.wait_for_startup_ready()
    assert final.startswith("Startup: Ready") or \
           final.startswith("Startup: Errors"), \
        f"Expected Ready or Errors terminal state, got: {final!r}"


def test_startup_loads_all_fxs(app):
    """Every shipped .jfx must compile and load; a failure means a GLSL shader regressed.
    LUT load failures are tolerated (printFotokem.cube isn't supported by the parser); asserts on FX only."""
    final = app.wait_for_startup_ready()
    # Both formats embed the FX counts; pull them out for assertion.
    m = re.search(r"\((\d+)/(\d+) FX", final) or \
        re.search(r"\((\d+) FX,\s*(\d+) LUT\)", final)
    assert m, f"Could not parse FX counts from status: {final!r}"
    if final.startswith("Startup: Ready"):
        fx_loaded = int(m.group(1))
        # Ready format has just one FX number — it's the loaded count.
        assert fx_loaded >= 5, \
            f"Expected ≥5 FXs loaded, got {fx_loaded}"
    else:
        fx_loaded, fx_expected = int(m.group(1)), int(m.group(2))
        assert fx_loaded == fx_expected, (
            f"FX autoload regression — only {fx_loaded}/{fx_expected} "
            f"FXs loaded. Status: {final!r}"
        )


def test_fx_panel_lists_loaded_effects(ready_app):
    """The autoload pass loads every src/FX/*.jfx; the panel should
    display them."""
    available = ready_app.by_object_name(locators.FXSTACK_AVAILABLE)
    items = _list_items(available)
    assert len(items) >= 5, f"Expected ≥5 FX entries, got {len(items)}"


def test_fx_panel_stack_starts_empty(ready_app):
    """A fresh launch has nothing on plate 0's FX stack."""
    stack = ready_app.by_object_name(locators.FXSTACK_STACK)
    assert _list_items(stack) == []


def test_fx_panel_add_button_titles(ready_app):
    add_btn = ready_app.by_object_name(locators.FXSTACK_ADD)
    remove_btn = ready_app.by_object_name(locators.FXSTACK_REMOVE)
    assert add_btn.get_attribute("title") == "Add FX to active plate"
    assert remove_btn.get_attribute("title") == "Remove FX from stack"


def test_fx_add_first_effect_lands_in_stack(ready_app):
    """Selecting a row in Available + clicking Add appends to the stack
    (addFXToActivePlate -> getFXStackOnPlate -> refreshLists, end to end).
    """
    available = ready_app.by_object_name(locators.FXSTACK_AVAILABLE)
    items = _list_items(available)
    assert items, "no Available FXs to test with"

    items[0].click()
    ready_app.by_object_name(locators.FXSTACK_ADD).click()

    stack = ready_app.by_object_name(locators.FXSTACK_STACK)
    after = _list_items(stack)
    assert len(after) == 1, (
        f"Expected stack to have 1 FX after Add, got {len(after)}"
    )


def test_fx_remove_clears_stack_entry(ready_app):
    """Add then Remove leaves the stack empty; covers the removeFXFromPlate
    bridge path (clearStack + re-add all but the removed index, since
    gfcFXStack has no single-item remove).
    """
    available = ready_app.by_object_name(locators.FXSTACK_AVAILABLE)
    items = _list_items(available)
    assert items
    items[0].click()
    ready_app.by_object_name(locators.FXSTACK_ADD).click()

    stack = ready_app.by_object_name(locators.FXSTACK_STACK)
    rows = _list_items(stack)
    assert len(rows) == 1
    rows[0].click()
    ready_app.by_object_name(locators.FXSTACK_REMOVE).click()

    stack = ready_app.by_object_name(locators.FXSTACK_STACK)
    assert _list_items(stack) == []

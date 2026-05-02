"""FX Params dock — read-only viewer of active plate's FX stack params.

Sits below the FX Stack / LUT tab group on the right side, vertically
split. Always visible (not tabified) so the AX bridge surfaces its
status label's value reliably and so the user can see params update
as they add or remove FX above.
"""
import re

import pytest
from appium.webdriver.common.appiumby import AppiumBy

from jefecheck import locators


def _list_items(list_widget):
    items = list_widget.find_elements(AppiumBy.XPATH, "*")
    return items[:-1] if items else []


@pytest.fixture
def ready_app(app):
    app.wait_for_startup_ready()
    return app


@pytest.fixture(autouse=True)
def _clean_fx_stack(app):
    # Same isolation as test_fx.py — Add tests in this file leak into
    # later tests if not cleared. Idempotent when stack already empty.
    app.wait_for_startup_ready()
    while True:
        stack = app.by_object_name(locators.FXSTACK_STACK)
        rows = _list_items(stack)
        if not rows:
            break
        rows[0].click()
        app.by_object_name(locators.FXSTACK_REMOVE).click()
    yield


def _status_text(status):
    # Mac2 surfaces QLabel::text as AXValue, accessibleName as AXTitle.
    # Selenium's `.text` reads AXValue first; fall back to value attr.
    return (status.text or status.get_attribute("value") or "").strip()


def test_fx_params_status_label_resolvable(ready_app):
    """The status label resolves by objectName. QDockWidget itself is
    not surfaced under Mac AX with its objectName intact (the bridge
    folds it into the layout), so the status label is the closest
    proxy for confirming the panel was constructed.
    """
    status = ready_app.by_object_name(locators.FXPARAMS_STATUS)
    assert status is not None


def test_fx_params_status_shows_empty_stack(ready_app):
    """With no FX on the active plate's stack, status reports 'no FX'."""
    status = ready_app.by_object_name(locators.FXPARAMS_STATUS)
    text = _status_text(status)
    assert re.search(r"no FX on stack", text), (
        f"Expected empty-stack status, got: {text!r}"
    )


def test_fx_params_updates_after_add(ready_app):
    """Adding an FX via the FX Stack panel triggers a refresh; the
    status label switches to '<plate> — N FX, M params (read-only)'.
    Cross-validates the FXStackPanel_Qt::stackChanged → FXParamPanel
    refresh wiring.
    """
    available = ready_app.by_object_name(locators.FXSTACK_AVAILABLE)
    items = _list_items(available)
    assert items, "no Available FXs to test with"
    items[0].click()
    ready_app.by_object_name(locators.FXSTACK_ADD).click()

    status = ready_app.by_object_name(locators.FXPARAMS_STATUS)
    text = _status_text(status)
    m = re.search(r"(\d+) FX,\s*(\d+) params", text)
    assert m, f"Expected 'N FX, M params' status after add, got: {text!r}"
    assert int(m.group(1)) == 1, f"Expected 1 FX after add, got: {text!r}"

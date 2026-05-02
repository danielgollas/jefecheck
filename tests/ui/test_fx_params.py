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
    # Wrapped in retry because under sweep load the AX bridge can
    # 404 the fxstack list lookups for ~1s after a tab/dock-state
    # change in a prior test.
    import time

    app.wait_for_startup_ready()
    deadline = time.monotonic() + 8.0
    while time.monotonic() < deadline:
        try:
            stack = app.by_object_name(locators.FXSTACK_STACK)
            rows = _list_items(stack)
            if not rows:
                break
            rows[0].click()
            app.by_object_name(locators.FXSTACK_REMOVE).click()
        except AssertionError:
            time.sleep(0.3)
            continue
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
    status label switches to '<plate> — N FX, M params (X editable)'.
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


_BCS_TITLE = "Color/Brightness, Contrast, Saturation"


def _add_fx_by_title(app, title):
    """Find the Available-list row whose StaticText sibling carries
    `title` (i.e. matches the FX's menuName) and click the
    corresponding TableRow. Each QListWidget item has both a
    TableRow (clickable, no title) and a StaticText (read-only,
    has title) under Mac2; we walk static_texts to find the
    right index, then click items[idx] which is the TableRow at
    the same position.

    Wrapped in retry — under sweep load the AX tree can transiently
    miss either the available list itself (right after a dock-state
    change in a prior test) or specific StaticText titles (when the
    list got scrolled and only some rows are virtualized).
    """
    import time

    last_err = None
    for attempt in range(15):
        try:
            available = app.by_object_name(locators.FXSTACK_AVAILABLE)
            static_texts = available.find_elements(
                AppiumBy.IOS_PREDICATE, "elementType==48"
            )
            target_idx = None
            for i, st in enumerate(static_texts):
                if (st.get_attribute("title") or "") == title:
                    target_idx = i
                    break
            if target_idx is None:
                raise AssertionError(
                    f"FX {title!r} not in {len(static_texts)} static-text rows"
                )
            items = _list_items(available)
            assert target_idx < len(items), (
                f"target_idx {target_idx} out of range "
                f"({len(items)} items)"
            )
            items[target_idx].click()
            app.by_object_name(locators.FXSTACK_ADD).click()
            return
        except AssertionError as e:
            last_err = e
            time.sleep(0.3)
    raise AssertionError(f"_add_fx_by_title({title!r}) failed: {last_err}")


def test_fx_params_bcs_produces_editable_count(ready_app):
    """Adding the BCS FX (4 float widgets) must show ≥4 editable
    params in the status label. Confirms the bridge maps
    FX_GUI_FLOAT through to a QDoubleSpinBox editor for every float
    parameter, not just the first one.
    """
    _add_fx_by_title(ready_app, _BCS_TITLE)
    status = ready_app.by_object_name(locators.FXPARAMS_STATUS)
    text = _status_text(status)
    m = re.search(r"\((\d+) editable\)", text)
    assert m, f"Expected '(N editable)' status, got: {text!r}"
    assert int(m.group(1)) >= 4, (
        f"Expected ≥4 editable params for BCS, got: {text!r}"
    )


def test_fx_params_brightness_spinbox_resolvable(ready_app):
    """Adding BCS surfaces a QDoubleSpinBox at the per-param objectName
    `fxparams.fx0.param.Brightness.spin`. End-to-end: bridge metadata
    → panel editor construction → Qt AX layer.

    The find is wrapped in a short retry loop because under sweep-load
    the Mac2 AX cache occasionally lags one tick behind the panel
    rebuild — the predicate has resolved by ~500ms in every observed
    case.
    """
    import time

    _add_fx_by_title(ready_app, _BCS_TITLE)

    # Sanity-check that BCS landed on the stack before asserting on
    # the per-param widget — a selection-propagation failure would
    # show as a too-low FX count and that's the more useful error.
    status = ready_app.by_object_name(locators.FXPARAMS_STATUS)
    text = _status_text(status)
    m = re.search(r"(\d+) FX", text)
    assert m and int(m.group(1)) >= 1, (
        f"Expected stack to have ≥1 FX after add, got status: {text!r}"
    )

    last_err = None
    spin = None
    for _ in range(10):
        try:
            spin = ready_app.driver.find_element(
                AppiumBy.IOS_PREDICATE,
                "identifier ENDSWITH 'fxparams.fx0.param.Brightness.spin'",
            )
            break
        except Exception as e:
            last_err = e
            time.sleep(0.2)
    assert spin is not None, (
        f"Brightness spinbox not found after BCS add. status={text!r}\n"
        f"last_err={last_err}"
    )

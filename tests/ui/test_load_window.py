"""UI tests for the Load Sequence Manager (Cmd+L).

The dialog mediates the four-track sequence load preparation flow.
These tests exercise lifecycle (open, edit, dismiss vs Load All) and
the drop-forwarding contract. The single-frame drag-drop fast path
keeps its existing test coverage in test_load.py — those tests must
continue to pass after this PR.
"""
import time

import pytest

from jefecheck import locators


def _open_load_window(app):
    app.send_shortcut("cmd+L")
    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        dlg = app.by_object_name(locators.LOAD_WINDOW)
        if dlg:
            return dlg
        time.sleep(0.1)
    raise AssertionError("Load Window did not appear after Cmd+L")


def test_load_window_smoke(app):
    """Cmd+L opens the modal with four strips and a Load All button."""
    _open_load_window(app)
    for idx in range(4):
        strip = app.by_object_name(locators.LOAD_STRIP_FMT.format(idx=idx))
        assert strip is not None, f"Strip {idx} missing"
    btn = app.by_object_name(locators.LOAD_WINDOW_LOAD_ALL)
    assert btn is not None


def test_load_window_dismiss_no_load(app, multiview_sequence):
    """Open, edit Track A filename, dismiss via Esc → no load fires."""
    _open_load_window(app)
    field = app.by_object_name(locators.LOAD_FILENAME_FMT.format(idx=0))
    field.send_keys(str(multiview_sequence))
    field.send_keys("\t")  # editingFinished → trackEdited → preview decode

    deadline = time.monotonic() + 3.0
    est = ""
    while time.monotonic() < deadline:
        est = app.by_object_name(
            locators.LOAD_ESTIMATES_FMT.format(idx=0)
        ).get_attribute("value") or ""
        if est and est != "–":
            break
        time.sleep(0.1)
    assert est and est != "–", "Preview estimates did not populate"

    app.send_shortcut("Escape")
    time.sleep(0.3)

    loaded_label = app.by_object_name(locators.STATUSBAR_LOADED)
    assert (loaded_label.get_attribute("value") or "") == "Loaded: -"


def test_load_window_load_all(app, multiview_sequence):
    """Set Track A's filename and click Load All → loaded label populates."""
    _open_load_window(app)
    field = app.by_object_name(locators.LOAD_FILENAME_FMT.format(idx=0))
    field.send_keys(str(multiview_sequence))
    field.send_keys("\t")
    time.sleep(0.5)

    btn = app.by_object_name(locators.LOAD_WINDOW_LOAD_ALL)
    btn.click()

    deadline = time.monotonic() + 6.0
    loaded = ""
    while time.monotonic() < deadline:
        lbl = app.by_object_name(locators.STATUSBAR_LOADED)
        loaded = lbl.get_attribute("value") or ""
        if loaded and loaded != "Loaded: -":
            break
        time.sleep(0.2)
    assert loaded and loaded != "Loaded: -", \
        f"Active plate did not show a loaded sequence; status was: {loaded!r}"


@pytest.mark.xfail(reason="drop simulation helper TBD")
def test_load_window_drop_while_open(app, multiview_sequence):
    """Drop a file while the modal is open → strip A's filename populates."""
    _open_load_window(app)
    app.drop_file_on_viewport(str(multiview_sequence))

    deadline = time.monotonic() + 3.0
    text = ""
    while time.monotonic() < deadline:
        text = (
            app.by_object_name(locators.LOAD_FILENAME_FMT.format(idx=0))
            .get_attribute("value")
            or ""
        )
        if text:
            break
        time.sleep(0.1)
    assert str(multiview_sequence) in text


def test_load_window_bad_filename_marks_error(app):
    """Non-existent path → header turns red, estimates dash, no popup."""
    _open_load_window(app)
    field = app.by_object_name(locators.LOAD_FILENAME_FMT.format(idx=1))
    field.send_keys("/tmp/this-file-does-not-exist.0001.exr")
    field.send_keys("\t")
    time.sleep(0.5)

    header = app.by_object_name(locators.LOAD_HEADER_FMT.format(idx=1))
    assert "not found" in (header.get_attribute("value") or "").lower()
    est = app.by_object_name(locators.LOAD_ESTIMATES_FMT.format(idx=1))
    assert (est.get_attribute("value") or "").strip() == "–"

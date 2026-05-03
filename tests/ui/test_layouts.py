"""Layout shortcuts (Cmd+1..4) change framing mode.

The viewport widget itself doesn't appear in macOS's NSAccessibility
tree (QOpenGLWidget is opaque to AX), so the main window owns a
permanent status-bar label that mirrors the active framing mode.
"""
from jefecheck import locators


def _layout(app) -> str:
    label = app.by_object_name(locators.STATUSBAR_LAYOUT)
    # XCUITest exposes static-text widgets' content via the `value`
    # attribute (not `title`, which is for buttons/menu items).
    return label.get_attribute("value") or ""


def test_default_layout_is_single(app):
    assert _layout(app) == "Layout: single"


def test_cmd_2_switches_to_double_horizontal(app):
    app.send_shortcut("cmd+2")
    assert _layout(app) == "Layout: double-horizontal"


def test_cmd_3_switches_to_double_vertical(app):
    app.send_shortcut("cmd+3")
    assert _layout(app) == "Layout: double-vertical"


def test_cmd_4_switches_to_quad(app):
    app.send_shortcut("cmd+4")
    assert _layout(app) == "Layout: quad"


def test_cmd_1_returns_to_single(app):
    app.send_shortcut("cmd+4")
    assert _layout(app) == "Layout: quad"
    app.send_shortcut("cmd+1")
    assert _layout(app) == "Layout: single"

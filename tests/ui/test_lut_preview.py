"""LUT preview/inspector presence. The preview canvases (1D curve / 3D cube)
are painted/GL widgets that aren't AX-addressable, so the smoke test asserts
the container and toggle resolve. The 3D camera/render-modes/morph and the
.tga (UnitCube) load are verified manually."""
from jefecheck import locators


def test_lut_preview_toggle_present(app):
    """The LUT preview toggle is present and addressable."""
    toggle = app.by_object_name(locators.LUT_PREVIEW_TOGGLE)
    assert toggle is not None


def test_lut_preview_pane_present(app):
    """The LUT preview container is present and addressable."""
    pane = app.by_object_name(locators.LUT_PREVIEW)
    assert pane is not None

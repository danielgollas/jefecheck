// Abstract main window. Replaces direct use of MainWindow (Fl_Double_Window subclass).
//
// Phase 0 only requires the small surface that non-MainWindow code uses:
// show/hide, geometry, fullscreen, viewport access. The MainWindow class itself
// remains FLTK-specific until Phase 2E ports it to Qt.
#ifndef JEFECHECK_UI_IMAINWINDOW_H
#define JEFECHECK_UI_IMAINWINDOW_H

namespace jefe::ui {

class IGLViewport;

class IMainWindow {
public:
    virtual ~IMainWindow() = default;

    virtual void show() = 0;
    virtual void hide() = 0;

    virtual int x() const = 0;
    virtual int y() const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual void resize(int x, int y, int w, int h) = 0;

    virtual bool isFullscreen() const = 0;
    virtual void setFullscreen(bool on) = 0;

    virtual void setTitle(const char* title) = 0;

    // The OpenGL viewport embedded in this window.
    virtual IGLViewport& viewport() = 0;

    static IMainWindow& instance();
    static void setInstance(IMainWindow* impl);
};

}  // namespace jefe::ui

#endif

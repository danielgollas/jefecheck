// Abstract event loop / app singleton. Replaces ~85 Fl::check()/wait() and
// the Fl::run() / Fl::screen_xywh() / Fl::set_color() / Fl::scheme() calls
// scattered across main.cpp, gfcPlaybackManager, and the network managers.
#ifndef JEFECHECK_UI_IAPPLICATION_H
#define JEFECHECK_UI_IAPPLICATION_H

#include <functional>

namespace jefe::ui {

struct ScreenGeometry {
    int x = 0, y = 0;
    int width = 0, height = 0;
};

class IApplication {
public:
    virtual ~IApplication() = default;

    // Process queued events without blocking (Fl::check() equivalent).
    // Returns true if the app should keep running.
    virtual bool processEvents() = 0;

    // Block until the next event arrives, or timeout (seconds, <0 = forever).
    // Returns true if the app should keep running.
    virtual bool waitForEvents(double timeoutSeconds = -1.0) = 0;

    // Run the main event loop until quit() is called.
    virtual int run() = 0;

    // Request app shutdown. The current run() returns at the next loop tick.
    virtual void quit() = 0;

    // Geometry of the screen containing (x,y), in pixels.
    virtual ScreenGeometry screenGeometryAt(int x, int y) const = 0;

    // Schedule fn to run after delaySeconds, on the main thread / event loop.
    // Returns an opaque handle that can be passed to cancelTimer().
    using TimerId = int;
    virtual TimerId scheduleTimer(double delaySeconds, std::function<void()> fn) = 0;
    virtual void cancelTimer(TimerId id) = 0;

    static IApplication& instance();
    static void setInstance(IApplication* impl);
};

}  // namespace jefe::ui

#endif

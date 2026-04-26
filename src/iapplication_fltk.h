// FLTK backend for jefe::ui::IApplication.
// Wraps Fl::check / Fl::wait / Fl::run / Fl::screen_xywh /
// Fl::add_timeout / Fl::remove_timeout. Singleton, registered in main.cpp.
#ifndef IAPPLICATION_FLTK_H
#define IAPPLICATION_FLTK_H

#include "ui/IApplication.h"

class IApplication_FLTK : public jefe::ui::IApplication {
public:
    bool processEvents() override;
    bool waitForEvents(double timeoutSeconds = -1.0) override;
    int run() override;
    void quit() override;
    jefe::ui::ScreenGeometry screenGeometryAt(int x, int y) const override;
    TimerId scheduleTimer(double delaySeconds, std::function<void()> fn) override;
    void cancelTimer(TimerId id) override;

private:
    bool m_quitRequested = false;
};

#endif

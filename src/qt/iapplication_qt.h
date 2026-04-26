// Qt backend for jefe::ui::IApplication. Wraps QApplication / QTimer.
// Singleton, registered in main_qt.cpp.
#ifndef IAPPLICATION_QT_H
#define IAPPLICATION_QT_H

#include "ui/IApplication.h"

class QApplication;

class IApplication_Qt : public jefe::ui::IApplication {
public:
    explicit IApplication_Qt(QApplication* qapp);

    bool processEvents() override;
    bool waitForEvents(double timeoutSeconds = -1.0) override;
    int run() override;
    void quit() override;
    jefe::ui::ScreenGeometry screenGeometryAt(int x, int y) const override;
    TimerId scheduleTimer(double delaySeconds, std::function<void()> fn) override;
    void cancelTimer(TimerId id) override;

private:
    QApplication* qapp_ = nullptr;
};

#endif

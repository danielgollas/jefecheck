#include "iapplication_qt.h"

#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <QScreen>

#include <unordered_map>
#include <memory>

namespace ui = jefe::ui;

namespace {
struct TimerEntry {
    int id = 0;
    std::unique_ptr<QTimer> timer;
};

std::unordered_map<int, TimerEntry>& timerRegistry() {
    static std::unordered_map<int, TimerEntry> r;
    return r;
}

int& nextTimerId() {
    static int id = 1;
    return id;
}
}

IApplication_Qt::IApplication_Qt(QApplication* qapp) : qapp_(qapp) {}

bool IApplication_Qt::processEvents() {
    QApplication::processEvents();
    return true;
}

bool IApplication_Qt::waitForEvents(double timeoutSeconds) {
    if (timeoutSeconds < 0.0) {
        QApplication::processEvents(QEventLoop::WaitForMoreEvents);
    } else {
        int ms = static_cast<int>(timeoutSeconds * 1000.0);
        if (ms <= 0) ms = 1;
        QApplication::processEvents(QEventLoop::WaitForMoreEvents, ms);
    }
    return true;
}

int IApplication_Qt::run() {
    return QApplication::exec();
}

void IApplication_Qt::quit() {
    QApplication::quit();
}

ui::ScreenGeometry IApplication_Qt::screenGeometryAt(int x, int y) const {
    QScreen* screen = QGuiApplication::screenAt(QPoint(x, y));
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return {};
    QRect g = screen->geometry();
    return { g.x(), g.y(), g.width(), g.height() };
}

ui::IApplication::TimerId IApplication_Qt::scheduleTimer(double delaySeconds, std::function<void()> fn) {
    int id = nextTimerId()++;
    auto& entry = timerRegistry()[id];
    entry.id = id;
    entry.timer = std::make_unique<QTimer>();
    entry.timer->setSingleShot(true);
    QObject::connect(entry.timer.get(), &QTimer::timeout, [id, fn = std::move(fn)]() {
        if (fn) fn();
        timerRegistry().erase(id);
    });
    entry.timer->start(static_cast<int>(delaySeconds * 1000.0));
    return id;
}

void IApplication_Qt::cancelTimer(TimerId id) {
    auto it = timerRegistry().find(id);
    if (it == timerRegistry().end()) return;
    if (it->second.timer) it->second.timer->stop();
    timerRegistry().erase(it);
}

#include "iapplication_fltk.h"
#include <FL/Fl.H>
#include <unordered_map>
#include <memory>

namespace ui = jefe::ui;

namespace {

// Fl::add_timeout takes a void* trampoline. We heap-allocate a struct that
// owns the std::function and a back-pointer to the registry, so we can
// resolve the TimerId on cancel and clean up when the timeout fires.
struct TimerEntry {
    int id;
    std::function<void()> fn;
};

// Static registry — TimerIds are stable handles even if the entry is freed.
std::unordered_map<int, std::unique_ptr<TimerEntry>>& timerRegistry() {
    static std::unordered_map<int, std::unique_ptr<TimerEntry>> r;
    return r;
}

int& nextTimerId() {
    static int id = 1;
    return id;
}

void timerTrampoline(void* data) {
    auto* entry = static_cast<TimerEntry*>(data);
    auto& reg = timerRegistry();
    auto it = reg.find(entry->id);
    if (it == reg.end()) return;  // cancelled before fire
    auto fn = std::move(entry->fn);
    reg.erase(it);
    if (fn) fn();
}

}  // namespace

bool IApplication_FLTK::processEvents() {
    Fl::check();
    return !m_quitRequested;
}

bool IApplication_FLTK::waitForEvents(double timeoutSeconds) {
    if (timeoutSeconds < 0.0) {
        Fl::wait();
    } else {
        Fl::wait(timeoutSeconds);
    }
    return !m_quitRequested;
}

int IApplication_FLTK::run() {
    while (!m_quitRequested && Fl::wait() > 0) {
        // Fl::wait() processes one batch of events per call.
    }
    return 0;
}

void IApplication_FLTK::quit() {
    m_quitRequested = true;
}

ui::ScreenGeometry IApplication_FLTK::screenGeometryAt(int x, int y) const {
    int sx = 0, sy = 0, sw = 0, sh = 0;
    Fl::screen_xywh(sx, sy, sw, sh, x, y);
    return { sx, sy, sw, sh };
}

ui::IApplication::TimerId IApplication_FLTK::scheduleTimer(double delaySeconds, std::function<void()> fn) {
    int id = nextTimerId()++;
    auto entry = std::make_unique<TimerEntry>();
    entry->id = id;
    entry->fn = std::move(fn);
    void* data = entry.get();
    timerRegistry()[id] = std::move(entry);
    Fl::add_timeout(delaySeconds, timerTrampoline, data);
    return id;
}

void IApplication_FLTK::cancelTimer(TimerId id) {
    auto& reg = timerRegistry();
    auto it = reg.find(id);
    if (it == reg.end()) return;
    Fl::remove_timeout(timerTrampoline, it->second.get());
    reg.erase(it);
}

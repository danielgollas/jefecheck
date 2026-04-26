#include "IEventSystem.h"
#include <cassert>

namespace jefe::ui {

namespace {
IEventSystem*& slot() {
    static IEventSystem* p = nullptr;
    return p;
}
}

IEventSystem& IEventSystem::instance() {
    assert(slot() && "IEventSystem::instance() called before setInstance()");
    return *slot();
}

void IEventSystem::setInstance(IEventSystem* impl) {
    slot() = impl;
}

}  // namespace jefe::ui

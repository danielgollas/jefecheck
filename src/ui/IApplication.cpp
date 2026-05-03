#include "IApplication.h"
#include <cassert>

namespace jefe::ui {

namespace {
IApplication*& slot() {
    static IApplication* p = nullptr;
    return p;
}
}

IApplication& IApplication::instance() {
    assert(slot() && "IApplication::instance() called before setInstance()");
    return *slot();
}

void IApplication::setInstance(IApplication* impl) {
    slot() = impl;
}

}  // namespace jefe::ui

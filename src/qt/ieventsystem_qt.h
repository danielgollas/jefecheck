// Qt backend for jefe::ui::IEventSystem.
// Minimal stub — for the first Qt build the only consumers are tests/skeletons,
// not actual callbacks. The real port will populate state from QEvent dispatch.
#ifndef IEVENTSYSTEM_QT_H
#define IEVENTSYSTEM_QT_H

#include "ui/IEventSystem.h"

class IEventSystem_Qt : public jefe::ui::IEventSystem {
public:
    int mouseX() const override { return 0; }
    int mouseY() const override { return 0; }
    int mouseRootX() const override { return 0; }
    int mouseRootY() const override { return 0; }
    jefe::ui::MouseButton mouseButton() const override { return jefe::ui::MouseButton::None; }
    bool isMouseButtonDown(jefe::ui::MouseButton) const override { return false; }
    int clickCount() const override { return 0; }
    int wheelDeltaY() const override { return 0; }
    jefe::ui::ModifierMask modifiers() const override { return jefe::ui::ModifierMask::None; }
    jefe::ui::EventType currentEventType() const override { return jefe::ui::EventType::Unknown; }
    jefe::ui::Key currentKey() const override { return jefe::ui::Key::Unknown; }
    bool isKeyDown(jefe::ui::Key) const override { return false; }
    std::string currentText() const override { return {}; }
    bool isPointerInside(int, int, int, int) const override { return false; }
};

#endif

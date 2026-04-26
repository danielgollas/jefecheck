// FLTK backend for jefe::ui::IEventSystem.
// Wraps Fl::event_* / Fl::get_key. Singleton, registered in main.cpp.
#ifndef IEVENTSYSTEM_FLTK_H
#define IEVENTSYSTEM_FLTK_H

#include "ui/IEventSystem.h"

class IEventSystem_FLTK : public jefe::ui::IEventSystem {
public:
    int mouseX() const override;
    int mouseY() const override;
    int mouseRootX() const override;
    int mouseRootY() const override;
    jefe::ui::MouseButton mouseButton() const override;
    bool isMouseButtonDown(jefe::ui::MouseButton button) const override;
    int clickCount() const override;
    int wheelDeltaY() const override;
    jefe::ui::ModifierMask modifiers() const override;
    jefe::ui::EventType currentEventType() const override;
    jefe::ui::Key currentKey() const override;
    bool isKeyDown(jefe::ui::Key key) const override;
    std::string currentText() const override;
    bool isPointerInside(int x, int y, int w, int h) const override;
};

#endif

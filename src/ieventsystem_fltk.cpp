#include "ieventsystem_fltk.h"
#include <FL/Fl.H>
#include <FL/Enumerations.H>

namespace ui = jefe::ui;

int IEventSystem_FLTK::mouseX() const     { return Fl::event_x(); }
int IEventSystem_FLTK::mouseY() const     { return Fl::event_y(); }
int IEventSystem_FLTK::mouseRootX() const { return Fl::event_x_root(); }
int IEventSystem_FLTK::mouseRootY() const { return Fl::event_y_root(); }

ui::MouseButton IEventSystem_FLTK::mouseButton() const {
    switch (Fl::event_button()) {
    case FL_LEFT_MOUSE:   return ui::MouseButton::Left;
    case FL_MIDDLE_MOUSE: return ui::MouseButton::Middle;
    case FL_RIGHT_MOUSE:  return ui::MouseButton::Right;
    default:              return ui::MouseButton::None;
    }
}

bool IEventSystem_FLTK::isMouseButtonDown(ui::MouseButton button) const {
    int s = Fl::event_state();
    switch (button) {
    case ui::MouseButton::Left:   return (s & FL_BUTTON1) != 0;
    case ui::MouseButton::Middle: return (s & FL_BUTTON2) != 0;
    case ui::MouseButton::Right:  return (s & FL_BUTTON3) != 0;
    default: return false;
    }
}

int IEventSystem_FLTK::clickCount() const  { return Fl::event_clicks(); }
int IEventSystem_FLTK::wheelDeltaY() const { return Fl::event_dy(); }

ui::ModifierMask IEventSystem_FLTK::modifiers() const {
    int s = Fl::event_state();
    unsigned m = 0;
    if (s & FL_SHIFT) m |= static_cast<unsigned>(ui::ModifierMask::Shift);
    if (s & FL_CTRL)  m |= static_cast<unsigned>(ui::ModifierMask::Ctrl);
    if (s & FL_ALT)   m |= static_cast<unsigned>(ui::ModifierMask::Alt);
    if (s & FL_META)  m |= static_cast<unsigned>(ui::ModifierMask::Meta);
    return static_cast<ui::ModifierMask>(m);
}

ui::EventType IEventSystem_FLTK::currentEventType() const {
    switch (Fl::event()) {
    case FL_PUSH:        return ui::EventType::Push;
    case FL_RELEASE:     return ui::EventType::Release;
    case FL_DRAG:        return ui::EventType::Drag;
    case FL_MOVE:        return ui::EventType::Move;
    case FL_ENTER:       return ui::EventType::Enter;
    case FL_LEAVE:       return ui::EventType::Leave;
    case FL_MOUSEWHEEL:  return ui::EventType::Wheel;
    case FL_KEYDOWN:     return ui::EventType::KeyDown;
    case FL_KEYUP:       return ui::EventType::KeyUp;
    case FL_FOCUS:       return ui::EventType::Focus;
    case FL_UNFOCUS:     return ui::EventType::Unfocus;
    case FL_PASTE:       return ui::EventType::Paste;
    default:             return ui::EventType::Unknown;
    }
}

ui::Key IEventSystem_FLTK::currentKey() const {
    return static_cast<ui::Key>(Fl::event_key());
}

bool IEventSystem_FLTK::isKeyDown(ui::Key key) const {
    return Fl::get_key(static_cast<int>(key)) != 0;
}

std::string IEventSystem_FLTK::currentText() const {
    const char* t = Fl::event_text();
    int n = Fl::event_length();
    if (!t || n <= 0) return {};
    return std::string(t, n);
}

bool IEventSystem_FLTK::isPointerInside(int x, int y, int w, int h) const {
    return Fl::event_inside(x, y, w, h) != 0;
}

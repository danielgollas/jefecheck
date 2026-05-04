// Abstract event-system facade. Replaces 150+ Fl::event_* call sites.
// Backend implementations: src/gfcui_fltk.cpp (FLTK), src/qt/IEventSystem_qt.cpp (Qt).
#ifndef JEFECHECK_UI_IEVENTSYSTEM_H
#define JEFECHECK_UI_IEVENTSYSTEM_H

#include <string>

namespace jefe::ui {

enum class MouseButton {
    None  = 0,
    Left  = 1,
    Middle = 2,
    Right = 3,
};

enum class ModifierMask : unsigned {
    None  = 0,
    Shift = 1u << 0,
    Ctrl  = 1u << 1,
    Alt   = 1u << 2,
    Meta  = 1u << 3,  // Cmd on macOS, Win on Windows, Super on Linux
};

inline ModifierMask operator|(ModifierMask a, ModifierMask b) {
    return static_cast<ModifierMask>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
inline bool any(ModifierMask m, ModifierMask test) {
    return (static_cast<unsigned>(m) & static_cast<unsigned>(test)) != 0;
}

// Logical key codes. Mirrors the subset of FLTK key codes the app actually uses.
// Printable ASCII keys are passed as their char value (cast to int).
enum class Key : int {
    Unknown = 0,

    Space = ' ',
    Enter = 0xff0d,
    Escape = 0xff1b,
    Tab = 0xff09,
    BackSpace = 0xff08,
    Delete = 0xffff,

    Left  = 0xff51,
    Up    = 0xff52,
    Right = 0xff53,
    Down  = 0xff54,

    PageUp   = 0xff55,
    PageDown = 0xff56,
    Home     = 0xff50,
    End      = 0xff57,

    F1  = 0xffbe,
    F2,  F3,  F4,  F5,  F6,
    F7,  F8,  F9,  F10, F11, F12,

    ShiftL = 0xffe1, ShiftR = 0xffe2,
    CtrlL  = 0xffe3, CtrlR  = 0xffe4,
    AltL   = 0xffe9, AltR   = 0xffea,
    MetaL  = 0xffeb, MetaR  = 0xffec,
};

class IEventSystem {
public:
    virtual ~IEventSystem() = default;

    // Pointer position relative to the window receiving the current event.
    virtual int mouseX() const = 0;
    virtual int mouseY() const = 0;

    // Pointer position in screen coordinates.
    virtual int mouseRootX() const = 0;
    virtual int mouseRootY() const = 0;

    // The mouse button that triggered the current event, or None.
    virtual MouseButton mouseButton() const = 0;

    // Click count for the current press event (1, 2, 3 for single/double/triple).
    virtual int clickCount() const = 0;

    // Vertical wheel delta for the current scroll event. Positive = up.
    virtual int wheelDeltaY() const = 0;

    // Modifier mask for the current event.
    virtual ModifierMask modifiers() const = 0;
    bool isShift() const { return any(modifiers(), ModifierMask::Shift); }
    bool isCtrl()  const { return any(modifiers(), ModifierMask::Ctrl); }
    bool isAlt()   const { return any(modifiers(), ModifierMask::Alt); }
    bool isMeta()  const { return any(modifiers(), ModifierMask::Meta); }

    // The key for the current keyboard event, or Unknown.
    virtual Key currentKey() const = 0;

    // Live-state query: is this key currently held down?
    virtual bool isKeyDown(Key key) const = 0;

    // Text payload of the current event (e.g. typed character or pasted text).
    virtual std::string currentText() const = 0;

    // True if (rootX,rootY) is inside the rectangle (x,y,w,h) in window coords.
    virtual bool isPointerInside(int x, int y, int w, int h) const = 0;

    // Singleton accessor — set by the active backend at startup.
    static IEventSystem& instance();
    static void setInstance(IEventSystem* impl);
};

}  // namespace jefe::ui

#endif

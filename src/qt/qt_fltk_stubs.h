// Stub definitions of the FLTK widget classes that survive in the Qt
// build's compile units only because some manager headers still declare
// `Fl_*` widget pointer fields. Nothing in the Qt build actually
// constructs or dereferences these — every method here is a no-op.
//
// Each stub matches the slice of the FLTK API our code calls so the .cpp
// files compile without the real <FL/*> headers. The instances themselves
// remain null in Qt-build code paths and are gated behind
// JEFECHECK_USE_FLTK at every actual call site.
#ifndef JEFECHECK_QT_FLTK_STUBS_H
#define JEFECHECK_QT_FLTK_STUBS_H

#ifndef JEFECHECK_USE_FLTK

#include <glad/glad.h>

// Minimal FLTK enum stubs used by FLTK-free .cpps that still pass FL_*
// constants to portable helpers like gfc_gl_font / gfc_gl_draw.
// Values mirror real FLTK so call-site behavior stays equivalent.
enum {
    FL_HELVETICA      = 0,
    FL_HELVETICA_BOLD = 1,
    FL_COURIER        = 4,
    FL_TIMES          = 8,
};
enum {
    FL_ALIGN_CENTER = 0x0000,
    FL_ALIGN_TOP    = 0x0001,
    FL_ALIGN_BOTTOM = 0x0002,
    FL_ALIGN_LEFT   = 0x0004,
    FL_ALIGN_RIGHT  = 0x0008,
    FL_ALIGN_INSIDE = 0x0010,
    FL_ALIGN_WRAP   = 0x0080,
};

class Fl_Widget {
public:
    void label(const char*) {}
    void copy_label(const char*) {}
    void labelcolor(unsigned int) {}
    void value(float) {}
    void redraw() {}
    void tooltip(const char*) {}
    void user_data(void*) {}
    void color(unsigned int) {}
    void selection_color(unsigned int) {}
    void resize(int, int, int, int) {}
};

class Fl_Group {
public:
    void clear() {}
};

class Fl_Scroll {
public:
    void clear() {}
    void scroll_to(int, int) {}
    int xposition() const { return 0; }
    int yposition() const { return 0; }
};

class Fl_Button {
public:
    void value(int) {}
    int value() const { return 0; }
    void activate() {}
    void deactivate() {}
};

class Fl_Progress {
public:
    void label(const char*) {}
    void copy_label(const char*) {}
    void labelcolor(unsigned int) {}
    void maximum(float) {}
    float maximum() const { return 0.0f; }
    void color(unsigned int) {}
    void selection_color(unsigned int) {}
    void value(float) {}
    float value() const { return 0.0f; }
};

inline unsigned int fl_rgb_color(unsigned int r, unsigned int g, unsigned int b) {
    return ((r&0xff)<<24) | ((g&0xff)<<16) | ((b&0xff)<<8);
}

// FLTK gl_rectf / gl_rect helpers — minimal GL replacements so call sites
// in FLTK-free .cpps still draw the right shapes in the Qt build.
inline void gl_rectf(int x, int y, int w, int h) {
    glRectf((float)x, (float)y, (float)(x + w), (float)(y + h));
}
inline void gl_rect(int x, int y, int w, int h) {
    glBegin(GL_LINE_LOOP);
    glVertex2f((float)x,        (float)y);
    glVertex2f((float)(x + w),  (float)y);
    glVertex2f((float)(x + w),  (float)(y + h));
    glVertex2f((float)x,        (float)(y + h));
    glEnd();
}

#endif  // !JEFECHECK_USE_FLTK
#endif  // JEFECHECK_QT_FLTK_STUBS_H

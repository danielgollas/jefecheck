#include "Fl_Button_gfc.h"
#include <FL/Fl.H>

Fl_Button_gfc::Fl_Button_gfc(int X, int Y, int W, int H, const char *l):Fl_Button(X,Y,W,H,l) {

}


void Fl_Button_gfc::draw() {

    labelcolor(fl_rgb_color(161,160,160));
    selection_color(fl_rgb_color(90,90,90));
    color(fl_rgb_color(42,42,42));
    if (type() == FL_HIDDEN_BUTTON) return;
    Fl_Color col = value() ? selection_color() : color();

    //draw_box(value() ? (down_box()?down_box():fl_down(box())) : box(), col);
    draw_box(FL_FLAT_BOX, col);
    if (labeltype() == FL_NORMAL_LABEL && value()) {
        Fl_Color c = labelcolor();
        labelcolor(fl_contrast(c, col));
        draw_label();
        labelcolor(c);
    } else draw_label();
    if (Fl::focus() == this) draw_focus();
}

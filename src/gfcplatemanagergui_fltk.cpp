#include "gfcplatemanagergui_fltk.h"

gfcPlateManagerGUI_FLTK::gfcPlateManagerGUI_FLTK()
    : layoutsGroup(nullptr), layoutChoice(nullptr) {}

gfcPlateManagerGUI_FLTK::~gfcPlateManagerGUI_FLTK() {}

int gfcPlateManagerGUI_FLTK::getLayoutChoice() {
    return layoutChoice ? layoutChoice->value() : 0;
}

void gfcPlateManagerGUI_FLTK::setLayoutChoice(int value) {
    if (layoutChoice) layoutChoice->value(value);
}

int gfcPlateManagerGUI_FLTK::getLayoutGroupX() { return layoutsGroup ? layoutsGroup->x() : 0; }
int gfcPlateManagerGUI_FLTK::getLayoutGroupY() { return layoutsGroup ? layoutsGroup->y() : 0; }
int gfcPlateManagerGUI_FLTK::getLayoutGroupW() { return layoutsGroup ? layoutsGroup->w() : 0; }
int gfcPlateManagerGUI_FLTK::getLayoutGroupH() { return layoutsGroup ? layoutsGroup->h() : 0; }

void gfcPlateManagerGUI_FLTK::redrawLayoutGroup() {
    if (layoutsGroup) layoutsGroup->redraw();
}

void gfcPlateManagerGUI_FLTK::assignLayoutGroupWidget(void* widget) {
    layoutsGroup = static_cast<Fl_Group*>(widget);
}

void gfcPlateManagerGUI_FLTK::assignLayoutChoiceWidget(void* widget) {
    layoutChoice = static_cast<Fl_Choice_gfc*>(widget);
}

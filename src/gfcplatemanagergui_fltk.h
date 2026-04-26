#ifndef GFCPLATEMANAGERGUI_FLTK_H
#define GFCPLATEMANAGERGUI_FLTK_H

#include "gfcplatemanagergui.h"
#include <FL/Fl_Group.H>
#include "Fl_Choice_gfc.h"

class gfcPlateManagerGUI_FLTK : public gfcPlateManagerGUI {
public:
    gfcPlateManagerGUI_FLTK();
    ~gfcPlateManagerGUI_FLTK();

    int getLayoutChoice() override;
    void setLayoutChoice(int value) override;

    int getLayoutGroupX() override;
    int getLayoutGroupY() override;
    int getLayoutGroupW() override;
    int getLayoutGroupH() override;

    void redrawLayoutGroup() override;

    void assignLayoutGroupWidget(void* widget) override;
    void assignLayoutChoiceWidget(void* widget) override;

private:
    Fl_Group* layoutsGroup;
    Fl_Choice_gfc* layoutChoice;
};

#endif

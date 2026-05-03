// Abstract GUI surface for gfcPlateManager. Concrete impls in
// src/gfcplatemanagergui_fltk.cpp (FLTK) and src/qt/gfcplatemanagergui_qt.cpp.
//
// Owns the multi-plate layout chooser and the geometry of the layouts group
// container. Routes the few Fl_Group / Fl_Choice_gfc accesses gfcPlateManager
// previously did directly.
#ifndef GFCPLATEMANAGERGUI_H
#define GFCPLATEMANAGERGUI_H

class gfcPlateManagerGUI {
public:
    gfcPlateManagerGUI();
    virtual ~gfcPlateManagerGUI();

    // The current selected layout (0=1x1, 1=2x1, 2=1x2, 3=2x2).
    virtual int getLayoutChoice() = 0;
    virtual void setLayoutChoice(int value) = 0;

    // Geometry of the layouts container in the main window's coordinate space.
    virtual int getLayoutGroupX() = 0;
    virtual int getLayoutGroupY() = 0;
    virtual int getLayoutGroupW() = 0;
    virtual int getLayoutGroupH() = 0;

    // Force a redraw of the layouts container.
    virtual void redrawLayoutGroup() = 0;

    // Wire the abstract surface to backend-specific widget pointers.
    virtual void assignLayoutGroupWidget(void* widget) = 0;
    virtual void assignLayoutChoiceWidget(void* widget) = 0;
};

#endif

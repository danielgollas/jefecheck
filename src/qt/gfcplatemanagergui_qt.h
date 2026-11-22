// SPDX-License-Identifier: GPL-2.0-or-later
// Qt skeleton for gfcPlateManagerGUI. See docs/MIGRATION.md.
#ifndef GFCPLATEMANAGERGUI_QT_H
#define GFCPLATEMANAGERGUI_QT_H

#include "gfcplatemanagergui.h"

class gfcPlateManagerGUI_Qt : public gfcPlateManagerGUI {
public:
    gfcPlateManagerGUI_Qt();
    ~gfcPlateManagerGUI_Qt();

    int getLayoutChoice() override;
    void setLayoutChoice(int value) override;

    int getLayoutGroupX() override;
    int getLayoutGroupY() override;
    int getLayoutGroupW() override;
    int getLayoutGroupH() override;

    void redrawLayoutGroup() override;

    void assignLayoutGroupWidget(void*) override;
    void assignLayoutChoiceWidget(void*) override;

private:
    // gfcPlateManager::setFramingMode writes the layout here and updateAllFromGUI
    // reads it back, so it has to round-trip: a stub returning 0 reset every
    // loaded session to the single-plate layout.
    int layoutChoice_ = 0;
};

#endif

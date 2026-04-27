// FX Stack and LUT panels. Each is a QWidget hosted inside its own
// QDockWidget; the two docks are stacked into a tab group at startup.
// Bodies are placeholders until the FX/LUT managers come online in the
// Qt build.
#ifndef JEFECHECK_QT_FX_LUT_PANEL_H
#define JEFECHECK_QT_FX_LUT_PANEL_H

#include <QWidget>

class FXStackPanel_Qt : public QWidget {
    Q_OBJECT
public:
    explicit FXStackPanel_Qt(QWidget* parent = nullptr);
};

class LUTPanel_Qt : public QWidget {
    Q_OBJECT
public:
    explicit LUTPanel_Qt(QWidget* parent = nullptr);
};

#endif

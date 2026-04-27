// FX Stack and LUT panels. Each is a QWidget hosted inside its own
// QDockWidget; the two docks are stacked into a tab group at startup.
// FX Stack stays a placeholder for now (PR follow-up). LUTPanel_Qt is
// real: lists loaded LUTs from lutManager, accepts dropped LUT files,
// and applies a selection to the active plate.
#ifndef JEFECHECK_QT_FX_LUT_PANEL_H
#define JEFECHECK_QT_FX_LUT_PANEL_H

#include <QWidget>

class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QListWidget;
class QListWidgetItem;

class FXStackPanel_Qt : public QWidget {
    Q_OBJECT
public:
    explicit FXStackPanel_Qt(QWidget* parent = nullptr);
};

class LUTPanel_Qt : public QWidget {
    Q_OBJECT
public:
    explicit LUTPanel_Qt(QWidget* parent = nullptr);

public slots:
    // Pulls the LUT name list from lutManager (via the bridge) and
    // refreshes the visible list. Called at construction and after
    // every drop. Cheap; no need to call from the per-tick.
    void refreshList();

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void applySelected();

    QListWidget* list_ = nullptr;
    QLabel* status_ = nullptr;
};

#endif

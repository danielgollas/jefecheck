// Plate Manager dock content: a scroll area of plate cards. The mode (Active /
// Visible / All) is chosen from the View menu or this panel's context menu (the
// same three actions); the viewport mini-map sits in the dock's title bar.
// Which cards show and where they sit comes from jefe::platelayout::arrange,
// driven by the mode, the viewport framing mode, the active plate and the dock
// shape (row for a top/bottom dock, column for a side dock, map when floating).
// The panel fixes its own extent to the packed cards (the column scrolls).
#ifndef JEFECHECK_QT_PLATE_MANAGER_H
#define JEFECHECK_QT_PLATE_MANAGER_H

#include <QList>
#include <QPoint>
#include <QSize>
#include <QVector>
#include <QWidget>

#include "../gfcPlateLayout.h"

class QAction;
class QActionGroup;
class QMenu;
class QGridLayout;
class QScrollArea;
class PlateCard_Qt;
class PlateMiniMap_Qt;

class PlateManager_Qt : public QWidget {
    Q_OBJECT
public:
    explicit PlateManager_Qt(QWidget* parent = nullptr);

    // Which plates get a card. Persisted as QSettings "PlateManager/mode"
    // (0 Active, 1 Visible, 2 All); the constructor reads it, default Visible.
    void setMode(jefe::platelayout::PlateMode mode);
    jefe::platelayout::PlateMode mode() const { return mode_; }

    // Row (top/bottom dock, wide-short cards), Column (side dock, narrow-tall
    // cards, scrolls) or Floating (viewport-shaped map of narrow-tall cards).
    void setDockShape(jefe::platelayout::DockShape shape);
    jefe::platelayout::DockShape dockShape() const { return shape_; }

    // Width available to a Row; sets how many cards fit per row (1..4).
    // Until it is set (or when px < 0) a Row holds 4 cards. A Row shows at
    // most two card rows; more scroll.
    void setRowBudget(int px);

    // Reflows when the framing mode, active plate, mode, shape or row column
    // count differ from the last layout; otherwise only updates the active
    // highlight and the mini-map (a few int compares when nothing changed).
    void syncToViewport();

    // Plates that have a card, in arrangement order.
    QList<int> shownPlateIds() const { return shown_; }
    // Grid cell of a plate's card (x = column, y = row); (-1,-1) when hidden.
    QPoint cardCell(int plate) const;
    bool cardOffscreen(int plate) const;
    // Created by the panel, shown by whoever places it (the dock's title bar).
    PlateMiniMap_Qt* miniMap() const { return miniMap_; }
    // The three exclusive, checkable mode actions (Active, Visible, All), objectNames
    // menu.view.platemanager.active|visible|all; checked state follows setMode.
    QList<QAction*> modeActions() const { return {modeActions_[0], modeActions_[1], modeActions_[2]}; }
    // The right-click menu: a "Plate Manager Shows" section with the mode actions.
    QMenu* buildContextMenu(QWidget* parent);

    QSize sizeHint() const override;

protected:
    void contextMenuEvent(QContextMenuEvent* e) override;

signals:
    // Emitted after every reflow.
    void arrangementChanged();

public slots:
    // Syncs to the viewport first, then pulls fresh state from each card's
    // bound gfcPlateGUI_Qt into its widgets (hidden cards too, so a card shown
    // later is never stale). Called when the viewport mutates plate state
    // outside the cards (mouse drag, wheel zoom, keyboard shortcuts, layout
    // changes, session load).
    void refreshAllCards();

    // Lightweight refresh of just the dragged plate's transform spinboxes.
    // Wired to GlViewport_Qt::plateTransformChanged during pan/zoom drag.
    void refreshPlateTransform(int plateIdx);

    // Sibling of refreshPlateTransform for color-correction drags
    // (W/E/Q/D/S key+drag).
    void refreshPlateColor(int plateIdx);

private:
    void reflow(int framingMode, int activePlate, int cols);
    void updateHighlights(int framingMode, int activePlate);
    // Columns a Row may use: 4 until a budget is set, else rowMaxCols.
    int rowCols() const;
    // Pins the panel's fixed extent to the packed cards (called by reflow, and
    // deferred a tick to re-measure after dock transitions settle, §11).
    void applyFixedExtent();
    void activatePlate(int plate);

    QActionGroup* modeGroup_ = nullptr;
    QAction* modeActions_[3] = {nullptr, nullptr, nullptr};
    PlateMiniMap_Qt* miniMap_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QWidget* inner_ = nullptr;
    QGridLayout* grid_ = nullptr;
    QList<PlateCard_Qt*> cards_;

    jefe::platelayout::PlateMode mode_ = jefe::platelayout::PlateMode::Visible;
    jefe::platelayout::DockShape shape_ = jefe::platelayout::DockShape::Row;
    int rowBudget_ = -1;
    int rowCardW_ = 0;   // wide-short card width, measured while in Row

    // What the current arrangement was built from.
    bool laid_ = false;
    int lastFraming_ = -1;
    int lastActive_ = -1;
    int lastCols_ = -1;
    jefe::platelayout::PlateMode lastMode_ = jefe::platelayout::PlateMode::Visible;
    jefe::platelayout::DockShape lastShape_ = jefe::platelayout::DockShape::Row;

    QList<int> shown_;
    QVector<QPoint> cells_;
    QSize extent_;
};

#endif

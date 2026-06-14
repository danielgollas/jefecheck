// Plate Manager dock content. Hosts the plate cards in a single row (1×N,
// horizontal dock) or a single column (N×1, vertical dock). Orientation is
// driven by the dock edge (setOrientation), not by width — the panel fixes
// its cross-axis extent to one card and scrolls the long axis.
#ifndef JEFECHECK_QT_PLATE_MANAGER_H
#define JEFECHECK_QT_PLATE_MANAGER_H

#include <QList>
#include <QScrollArea>

class QGridLayout;
class PlateCard_Qt;

class PlateManager_Qt : public QScrollArea {
    Q_OBJECT
public:
    explicit PlateManager_Qt(QWidget* parent = nullptr);

public slots:
    // Pulls fresh state from each card's bound gfcPlateGUI_Qt and pushes
    // it back into the spinboxes. Called when the viewport mutates plate
    // state outside the cards (mouse drag, wheel zoom, keyboard
    // shortcuts), so the user can see the values they're editing.
    void refreshAllCards();

    // Lightweight refresh of just the dragged plate's transform spinboxes.
    // Wired to GlViewport_Qt::plateTransformChanged during pan/zoom drag —
    // an order of magnitude cheaper than refreshAllCards since it skips
    // the other 3 cards' widget-block scopes entirely.
    void refreshPlateTransform(int plateIdx);

    // Sibling of refreshPlateTransform for color-correction drags
    // (W/E/Q/D/S key+drag). Routes plateColorChanged(plateIdx) to the
    // matching card's refreshColorOnly().
    void refreshPlateColor(int plateIdx);

    // Switch between the horizontal (false: 1×N row, cards in wide-short
    // form) and vertical (true: N×1 column, cards in narrow-tall form)
    // arrangements. Driven by the dock edge in MainWindow. Re-flows the
    // grid, re-lays each card via PlateCard_Qt::setVertical, and pins the
    // panel's cross-axis extent to one card so the long axis is what scrolls.
    void setOrientation(bool vertical);

private:
    void arrange();
    // Pins the panel's fixed extent to the packed cards (called by arrange,
    // and deferred a tick to re-measure after dock transitions settle).
    void applyFixedExtent();

    QWidget* inner_ = nullptr;
    QGridLayout* grid_ = nullptr;
    QList<PlateCard_Qt*> cards_;
    bool vertical_ = false;
};

#endif

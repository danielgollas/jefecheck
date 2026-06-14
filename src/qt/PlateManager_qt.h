// Plate Manager dock content. Hosts up to N plate cards in a 1- or 2-column
// QGridLayout that reflows on resize. Two columns when there's room (default
// docked state), single column when narrow (e.g. floated to a vertical edge).
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

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void reflow(int viewportWidth);

    QWidget* inner_ = nullptr;
    QGridLayout* grid_ = nullptr;
    QList<PlateCard_Qt*> cards_;
    int currentColumns_ = 0;
};

#endif

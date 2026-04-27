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

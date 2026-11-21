// SPDX-License-Identifier: GPL-2.0-or-later
// Map of the viewport layout in the Plate Manager title bar, sized by the bar to fit a
// native dock title (37 x 20 on macOS; 44 x 24 until placed): one cell per
// plate the layout shows, the active one in the accent colour. A click on a
// cell asks for that plate to become active (plateClicked); the owner applies it.
#ifndef JEFECHECK_QT_PLATE_MINIMAP_H
#define JEFECHECK_QT_PLATE_MINIMAP_H

#include <QWidget>

class PlateMiniMap_Qt : public QWidget {
    Q_OBJECT
public:
    explicit PlateMiniMap_Qt(QWidget* parent = nullptr);

    // Repaints (and rewrites the tooltip) only when a value changes.
    void setState(int framingMode, int activePlate);
    int activePlate() const { return active_; }

signals:
    void plateClicked(int plate);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;

private:
    int framing_ = -1;
    int active_ = -1;
};

#endif

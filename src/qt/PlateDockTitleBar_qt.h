// SPDX-License-Identifier: GPL-2.0-or-later
// Title bar for the Plate Manager dock (QDockWidget::setTitleBarWidget): close
// and float buttons on the left, drawn with the style's own dock-title icons so
// they match the native title bars of the other docks, the title centred, and
// the viewport mini-map at the right. Presses outside the buttons and the map
// are ignored, so they reach the QDockWidget, which drags, undocks and (on a
// double-click) floats or re-docks it as it does with a native title bar.
#ifndef JEFECHECK_QT_PLATE_DOCK_TITLE_BAR_H
#define JEFECHECK_QT_PLATE_DOCK_TITLE_BAR_H

#include <QAbstractButton>
#include <QStyle>
#include <QWidget>

class QDockWidget;
class QLabel;

// One of the title bar's icon buttons: paints a QStyle standard icon, no frame,
// so the theme's QToolButton rules (24 px, border) don't apply.
class DockTitleButton_Qt : public QAbstractButton {
    Q_OBJECT
public:
    DockTitleButton_Qt(QStyle::StandardPixmap icon, QWidget* parent);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    QStyle::StandardPixmap icon_;
    bool hover_ = false;
};

class PlateDockTitleBar_Qt : public QWidget {
    Q_OBJECT
public:
    // `map` is reparented into the bar, at its right edge, and resized to fit the bar.
    PlateDockTitleBar_Qt(QDockWidget* dock, QWidget* map);

    QAbstractButton* closeButton() const { return close_; }
    QAbstractButton* floatButton() const { return float_; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QDockWidget* dock_;
    DockTitleButton_Qt* close_ = nullptr;
    DockTitleButton_Qt* float_ = nullptr;
    QLabel* title_ = nullptr;
    int nativeH_ = 22;   // a native dock title's height in this style, measured in the ctor
};

#endif

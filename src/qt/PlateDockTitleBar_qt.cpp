// SPDX-License-Identifier: GPL-2.0-or-later
#include "PlateDockTitleBar_qt.h"
#include "UiMetrics_qt.h"

#include <algorithm>

#include <QDockWidget>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>

namespace ui = jefe::qt::ui;

DockTitleButton_Qt::DockTitleButton_Qt(QStyle::StandardPixmap icon, QWidget* parent)
    : QAbstractButton(parent), icon_(icon) {
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
    // Painted here, not by the theme: the density checker skips it like other custom-painted widgets.
    setProperty(ui::kDensityExemptProperty, true);
}

QSize DockTitleButton_Qt::sizeHint() const {
    // Same metrics QDockWidget uses for its native title buttons.
    const int icon = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    const int margin = style()->pixelMetric(QStyle::PM_DockWidgetTitleBarButtonMargin, nullptr, this);
    return QSize(icon + 2 * margin, icon + 2 * margin);
}

void DockTitleButton_Qt::enterEvent(QEnterEvent* e) {
    hover_ = true;
    update();
    QAbstractButton::enterEvent(e);
}

void DockTitleButton_Qt::leaveEvent(QEvent* e) {
    hover_ = false;
    update();
    QAbstractButton::leaveEvent(e);
}

void DockTitleButton_Qt::paintEvent(QPaintEvent*) {
    QPainter p(this);
    QStyleOptionDockWidget opt;
    opt.initFrom(this);
    const int iconPx = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    const QIcon icon = style()->standardIcon(icon_, &opt, this);
    const QIcon::Mode mode = !isEnabled() ? QIcon::Disabled : (hover_ || isDown()) ? QIcon::Active : QIcon::Normal;
    const QPixmap pm = icon.pixmap(QSize(iconPx, iconPx), devicePixelRatioF(), mode);
    const QRect r(QPoint((width() - iconPx) / 2, (height() - iconPx) / 2), QSize(iconPx, iconPx));
    if (isDown()) p.setOpacity(0.7);
    p.drawPixmap(r, pm);
}

PlateDockTitleBar_Qt::PlateDockTitleBar_Qt(QDockWidget* dock, QWidget* map) : QWidget(dock), dock_(dock) {
    setObjectName("platemanager.titlebar");
    setAccessibleName("Plate Manager title bar");

    close_ = new DockTitleButton_Qt(QStyle::SP_TitleBarCloseButton, this);
    close_->setObjectName("platemanager.close");
    close_->setAccessibleName("Close");
    close_->setToolTip(tr("Close the Plate Manager"));
    connect(close_, &QAbstractButton::clicked, dock_, &QDockWidget::close);

    float_ = new DockTitleButton_Qt(QStyle::SP_TitleBarNormalButton, this);
    float_->setObjectName("platemanager.float");
    float_->setAccessibleName("Float");
    float_->setToolTip(tr("Float or dock the Plate Manager"));
    connect(float_, &QAbstractButton::clicked, dock_, [this]() { dock_->setFloating(!dock_->isFloating()); });

    title_ = new QLabel(dock_->windowTitle(), this);
    title_->setAlignment(Qt::AlignCenter);
    connect(dock_, &QDockWidget::windowTitleChanged, title_, &QLabel::setText);

    // Equal-width blocks either side keep the title centred on the bar, as on a native title
    // bar. Plain layouts and spacing, no container widgets: the theme paints every QWidget's
    // background, which showed as lighter boxes behind the buttons and the map.
    map->setParent(this);
    map->show();
    const int buttonsW = close_->sizeHint().width() + float_->sizeHint().width();
    const int side = std::max(buttonsW, map->sizeHint().width());
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(2, 1, ui::kGapTight, 2);   // bottom: room for the separator line
    row->setSpacing(0);
    row->addWidget(close_);
    row->addWidget(float_);
    row->addSpacing(side - buttonsW);
    row->addWidget(title_, 1);
    row->addSpacing(side - map->sizeHint().width());
    row->addWidget(map);

    const auto syncButtons = [this]() {
        const auto f = dock_->features();
        close_->setVisible(f.testFlag(QDockWidget::DockWidgetClosable));
        float_->setVisible(f.testFlag(QDockWidget::DockWidgetFloatable));
    };
    syncButtons();
    connect(dock_, &QDockWidget::featuresChanged, this, syncButtons);
}

void PlateDockTitleBar_Qt::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().window());
#ifdef Q_OS_MACOS
    // The 1 px separator the macOS style draws under a native dock title (sampled: #bfbfbf).
    p.fillRect(QRect(0, height() - 1, width(), 1), QColor(0xbf, 0xbf, 0xbf));
#endif
}

QSize PlateDockTitleBar_Qt::sizeHint() const {
    return layout()->sizeHint();
}

QSize PlateDockTitleBar_Qt::minimumSizeHint() const {
    return layout()->minimumSize();
}

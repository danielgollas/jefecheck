#include "PlateManager_qt.h"
#include "PlateCard_qt.h"
#include "SequenceLoadBridge_qt.h"

#include <algorithm>

#include <QGridLayout>
#include <QMargins>
#include <QSize>
#include <QStyle>
#include <QTimer>
#include <QWidget>

PlateManager_Qt::PlateManager_Qt(QWidget* parent) : QScrollArea(parent) {
    setObjectName("platemanager.scroll");
    setAccessibleName("Plate Manager");
    inner_ = new QWidget(this);
    inner_->setObjectName("platemanager.grid");
    grid_ = new QGridLayout(inner_);
    grid_->setContentsMargins(4, 4, 4, 4);
    grid_->setSpacing(2);
    inner_->setLayout(grid_);

    for (int i = 0; i < 4; ++i) {
        // Bind to the rendering chain's plate GUI when it's available
        // (set up by initializeRenderingChain() before docks are built).
        // Falls back to an owned GUI when running without the chain so
        // the dock still renders for tests / dev.
        auto* externalGui = jefe::qt::getPlateGUIQt(i);
        auto* card = new PlateCard_Qt(i, externalGui, inner_);
        connect(card, &PlateCard_Qt::clicked,
                this, [this](int id) {
                    jefe::qt::setActivePlate(id);
                    refreshAllCards();
                });
        // A layer change writes to the shared sequence; refresh every card so
        // siblings bound to the same track show the new layer in their combo.
        connect(card, &PlateCard_Qt::layerChanged,
                this, &PlateManager_Qt::refreshAllCards);
        cards_.append(card);
    }

    setWidget(inner_);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    arrange();
}

void PlateManager_Qt::setOrientation(bool vertical) {
    vertical_ = vertical;
    // Re-flow each card's internal layout first so its sizeHint reflects the
    // arrangement we're about to pin the panel's cross-axis to.
    for (auto* card : cards_) {
        if (card) card->setVertical(vertical);
    }
    arrange();
}

void PlateManager_Qt::arrange() {
    // Orientation drives the grid shape: vertical ⇒ a single column (4×1,
    // narrow-tall cards); horizontal ⇒ two columns (2×2, wide-short cards).
    for (auto* card : cards_) {
        grid_->removeWidget(card);
    }

    const int n = cards_.size();
    const int cols = vertical_ ? 1 : 2;
    const int rows = (n + cols - 1) / cols;
    for (int i = 0; i < n; ++i) {
        grid_->addWidget(cards_[i], i / cols, i % cols,
                         Qt::AlignLeft | Qt::AlignTop);
    }

    // Content rows/cols take zero stretch. Horizontal is fixed on BOTH axes
    // to the 2×2 content (so floating/docked is always exactly that size),
    // so it needs no spacer. Vertical fixes width only and lets height grow,
    // so a trailing spacer row keeps the cards pinned to the top.
    for (int c = 0; c <= 2; ++c) grid_->setColumnStretch(c, 0);
    for (int r = 0; r <= n; ++r) grid_->setRowStretch(r, 0);
    if (vertical_) grid_->setRowStretch(rows, 1);

    if (n == 0) return;

    // Pin the panel size to the packed cards. Do it now (best-effort) AND
    // again next event-loop turn: reading a card's sizeHint synchronously
    // during a dock move/undock can return a stale/zero size, which is what
    // collapsed the vertical width and produced a 0-size window on undock.
    // The deferred pass measures after the widget tree has settled.
    applyFixedExtent();
    QTimer::singleShot(0, this, [this]() { applyFixedExtent(); });
}

void PlateManager_Qt::applyFixedExtent() {
    const int n = cards_.size();
    if (n == 0) return;
    const int cols = vertical_ ? 1 : 2;
    const int rows = (n + cols - 1) / cols;

    int cardW = 0, cardH = 0;
    for (auto* c : cards_) {
        if (!c) continue;
        c->ensurePolished();
        if (auto* L = c->layout()) L->activate();
        const QSize sh = c->sizeHint();
        cardW = std::max(cardW, sh.width());
        cardH = std::max(cardH, sh.height());
    }
    // Safety floors: a transient bad sizeHint (dock/undock mid-flight) must
    // never collapse the panel to a near-zero window. The narrow card is
    // ~200 wide; the wide-short card ~56 tall. These floors keep the panel
    // usable until the deferred pass measures the real size.
    cardW = std::max(cardW, 200);
    cardH = std::max(cardH, 56);

    const QMargins m = grid_->contentsMargins();
    const int sp = grid_->spacing();
    const int frame = frameWidth() * 2;  // QScrollArea border on both sides

    if (vertical_) {
        // Fix width to one narrow card; height is free and scrolls. No
        // horizontal scrollbar (width is fixed to content); the vertical
        // scrollbar gets its own extent so it doesn't cover the controls.
        const int sb = style()->pixelMetric(QStyle::PM_ScrollBarExtent) + 2;
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setMaximumWidth(QWIDGETSIZE_MAX);
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
        setFixedWidth(cardW + m.left() + m.right() + frame + sb);
    } else {
        // Fixed both ways — exactly the 2×2 block (cols×rows cards plus the
        // grid spacing and margins), floating or docked. Scrollbars off so
        // the area doesn't reserve a strip (reads as extra height/width).
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        const int w = cols * cardW + (cols - 1) * sp + m.left() + m.right() + frame;
        const int h = rows * cardH + (rows - 1) * sp + m.top() + m.bottom() + frame;
        setFixedSize(w, h);
    }
}

void PlateManager_Qt::refreshAllCards() {
    const int active = jefe::qt::getActivePlate();
    for (auto* card : cards_) {
        if (!card) continue;
        card->refreshFromState();
        card->setActiveHighlight(card->id() == active);
    }
}

void PlateManager_Qt::refreshPlateTransform(int plateIdx) {
    if (plateIdx < 0 || plateIdx >= cards_.size()) return;
    if (auto* card = cards_[plateIdx]) card->refreshTransformOnly();
}

void PlateManager_Qt::refreshPlateColor(int plateIdx) {
    if (plateIdx < 0 || plateIdx >= cards_.size()) return;
    if (auto* card = cards_[plateIdx]) card->refreshColorOnly();
}

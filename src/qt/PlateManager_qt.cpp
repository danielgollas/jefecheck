#include "PlateManager_qt.h"
#include "PlateCard_qt.h"
#include "SequenceLoadBridge_qt.h"

#include <QGridLayout>
#include <QResizeEvent>
#include <QWidget>

namespace {
// Below this viewport width we collapse to a single column. Pick something a
// bit above one card's minimum so we don't flicker on the boundary.
constexpr int kTwoColumnThreshold = 600;
}  // namespace

PlateManager_Qt::PlateManager_Qt(QWidget* parent) : QScrollArea(parent) {
    setObjectName("platemanager.scroll");
    setAccessibleName("Plate Manager");
    inner_ = new QWidget(this);
    inner_->setObjectName("platemanager.grid");
    grid_ = new QGridLayout(inner_);
    grid_->setContentsMargins(6, 6, 6, 6);
    grid_->setSpacing(8);
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
        cards_.append(card);
    }

    setWidget(inner_);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    reflow(viewport()->width());
}

void PlateManager_Qt::resizeEvent(QResizeEvent* e) {
    QScrollArea::resizeEvent(e);
    reflow(viewport()->width());
}

void PlateManager_Qt::reflow(int viewportWidth) {
    const int cols = (viewportWidth >= kTwoColumnThreshold) ? 2 : 1;
    if (cols == currentColumns_) return;
    currentColumns_ = cols;

    for (auto* card : cards_) {
        grid_->removeWidget(card);
    }

    for (int i = 0; i < cards_.size(); ++i) {
        grid_->addWidget(cards_[i], i / cols, i % cols);
    }

    // Make both columns share width equally when there are two; just one
    // column otherwise. setColumnStretch needs to be called for every
    // possible column index up to the previous max.
    grid_->setColumnStretch(0, 1);
    grid_->setColumnStretch(1, cols == 2 ? 1 : 0);
}

void PlateManager_Qt::refreshAllCards() {
    const int active = jefe::qt::getActivePlate();
    for (auto* card : cards_) {
        if (!card) continue;
        card->refreshFromState();
        card->setActiveHighlight(card->id() == active);
    }
}

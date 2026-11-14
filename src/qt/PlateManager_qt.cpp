#include "PlateManager_qt.h"
#include "PlateCard_qt.h"
#include "PlateMiniMap_qt.h"
#include "SequenceLoadBridge_qt.h"
#include "UiMetrics_qt.h"

#include <algorithm>

#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMargins>
#include <QScrollArea>
#include <QSettings>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace pl = jefe::platelayout;
namespace ui = jefe::qt::ui;

namespace {
constexpr const char* kModeKey = "PlateManager/mode";
constexpr int kMaxRowCols = 4;
// A row dock shows at most two card rows (about the old bottom dock's height);
// more rows scroll, so a narrow window never grows the strip past that.
constexpr int kMaxRowRows = 2;
}

PlateManager_Qt::PlateManager_Qt(QWidget* parent) : QWidget(parent) {
    setObjectName("platemanager.panel");
    setAccessibleName("Plate Manager");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Header: mode switch, stretch, mini-map. Outside the scroll area so it
    // stays put while a side column scrolls.
    header_ = new QWidget(this);
    header_->setObjectName("platemanager.header");
    auto* hl = new QHBoxLayout(header_);
    hl->setContentsMargins(ui::kGap, ui::kGapTight, ui::kGap, ui::kGapTight);
    hl->setSpacing(ui::kGapTight);
    modeGroup_ = new QButtonGroup(this);
    modeGroup_->setExclusive(true);
    const struct { const char* text; const char* leaf; const char* tip; } modes[] = {
        {"Active", "active", "Show only the active plate"},
        {"Visible", "visible", "Show the plates the viewport layout shows"},
        {"All", "all", "Show all four plates"}};
    for (int i = 0; i < 3; ++i) {
        auto* b = new QToolButton(header_);
        b->setText(modes[i].text);
        b->setObjectName(QString("platemanager.mode.%1").arg(modes[i].leaf));
        b->setAccessibleName(QString("%1 plates").arg(modes[i].text));
        b->setToolTip(modes[i].tip);
        b->setCheckable(true);
        ui::useWidgetRect(b);
        modeGroup_->addButton(b, i);
        hl->addWidget(b);
        modeButtons_[i] = b;
    }
    hl->addStretch(1);
    miniMap_ = new PlateMiniMap_Qt(header_);
    hl->addWidget(miniMap_);
    outer->addWidget(header_);

    scroll_ = new QScrollArea(this);
    scroll_->setObjectName("platemanager.scroll");
    scroll_->setAccessibleName("Plate cards");
    scroll_->setFrameShape(QFrame::NoFrame);   // the cards carry their own border
    inner_ = new QWidget(scroll_);
    inner_->setObjectName("platemanager.grid");
    grid_ = new QGridLayout(inner_);
    grid_->setContentsMargins(ui::kGapTight, 0, ui::kGapTight, 0);
    grid_->setSpacing(ui::kGap);
    inner_->setLayout(grid_);

    cells_.fill(QPoint(-1, -1), 4);
    for (int i = 0; i < 4; ++i) {
        // Bind to the rendering chain's plate GUI when available (set up before the docks
        // are built); otherwise use an owned GUI so the dock renders for tests.
        auto* externalGui = jefe::qt::getPlateGUIQt(i);
        auto* card = new PlateCard_Qt(i, externalGui, inner_);
        connect(card, &PlateCard_Qt::clicked, this, &PlateManager_Qt::activatePlate);
        // A layer change writes to the shared sequence; refresh every card so
        // siblings bound to the same track show the new layer in their combo.
        connect(card, &PlateCard_Qt::layerChanged,
                this, &PlateManager_Qt::refreshAllCards);
        card->hide();
        cards_.append(card);
    }

    scroll_->setWidget(inner_);
    scroll_->setWidgetResizable(true);
    outer->addWidget(scroll_, 1);

    connect(miniMap_, &PlateMiniMap_Qt::plateClicked, this, &PlateManager_Qt::activatePlate);

    const int stored = QSettings().value(kModeKey, int(pl::PlateMode::Visible)).toInt();
    mode_ = (stored >= 0 && stored <= 2) ? pl::PlateMode(stored) : pl::PlateMode::Visible;
    modeButtons_[int(mode_)]->setChecked(true);
    connect(modeGroup_, &QButtonGroup::idClicked, this,
            [this](int id) { setMode(pl::PlateMode(id)); });

    syncToViewport();
}

void PlateManager_Qt::activatePlate(int plate) {
    jefe::qt::setActivePlate(plate);
    refreshAllCards();
}

void PlateManager_Qt::setMode(pl::PlateMode mode) {
    if (auto* b = modeButtons_[int(mode)]; b && !b->isChecked()) {
        const QSignalBlocker block(modeGroup_);
        b->setChecked(true);
    }
    if (mode != mode_) {
        mode_ = mode;
        QSettings().setValue(kModeKey, int(mode));
    }
    syncToViewport();
}

void PlateManager_Qt::setDockShape(pl::DockShape shape) {
    if (shape == shape_ && laid_) return;
    shape_ = shape;
    syncToViewport();
}

void PlateManager_Qt::setRowBudget(int px) {
    if (px == rowBudget_) return;
    rowBudget_ = px;
    syncToViewport();
}

int PlateManager_Qt::rowCols() const {
    if (rowBudget_ < 0 || rowCardW_ <= 0) return kMaxRowCols;
    // The budget covers the card block plus the grid's side margins.
    const QMargins m = grid_->contentsMargins();
    return pl::rowMaxCols(rowBudget_ - m.left() - m.right(), rowCardW_, ui::kGap);
}

void PlateManager_Qt::syncToViewport() {
    const int framing = jefe::qt::getFramingMode();
    const int active = jefe::qt::getActivePlate();
    const int cols = shape_ == pl::DockShape::Row ? rowCols() : kMaxRowCols;
    if (!laid_ || framing != lastFraming_ || active != lastActive_ || mode_ != lastMode_ ||
        shape_ != lastShape_ || cols != lastCols_) {
        reflow(framing, active, cols);
    } else {
        updateHighlights(framing, active);
    }
}

void PlateManager_Qt::updateHighlights(int framingMode, int activePlate) {
    for (auto* card : cards_) card->setActiveHighlight(card->id() == activePlate);
    miniMap_->setState(framingMode, activePlate);
}

void PlateManager_Qt::reflow(int framingMode, int activePlate, int cols) {
    const bool activeMoved = activePlate != lastActive_;
    // Cards take their narrow-tall form in a column or floating map, the
    // wide-short form in a row. setVertical is a no-op when it already matches.
    const bool vertical = shape_ != pl::DockShape::Row;
    for (auto* card : cards_) card->setVertical(vertical);
    // The wide-short width sets how many cards a row holds; measure it before
    // arranging so the first budgeted reflow already uses it.
    if (!vertical && rowCardW_ <= 0) {
        for (auto* card : cards_) {
            card->ensurePolished();
            if (auto* L = card->layout()) L->activate();
            rowCardW_ = std::max(rowCardW_, card->sizeHint().width());
        }
        if (shape_ == pl::DockShape::Row) cols = rowCols();
    }

    const auto arrangement = pl::arrange(mode_, framingMode, activePlate, shape_, cols);
    for (auto* card : cards_) grid_->removeWidget(card);
    for (int r = 0; r <= 4; ++r) grid_->setRowStretch(r, 0);
    for (int c = 0; c <= 4; ++c) grid_->setColumnStretch(c, 0);

    shown_.clear();
    cells_.fill(QPoint(-1, -1), 4);
    int rows = 0, usedCols = 0;
    for (const auto& g : arrangement) {
        if (g.plate < 0 || g.plate >= cards_.size()) continue;
        grid_->addWidget(cards_[g.plate], g.row, g.col, Qt::AlignLeft | Qt::AlignTop);
        cells_[g.plate] = QPoint(g.col, g.row);
        shown_.append(g.plate);
        rows = std::max(rows, g.row + 1);
        usedCols = std::max(usedCols, g.col + 1);
    }
    // A trailing stretch row and column pin the cards to the top-left.
    grid_->setRowStretch(rows, 1);
    grid_->setColumnStretch(usedCols, 1);

    const auto visible = pl::visiblePlates(framingMode);
    for (auto* card : cards_) {
        const bool isShown = shown_.contains(card->id());
        card->setVisible(isShown);
        card->setOffscreen(mode_ == pl::PlateMode::All &&
                           std::find(visible.begin(), visible.end(), card->id()) == visible.end());
    }
    updateHighlights(framingMode, activePlate);

    laid_ = true;
    lastFraming_ = framingMode;
    lastActive_ = activePlate;
    lastMode_ = mode_;
    lastShape_ = shape_;
    lastCols_ = cols;

    // Pin the panel size now and again next event-loop turn: a card's sizeHint read
    // during a dock move/undock can be stale/zero (a 0-size window, §11).
    applyFixedExtent();
    QTimer::singleShot(0, this, [this]() { applyFixedExtent(); });

    // A scrolling column (or a row past two card rows) brings the active card into view.
    if (shape_ != pl::DockShape::Floating && activeMoved &&
        activePlate >= 0 && activePlate < cards_.size() && cards_[activePlate]->isVisibleTo(inner_)) {
        inner_->layout()->activate();
        scroll_->ensureWidgetVisible(cards_[activePlate]);
        QTimer::singleShot(0, this, [this, activePlate]() {
            if (shape_ != pl::DockShape::Floating && lastActive_ == activePlate)
                scroll_->ensureWidgetVisible(cards_[activePlate]);
        });
    }
    emit arrangementChanged();
}

void PlateManager_Qt::applyFixedExtent() {
    if (shown_.isEmpty()) return;
    int rows = 0, cols = 0;
    for (int p : shown_) {
        rows = std::max(rows, cells_[p].y() + 1);
        cols = std::max(cols, cells_[p].x() + 1);
    }

    int cardW = 0, cardH = 0;
    for (int p : shown_) {
        PlateCard_Qt* c = cards_[p];
        c->ensurePolished();
        if (auto* L = c->layout()) L->activate();
        const QSize sh = c->sizeHint();
        cardW = std::max(cardW, sh.width());
        cardH = std::max(cardH, sh.height());
    }
    // Safety floors: a transient bad sizeHint (dock/undock mid-flight) must not
    // collapse the panel; the narrow card is ~280 wide, the wide-short 70 tall.
    cardW = std::max(cardW, 200);
    cardH = std::max(cardH, 56);

    const QMargins m = grid_->contentsMargins();
    const int sp = grid_->spacing();
    const int frame = scroll_->frameWidth() * 2;  // QScrollArea border on both sides
    header_->ensurePolished();
    if (auto* L = header_->layout()) L->activate();
    const int headerH = std::max(header_->sizeHint().height(), ui::kControlH + 2 * ui::kGapTight);
    const int headerW = header_->minimumSizeHint().width();
    const int contentW = cols * cardW + (cols - 1) * sp + m.left() + m.right() + frame;
    const bool rowScrolls = shape_ == pl::DockShape::Row && rows > kMaxRowRows;
    const int shownRows = rowScrolls ? kMaxRowRows : rows;
    const int contentH = shownRows * cardH + (shownRows - 1) * sp + m.top() + m.bottom() + frame;
    const int sb = style()->pixelMetric(QStyle::PM_ScrollBarExtent) + 2;

    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (shape_ == pl::DockShape::Column) {
        // Fix width to one narrow card; height is free and scrolls. The vertical
        // scrollbar gets its own extent so it doesn't cover the controls.
        scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        const int w = std::max(contentW + sb, headerW);
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
        setFixedWidth(w);
        extent_ = QSize(w, headerH + contentH);
    } else {
        // Fixed both ways: exactly the header plus the card block. Scrollbars
        // off so the area reserves no strip, unless a row holds more than two
        // card rows: then it shows two and scrolls, with room for the bar.
        scroll_->setVerticalScrollBarPolicy(rowScrolls ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAlwaysOff);
        const int w = std::max(contentW + (rowScrolls ? sb : 0), headerW);
        const int h = headerH + contentH;
        setFixedSize(w, h);
        if (shape_ == pl::DockShape::Row && cols > 1) {
            // The window must still be able to narrow: a row may shrink to one column's
            // width, and the narrower window's budget then re-flows it into fewer columns
            // (a dragged edge clips the right-hand card for a tick until that lands).
            const int oneCol = cardW + m.left() + m.right() + frame + (shown_.size() > kMaxRowRows ? sb : 0);
            setMinimumWidth(std::max(oneCol, headerW));
        }
        extent_ = QSize(w, h);
    }

    // The wide-short width can settle a tick late; re-flow if it changes the
    // number of cards a budgeted row holds.
    if (shape_ == pl::DockShape::Row && cardW != rowCardW_) {
        rowCardW_ = cardW;
        if (rowCols() != lastCols_)
            QTimer::singleShot(0, this, [this]() { syncToViewport(); });
    }
}

QSize PlateManager_Qt::sizeHint() const {
    return extent_.isValid() ? extent_ : QWidget::sizeHint();
}

QPoint PlateManager_Qt::cardCell(int plate) const {
    if (plate < 0 || plate >= cells_.size()) return QPoint(-1, -1);
    return cells_[plate];
}

bool PlateManager_Qt::cardOffscreen(int plate) const {
    if (plate < 0 || plate >= cards_.size()) return false;
    return cards_[plate]->isOffscreen();
}

void PlateManager_Qt::refreshAllCards() {
    syncToViewport();
    for (auto* card : cards_) card->refreshFromState();
}

void PlateManager_Qt::refreshPlateTransform(int plateIdx) {
    if (plateIdx < 0 || plateIdx >= cards_.size()) return;
    if (auto* card = cards_[plateIdx]) card->refreshTransformOnly();
}

void PlateManager_Qt::refreshPlateColor(int plateIdx) {
    if (plateIdx < 0 || plateIdx >= cards_.size()) return;
    if (auto* card = cards_[plateIdx]) card->refreshColorOnly();
}

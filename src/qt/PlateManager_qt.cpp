#include "PlateManager_qt.h"
#include "PlateCard_qt.h"
#include "PlateMiniMap_qt.h"
#include "SequenceLoadBridge_qt.h"
#include "UiMetrics_qt.h"

#include <algorithm>

#include <QAction>
#include <QActionGroup>
#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QMenu>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMargins>
#include <QScrollArea>
#include <QSettings>
#include <QStyle>
#include <QTimer>
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

    // Mode actions: the View menu's "Plate Manager Shows" submenu and this panel's
    // context menu share them. The mini-map lives in the dock's title bar
    // (PlateDockTitleBar_Qt reparents it); there is no header row.
    modeGroup_ = new QActionGroup(this);
    modeGroup_->setExclusive(true);
    const struct { const char* text; const char* leaf; const char* tip; } modes[] = {
        {"Active", "active", "Show only the active plate"},
        {"Visible", "visible", "Show the plates the viewport layout shows"},
        {"All", "all", "Show all four plates"}};
    for (int i = 0; i < 3; ++i) {
        auto* a = new QAction(tr(modes[i].text), this);
        a->setObjectName(QString("menu.view.platemanager.%1").arg(modes[i].leaf));
        a->setToolTip(tr(modes[i].tip));
        a->setStatusTip(tr(modes[i].tip));
        a->setCheckable(true);
        modeGroup_->addAction(a);
        modeActions_[i] = a;
        connect(a, &QAction::triggered, this, [this, i]() { setMode(pl::PlateMode(i)); });
    }
    miniMap_ = new PlateMiniMap_Qt(this);
    miniMap_->hide();

    scroll_ = new QScrollArea(this);
    scroll_->setObjectName("platemanager.scroll");
    scroll_->setAccessibleName("Plate cards");
    scroll_->setFrameShape(QFrame::NoFrame);   // the cards carry their own border
    inner_ = new QWidget(scroll_);
    inner_->setObjectName("platemanager.grid");
    grid_ = new QGridLayout(inner_);
    grid_->setContentsMargins(ui::kGapTight, ui::kGapTight, ui::kGapTight, ui::kGapTight);
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
    modeActions_[int(mode_)]->setChecked(true);

    syncToViewport();
}

QMenu* PlateManager_Qt::buildContextMenu(QWidget* parent) {
    auto* menu = new QMenu(parent);
    menu->setObjectName("platemanager.contextmenu");
    menu->addSection(tr("Plate Manager Shows"));
    for (QAction* a : modeActions_) menu->addAction(a);
    return menu;
}

void PlateManager_Qt::contextMenuEvent(QContextMenuEvent* e) {
    // Over the empty area, a card's background or a caption; editors, buttons and
    // combos keep their own behaviour.
    QWidget* under = childAt(e->pos());
    for (QWidget* w = under; w && w != this; w = w->parentWidget()) {
        if (qobject_cast<QAbstractSpinBox*>(w) || qobject_cast<QComboBox*>(w) || qobject_cast<QAbstractButton*>(w)) {
            e->ignore();
            return;
        }
    }
    if (contextMenuHook_) { contextMenuHook_(); e->accept(); return; }
    QMenu* menu = buildContextMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(e->globalPos());
    e->accept();
}

void PlateManager_Qt::activatePlate(int plate) {
    jefe::qt::setActivePlate(plate);
    refreshAllCards();
}

void PlateManager_Qt::setMode(pl::PlateMode mode) {
    if (auto* a = modeActions_[int(mode)]; a && !a->isChecked()) a->setChecked(true);
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
    const int before = lastCols_;
    rowBudget_ = px;
    syncToViewport();
    // The panel's width may still cap the columns (no reflow), but the full-row extent the
    // dock can be widened to follows the budget: refresh it so pinPlateDock sees it.
    if (laid_ && shape_ == pl::DockShape::Row && lastCols_ == before) applyFixedExtent();
}

int PlateManager_Qt::rowCols() const {
    if (rowBudget_ < 0 || rowCardW_ <= 0) return kMaxRowCols;
    // The budget covers the card block plus the grid's side margins.
    const QMargins m = grid_->contentsMargins();
    return pl::rowMaxCols(rowBudget_ - m.left() - m.right(), rowCardW_, ui::kGap);
}

int PlateManager_Qt::widthCols() const {
    // Only a shown panel has a real width; before that the budget alone decides.
    if (!isVisible() || width() <= 0 || rowCardW_ <= 0) return kMaxRowCols;
    const QMargins m = grid_->contentsMargins();
    const int avail = width() - m.left() - m.right() - scroll_->frameWidth() * 2;
    return pl::rowMaxCols(avail, rowCardW_, ui::kGap);
}

int PlateManager_Qt::effectiveRowCols() const {
    return std::min(rowCols(), widthCols());
}

void PlateManager_Qt::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    // A dock squeezed by its separator (or by the window, at an unchanged budget) gets
    // fewer columns, and a widened one gets them back; re-place the cards only when the
    // column count actually changes.
    if (shape_ == pl::DockShape::Row && laid_ && effectiveRowCols() != lastCols_) {
        reflowFromResize_ = true;
        syncToViewport();
        reflowFromResize_ = false;
    }
}

void PlateManager_Qt::syncToViewport() {
    const int framing = jefe::qt::getFramingMode();
    const int active = jefe::qt::getActivePlate();
    const int cols = shape_ == pl::DockShape::Row ? effectiveRowCols() : kMaxRowCols;
    if (!laid_ || framing != lastFraming_ || active != lastActive_ || mode_ != lastMode_ ||
        shape_ != lastShape_ || cols != lastCols_) {
        reflow(framing, active, cols);
    } else {
        updateHighlights(framing, active);
    }
}

void PlateManager_Qt::updateHighlights(int framingMode, int activePlate) {
    for (auto* card : cards_) card->setActiveHighlight(card->id() == activePlate);
    if (miniMap_) miniMap_->setState(framingMode, activePlate);
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
        if (shape_ == pl::DockShape::Row) cols = effectiveRowCols();
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
    lastReflowFromResize_ = reflowFromResize_;
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
        const int w = contentW + sb;
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);
        setFixedWidth(w);
        extent_ = QSize(w, contentH);
    } else {
        // Fixed both ways: exactly the card block. Scrollbars
        // off so the area reserves no strip, unless a row holds more than two
        // card rows: then it shows two and scrolls, with room for the bar.
        scroll_->setVerticalScrollBarPolicy(rowScrolls ? Qt::ScrollBarAlwaysOn : Qt::ScrollBarAlwaysOff);
        const int w = contentW + (rowScrolls ? sb : 0);
        const int h = contentH;
        if (shape_ == pl::DockShape::Row) {
            // Height is fixed to the placed rows. Width ranges from one column (so the window
            // can narrow and the separator can squeeze the dock; resizeEvent then re-places
            // the cards in fewer columns) to the full row the budget allows (the extent, so
            // pinPlateDock can widen the dock back to it).
            const int n = int(shown_.size());
            const int budgetCols = std::max(1, std::min(rowCols(), n));
            const int fullCols = n > budgetCols ? pl::balancedRowCols(n, budgetCols) : std::max(n, 1);
            const int fullRows = (n + fullCols - 1) / fullCols;
            const int fullW = std::max(w, fullCols * cardW + (fullCols - 1) * sp + m.left() + m.right() + frame +
                                              (fullRows > kMaxRowRows ? sb : 0));
            const int oneCol = cardW + m.left() + m.right() + frame + (n > kMaxRowRows ? sb : 0);
            setFixedHeight(h);
            setMinimumWidth(std::min(oneCol, fullW));
            setMaximumWidth(fullW);
            extent_ = QSize(fullW, h);
        } else {
            setFixedSize(w, h);
            extent_ = QSize(w, h);
        }
    }

    // The wide-short width can settle a tick late; re-flow if it changes the
    // number of cards a budgeted row holds.
    if (shape_ == pl::DockShape::Row && cardW != rowCardW_) {
        rowCardW_ = cardW;
        if (effectiveRowCols() != lastCols_)
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

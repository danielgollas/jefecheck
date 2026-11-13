#include "LoadWindowDialog_qt.h"

#include "FlowLayout_qt.h"
#include "GlViewport_qt.h"
#include "SequenceLoadBridge_qt.h"
#include "TrackStrip_qt.h"
#include "UiMetrics_qt.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

LoadWindowDialog_Qt::LoadWindowDialog_Qt(GlViewport_Qt* viewport, QWidget* parent)
    : QDialog(parent), viewport_(viewport) {
    setObjectName("dialog.loadwindow");
    setWindowTitle("Load Sequence Manager");
    // Non-modal: the user needs the main window while it is open.
    // viewport.loadWindowOpen_ gates whether drops route here or to the fast path.
    setModal(false);

    auto* outer = new QVBoxLayout(this);

    // Strip grid: FlowLayout so 2x2 default reflows to 4x1 at narrow widths.
    auto* gridHost = new QWidget(this);
    auto* flow = new FlowLayout_Qt(gridHost, /*margin=*/4,
                                   /*hSpacing=*/jefe::qt::ui::kGapGroup, /*vSpacing=*/jefe::qt::ui::kGap);
    flow->setStretchItems(true);
    for (int i = 0; i < 4; ++i) {
        strips_[i] = new TrackStrip_Qt(i, gridHost);
        connect(strips_[i], &TrackStrip_Qt::trackEdited,
                this, &LoadWindowDialog_Qt::onTrackEdited);
        flow->addWidget(strips_[i]);
    }
    gridHost->setLayout(flow);
    // The strips need more height than a small screen offers when stacked, so
    // the grid scrolls instead of clipping when the dialog is shrunk.
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("dialog.loadwindow.scroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(gridHost);
    outer->addWidget(scroll, /*stretch=*/1);

    // Load All button anchored at the bottom.
    auto* row = new QHBoxLayout();
    row->addStretch(1);
    loadAll_ = new QPushButton("Load All", this);
    loadAll_->setObjectName("dialog.loadwindow.button.loadAll");
    loadAll_->setDefault(true);
    row->addWidget(loadAll_);
    outer->addLayout(row);

    connect(loadAll_, &QPushButton::clicked, this, &LoadWindowDialog_Qt::accept);

    // Narrow + tall by default so FlowLayout stacks the four strips vertically;
    // users can widen it to reflow into 2x2.
    // Minimum width = the strips' minimum (plus flow margin) + the vertical
    // scrollbar + dialog margins, so no horizontal scrollbar appears at the minimum.
    const QMargins om = outer->contentsMargins();
    const int minW = gridHost->minimumSizeHint().width() +
                     style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this) +
                     om.left() + om.right() + 2 * scroll->frameWidth();
    setMinimumSize(minW, 360);
    resize(620, 720);
}

void LoadWindowDialog_Qt::showEvent(QShowEvent* e) {
    QDialog::showEvent(e);
    // Flip viewport into preview mode for all plates while we're up.
    if (viewport_) viewport_->setLoadWindowOpen(true);
    for (auto* s : strips_) if (s) s->refreshFromGUI();
}

void LoadWindowDialog_Qt::reject() {
    if (viewport_) viewport_->setLoadWindowOpen(false);
    QDialog::reject();
}

void LoadWindowDialog_Qt::accept() {
    // Load All path: close first, then fire the loads — eyes go on the
    // viewport, not the closing modal.
    if (viewport_) viewport_->setLoadWindowOpen(false);
    QDialog::accept();
    jefe::qt::startLoadingAllTracks();
}

void LoadWindowDialog_Qt::onTrackEdited(int trackIdx) {
    // loadPreview uploads to GL: the viewport's context must be current on this
    // thread (as in the loadFileIntoPlate fast path) or the upload fails silently.
    if (viewport_) viewport_->makeCurrent();
    const bool ok = jefe::qt::reloadTrackPreview(trackIdx);
    if (viewport_) viewport_->doneCurrent();

    if (trackIdx >= 0 && trackIdx < 4 && strips_[trackIdx]) {
        // Always pull widget state back: findSequenceFiles writes the discovered
        // from/to bounds even when the preview decode failed.
        strips_[trackIdx]->refreshFromGUI();
        if (!ok) {
            // Distinguish "empty filename" (no error) from "preview failed".
            // refreshFromGUI just reset the header; re-paint it red.
            const auto p = jefe::qt::getTrackParams(trackIdx);
            if (!p.filename.empty()) {
                strips_[trackIdx]->markError("File not found");
            }
        }
    }
    if (viewport_) viewport_->update();
}

void LoadWindowDialog_Qt::setTrackFilename(int plateIdx, const QString& path) {
    // Today: plateIdx == trackIdx for the active plate's track. Future
    // PR resolves via plateManager.getTrackOnPlate(plateIdx).
    const int trackIdx = plateIdx;
    if (trackIdx < 0 || trackIdx >= 4 || !strips_[trackIdx]) return;
    strips_[trackIdx]->setFilenameFromDrop(path);
}

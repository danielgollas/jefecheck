#include "LoadWindowDialog_qt.h"

#include "FlowLayout_qt.h"
#include "GlViewport_qt.h"
#include "SequenceLoadBridge_qt.h"
#include "TrackStrip_qt.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

LoadWindowDialog_Qt::LoadWindowDialog_Qt(GlViewport_Qt* viewport, QWidget* parent)
    : QDialog(parent), viewport_(viewport) {
    setObjectName("dialog.loadwindow");
    setWindowTitle("Load Sequence Manager");
    setModal(true);

    auto* outer = new QVBoxLayout(this);

    // Strip grid: FlowLayout so 2x2 default reflows to 4x1 at narrow widths.
    auto* gridHost = new QWidget(this);
    auto* flow = new FlowLayout_Qt(gridHost, /*margin=*/4,
                                   /*hSpacing=*/12, /*vSpacing=*/12);
    for (int i = 0; i < 4; ++i) {
        strips_[i] = new TrackStrip_Qt(i, gridHost);
        connect(strips_[i], &TrackStrip_Qt::trackEdited,
                this, &LoadWindowDialog_Qt::onTrackEdited);
        flow->addWidget(strips_[i]);
    }
    gridHost->setLayout(flow);
    outer->addWidget(gridHost, /*stretch=*/1);

    // Load All button anchored at the bottom.
    auto* row = new QHBoxLayout();
    row->addStretch(1);
    loadAll_ = new QPushButton("Load All", this);
    loadAll_->setObjectName("dialog.loadwindow.button.loadAll");
    loadAll_->setDefault(true);
    row->addWidget(loadAll_);
    outer->addLayout(row);

    connect(loadAll_, &QPushButton::clicked, this, &LoadWindowDialog_Qt::accept);

    // Default to a narrow + tall layout so FlowLayout stacks the four
    // strips vertically (4×1) — 900px-tall display still fits all four
    // comfortably. Users can widen the dialog to reflow into 2×2 if
    // they prefer that.
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
    // loadPreview decodes a frame and uploads to GL; the viewport's
    // QOpenGLWidget context must be current on the calling thread
    // (mirrors MainWindow_Qt::loadFileIntoPlate's fast-path pattern at
    // MainWindow_qt.cpp:768-774). Without this, glDeleteTextures /
    // glTexImage2D inside loadPreview silently fail and previewFrame
    // stays unloaded — findSequenceFiles writes the discovered range
    // to the GUI before that point, but the broken pipeline caused
    // observed brittle behavior where the spinners stayed at (1,1)
    // and Load All loaded one frame.
    if (viewport_) viewport_->makeCurrent();
    const bool ok = jefe::qt::reloadTrackPreview(trackIdx);
    if (viewport_) viewport_->doneCurrent();

    if (trackIdx >= 0 && trackIdx < 4 && strips_[trackIdx]) {
        // Always pull widget state back from the GUI — findSequenceFiles
        // writes the discovered from/to bounds via setFromToBounds even
        // when the preview pixel decode itself failed. Skipping the
        // refresh on !ok was the reason the spinners stayed at (1,1)
        // and Load All only got a single frame.
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

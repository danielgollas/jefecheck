#include "TrackStrip_qt.h"

#include "SequenceLoadBridge_qt.h"
#include "../UIConstants.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
constexpr int kMaxFrameNumber = 9'999'999;
}

TrackStrip_Qt::TrackStrip_Qt(int trackIdx, QWidget* parent)
    : QWidget(parent), trackIdx_(trackIdx) {
    setObjectName(QString("dialog.loadwindow.strip.%1").arg(trackIdx_));

    auto* outer = new QVBoxLayout(this);

    // Row 1: filename + Browse
    auto* row1 = new QHBoxLayout();
    filename_ = new QLineEdit(this);
    filename_->setObjectName(QString("dialog.loadwindow.strip.%1.filename").arg(trackIdx_));
    browse_ = new QPushButton("Browse…", this);
    browse_->setObjectName(QString("dialog.loadwindow.strip.%1.browse").arg(trackIdx_));
    row1->addWidget(filename_, /*stretch=*/1);
    row1->addWidget(browse_);
    outer->addLayout(row1);

    // Row 2: From / To
    auto* row2 = new QHBoxLayout();
    from_ = new QSpinBox(this);
    from_->setObjectName(QString("dialog.loadwindow.strip.%1.from").arg(trackIdx_));
    from_->setRange(0, kMaxFrameNumber);
    to_ = new QSpinBox(this);
    to_->setObjectName(QString("dialog.loadwindow.strip.%1.to").arg(trackIdx_));
    to_->setRange(0, kMaxFrameNumber);
    row2->addWidget(new QLabel("From:", this));
    row2->addWidget(from_);
    row2->addSpacing(8);
    row2->addWidget(new QLabel("To:", this));
    row2->addWidget(to_);
    row2->addStretch(1);
    outer->addLayout(row2);

    // Row 3: Scale / Bit Depth / Channels
    auto* row3 = new QHBoxLayout();

    scale_ = new QComboBox(this);
    scale_->setObjectName(QString("dialog.loadwindow.strip.%1.scale").arg(trackIdx_));
    scale_->addItem("100%", 100);
    scale_->addItem("50%",  50);
    scale_->addItem("25%",  25);
    row3->addWidget(new QLabel("Scale:", this));
    row3->addWidget(scale_);

    bitDepth_ = new QComboBox(this);
    bitDepth_->setObjectName(QString("dialog.loadwindow.strip.%1.bitdepth").arg(trackIdx_));
    bitDepth_->addItem("8-bit",   GFC_8BPC);
    bitDepth_->addItem("16-bit",  GFC_16BPC);
    bitDepth_->addItem("16-half", GFC_16HALF);
    row3->addWidget(new QLabel("Bit:", this));
    row3->addWidget(bitDepth_);

    channels_ = new QComboBox(this);
    channels_->setObjectName(QString("dialog.loadwindow.strip.%1.channels").arg(trackIdx_));
    channels_->addItem("(default)");
    row3->addWidget(new QLabel("Channels:", this));
    row3->addWidget(channels_, 1);
    outer->addLayout(row3);

    connect(filename_, &QLineEdit::editingFinished,
            this, &TrackStrip_Qt::onFilenameChanged);
    connect(browse_,   &QPushButton::clicked,
            this, &TrackStrip_Qt::onBrowse);
    connect(from_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TrackStrip_Qt::onFromChanged);
    connect(to_,   QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TrackStrip_Qt::onToChanged);
    connect(scale_,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TrackStrip_Qt::onScaleChanged);
    connect(bitDepth_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TrackStrip_Qt::onBitDepthChanged);
    connect(channels_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TrackStrip_Qt::onChannelChanged);
}

void TrackStrip_Qt::refreshFromGUI() {
    refreshing_ = true;
    const auto p = jefe::qt::getTrackParams(trackIdx_);
    filename_->setText(QString::fromStdString(p.filename));
    from_->setValue(p.from);
    to_->setValue(p.to);

    {
        int sIdx = scale_->findData(p.scalePct);
        if (sIdx < 0) sIdx = scale_->findData(100);
        scale_->setCurrentIndex(sIdx);
    }
    {
        int bIdx = bitDepth_->findData(p.compression);
        if (bIdx < 0) bIdx = bitDepth_->findData(GFC_16HALF);
        bitDepth_->setCurrentIndex(bIdx);
    }
    channels_->clear();
    if (p.channelOptions.empty()) {
        channels_->addItem("(default)");
    } else {
        for (const auto& name : p.channelOptions) {
            channels_->addItem(QString::fromStdString(name));
        }
        int chIdx = p.channel;
        if (chIdx < 0 || chIdx >= channels_->count()) chIdx = 0;
        channels_->setCurrentIndex(chIdx);
    }
    refreshing_ = false;
}

void TrackStrip_Qt::refreshDerivedLabels() {
    // Filled in by Task 14 (header + estimates labels).
}

void TrackStrip_Qt::markError(const QString& /*reason*/) {
    // Filled in by Task 14.
}

void TrackStrip_Qt::setFilenameFromDrop(const QString& path) {
    filename_->setText(path);
    onFilenameChanged();
}

void TrackStrip_Qt::onFilenameChanged() {
    if (refreshing_) return;
    jefe::qt::setTrackFilename(trackIdx_, filename_->text().toStdString());
    emit trackEdited(trackIdx_);
}

void TrackStrip_Qt::onBrowse() {
    if (refreshing_) return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose sequence frame for Track %1").arg(QChar('A' + trackIdx_)),
        QString(),
        tr("Images (*.exr *.dpx *.png *.jpg *.jpeg *.tif *.tiff *.tga *.bmp)"));
    if (path.isEmpty()) return;
    filename_->setText(path);
    onFilenameChanged();
}

void TrackStrip_Qt::onFromChanged(int v) {
    if (refreshing_) return;
    if (v > to_->value()) {
        refreshing_ = true;
        from_->setValue(to_->value());
        refreshing_ = false;
        return;
    }
    jefe::qt::setTrackFrom(trackIdx_, v);
    emit trackEdited(trackIdx_);
}

void TrackStrip_Qt::onToChanged(int v) {
    if (refreshing_) return;
    if (v < from_->value()) {
        refreshing_ = true;
        to_->setValue(from_->value());
        refreshing_ = false;
        return;
    }
    jefe::qt::setTrackTo(trackIdx_, v);
    emit trackEdited(trackIdx_);
}

void TrackStrip_Qt::onScaleChanged(int idx) {
    if (refreshing_) return;
    jefe::qt::setTrackScalePct(trackIdx_, scale_->itemData(idx).toInt());
    emit trackEdited(trackIdx_);
}

void TrackStrip_Qt::onBitDepthChanged(int idx) {
    if (refreshing_) return;
    jefe::qt::setTrackCompression(trackIdx_, bitDepth_->itemData(idx).toInt());
    emit trackEdited(trackIdx_);
}

void TrackStrip_Qt::onChannelChanged(int idx) {
    if (refreshing_) return;
    jefe::qt::setTrackChannel(trackIdx_, idx);
    emit trackEdited(trackIdx_);
}

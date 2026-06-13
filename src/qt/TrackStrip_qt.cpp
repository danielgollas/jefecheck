#include "TrackStrip_qt.h"

#include "SequenceLoadBridge_qt.h"
#include "../UIConstants.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kMaxFrameNumber = 9'999'999;
}

TrackStrip_Qt::TrackStrip_Qt(int trackIdx, QWidget* parent)
    : QWidget(parent), trackIdx_(trackIdx) {
    setObjectName(QString("dialog.loadwindow.strip.%1").arg(trackIdx_));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 4, 8, 4);
    outer->setSpacing(4);

    header_ = new QLabel(this);
    header_->setObjectName(QString("dialog.loadwindow.strip.%1.header").arg(trackIdx_));
    header_->setText(QString("Track %1").arg(QChar('A' + trackIdx_)));
    QFont hf = header_->font();
    hf.setBold(true);
    hf.setPointSize(std::max(8, hf.pointSize() - 1));
    header_->setFont(hf);
    outer->addWidget(header_);

    // Row 1: filename + Browse + Recent
    auto* row1 = new QHBoxLayout();
    row1->setSpacing(6);
    filename_ = new QLineEdit(this);
    filename_->setObjectName(QString("dialog.loadwindow.strip.%1.filename").arg(trackIdx_));
    // Don't let the line edit grow to fit a long path — we elide and
    // keep the strip width stable. Any positive width works; the layout
    // gives it the available row stretch via row1->addWidget(..., 1).
    filename_->setMinimumWidth(80);
    filename_->installEventFilter(this);
    browse_ = new QPushButton("Browse…", this);
    browse_->setObjectName(QString("dialog.loadwindow.strip.%1.browse").arg(trackIdx_));
    recent_ = new QToolButton(this);
    recent_->setObjectName(QString("dialog.loadwindow.strip.%1.recent").arg(trackIdx_));
    recent_->setText("Recent ▾");
    recent_->setPopupMode(QToolButton::InstantPopup);
    recent_->setMenu(new QMenu(this));
    row1->addWidget(filename_, /*stretch=*/1);
    row1->addWidget(browse_);
    row1->addWidget(recent_);
    outer->addLayout(row1);

    // Row 2: From / To / Scale / Bit / Channels — single tight line
    auto* row2 = new QHBoxLayout();
    row2->setSpacing(6);

    from_ = new QSpinBox(this);
    from_->setObjectName(QString("dialog.loadwindow.strip.%1.from").arg(trackIdx_));
    from_->setRange(0, kMaxFrameNumber);
    to_ = new QSpinBox(this);
    to_->setObjectName(QString("dialog.loadwindow.strip.%1.to").arg(trackIdx_));
    to_->setRange(0, kMaxFrameNumber);
    row2->addWidget(new QLabel("From:", this));
    row2->addWidget(from_);
    row2->addWidget(new QLabel("To:", this));
    row2->addWidget(to_);

    scale_ = new QComboBox(this);
    scale_->setObjectName(QString("dialog.loadwindow.strip.%1.scale").arg(trackIdx_));
    scale_->addItem("100%", 100);
    scale_->addItem("50%",  50);
    scale_->addItem("25%",  25);
    row2->addWidget(new QLabel("Scale:", this));
    row2->addWidget(scale_);

    bitDepth_ = new QComboBox(this);
    bitDepth_->setObjectName(QString("dialog.loadwindow.strip.%1.bitdepth").arg(trackIdx_));
    bitDepth_->addItem("8-bit",   GFC_8BPC);
    bitDepth_->addItem("16-bit",  GFC_16BPC);
    bitDepth_->addItem("16-half", GFC_16HALF);
    row2->addWidget(new QLabel("Bit:", this));
    row2->addWidget(bitDepth_);

    channels_ = new QComboBox(this);
    channels_->setObjectName(QString("dialog.loadwindow.strip.%1.channels").arg(trackIdx_));
    channels_->addItem("(default)");
    row2->addWidget(new QLabel("Ch:", this));
    row2->addWidget(channels_, 1);
    outer->addLayout(row2);

    // Row 3: Crop / Reload / Unload / estimates
    auto* row3 = new QHBoxLayout();
    row3->setSpacing(6);

    crop_ = new QCheckBox("Crop", this);
    crop_->setObjectName(QString("dialog.loadwindow.strip.%1.crop").arg(trackIdx_));
    row3->addWidget(crop_);

    reload_ = new QPushButton("Reload", this);
    reload_->setObjectName(QString("dialog.loadwindow.strip.%1.reload").arg(trackIdx_));
    row3->addWidget(reload_);

    unload_ = new QPushButton("Unload && Clear", this);
    unload_->setObjectName(QString("dialog.loadwindow.strip.%1.unload").arg(trackIdx_));
    row3->addWidget(unload_);

    estimates_ = new QLabel("–", this);
    estimates_->setObjectName(QString("dialog.loadwindow.strip.%1.estimates").arg(trackIdx_));
    estimates_->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    row3->addWidget(estimates_, /*stretch=*/1);

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
    connect(crop_,   &QCheckBox::toggled,    this, &TrackStrip_Qt::onCropToggled);
    connect(reload_, &QPushButton::clicked,  this, &TrackStrip_Qt::onReload);
    connect(unload_, &QPushButton::clicked,  this, &TrackStrip_Qt::onUnload);

    rebuildRecentMenu();
}

void TrackStrip_Qt::refreshFromGUI() {
    refreshing_ = true;
    const auto p = jefe::qt::getTrackParams(trackIdx_);
    // Display the generic seq pattern (foo.####.exr) when the loader
    // discovered one; fall back to the literal filename otherwise.
    // The bridge still stores whatever the user typed under the hood.
    displayPath_ = QString::fromStdString(
        p.filenameGeneric.empty() ? p.filename : p.filenameGeneric);
    filename_->setText(displayPath_);
    applyElidedFilenameText();
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
    crop_->setChecked(p.crop);
    refreshDerivedLabels();
    refreshing_ = false;
}

void TrackStrip_Qt::refreshDerivedLabels() {
    // The seq pattern lives in the filename line edit now; the header
    // is just the track tag. markError repaints this red on demand.
    header_->setText(QString("Track %1").arg(QChar('A' + trackIdx_)));
    header_->setStyleSheet("");

    const auto est = jefe::qt::getTrackEstimates(trackIdx_);
    if (est.frames <= 0) {
        estimates_->setText("–");
    } else {
        double b = (double)est.bytes;
        const char* unit = "B";
        if (b > 1024) { b /= 1024; unit = "KB"; }
        if (b > 1024) { b /= 1024; unit = "MB"; }
        if (b > 1024) { b /= 1024; unit = "GB"; }
        estimates_->setText(
            QString("%1 frames · ≈%2 %3 · ~%4s")
                .arg(est.frames)
                .arg(b, 0, 'f', b >= 10 ? 0 : 1)
                .arg(unit)
                .arg(est.seconds, 0, 'f', 1));
    }
}

void TrackStrip_Qt::markError(const QString& reason) {
    header_->setText(QString("Track %1: %2")
                         .arg(QChar('A' + trackIdx_))
                         .arg(reason));
    header_->setStyleSheet("color: #d44; font-weight: bold;");
    estimates_->setText("–");
}

void TrackStrip_Qt::setFilenameFromDrop(const QString& path) {
    displayPath_ = path;
    filename_->setText(path);
    onFilenameChanged();
}

void TrackStrip_Qt::onFilenameChanged() {
    if (refreshing_) return;
    // Capture the un-elided text *before* the user edits get clobbered
    // by any subsequent re-elision pass; if the line edit currently
    // holds an elided rendering of displayPath_, prefer that (the
    // user didn't type anything new).
    QString typed = filename_->text();
    if (!displayPath_.isEmpty() && !typed.contains(QChar(0x2026)) &&
        typed != displayPath_) {
        // User typed a new value — adopt it as the new full path.
        displayPath_ = typed;
    } else if (typed.contains(QChar(0x2026))) {
        // Stale elision left in the field; ignore it and keep displayPath_.
        typed = displayPath_;
    } else {
        displayPath_ = typed;
    }
    jefe::qt::setTrackFilename(trackIdx_, displayPath_.toStdString());
    pushRecentPath(displayPath_);
    emit trackEdited(trackIdx_);
}

void TrackStrip_Qt::onBrowse() {
    if (refreshing_) return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose sequence frame for Track %1").arg(QChar('A' + trackIdx_)),
        QString(),
        tr("Images (*.exr *.dpx *.png *.jpg *.jpeg *.tif *.tiff *.tga *.bmp)"));
    if (path.isEmpty()) return;
    displayPath_ = path;
    filename_->setText(path);
    onFilenameChanged();
}

void TrackStrip_Qt::onFromChanged(int v) {
    if (refreshing_) return;
    if (v >= to_->value()) {
        refreshing_ = true;
        const int clamped = std::max(0, to_->value() - 1);
        from_->setValue(clamped);
        refreshing_ = false;
        return;
    }
    jefe::qt::setTrackFrom(trackIdx_, v);
    emit trackEdited(trackIdx_);
}

void TrackStrip_Qt::onToChanged(int v) {
    if (refreshing_) return;
    if (v <= from_->value()) {
        refreshing_ = true;
        to_->setValue(from_->value() + 1);
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

void TrackStrip_Qt::onCropToggled(bool on) {
    if (refreshing_) return;
    jefe::qt::setTrackCrop(trackIdx_, on);
    emit trackEdited(trackIdx_);
}

void TrackStrip_Qt::onReload() {
    if (refreshing_) return;
    pushRecentPath(filename_->text());
    // Drive the re-decode directly so the Reload button doesn't depend
    // on the dialog routing trackEdited through reloadTrackPreview.
    jefe::qt::reloadTrackPreview(trackIdx_);
    emit trackEdited(trackIdx_);
}

void TrackStrip_Qt::onUnload() {
    jefe::qt::unloadAndClearTrack(trackIdx_);
    refreshFromGUI();
    emit trackEdited(trackIdx_);
}

void TrackStrip_Qt::onRecentSelected(const QString& path) {
    displayPath_ = path;
    filename_->setText(path);
    onFilenameChanged();
}

void TrackStrip_Qt::pushRecentPath(const QString& path) {
    if (path.isEmpty()) return;
    QStringList recents = loadRecentPaths();
    recents.removeAll(path);
    recents.prepend(path);
    while (recents.size() > 10) recents.removeLast();
    QSettings s;
    s.setValue(QString("loadwindow/recent/%1").arg(trackIdx_), recents);
    rebuildRecentMenu();
}

QStringList TrackStrip_Qt::loadRecentPaths() const {
    QSettings s;
    return s.value(QString("loadwindow/recent/%1").arg(trackIdx_)).toStringList();
}

bool TrackStrip_Qt::eventFilter(QObject* o, QEvent* e) {
    if (o == filename_) {
        if (e->type() == QEvent::FocusIn) {
            // Restore the full path so the user can see/edit it.
            QSignalBlocker b(filename_);
            filename_->setText(displayPath_);
        } else if (e->type() == QEvent::FocusOut) {
            applyElidedFilenameText();
        }
    }
    return QWidget::eventFilter(o, e);
}

void TrackStrip_Qt::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    applyElidedFilenameText();
}

void TrackStrip_Qt::applyElidedFilenameText() {
    if (!filename_) return;
    if (filename_->hasFocus()) return;
    QFontMetrics fm(filename_->font());
    const int avail = std::max(40, filename_->width() - 20);
    const QString elided = fm.elidedText(displayPath_, Qt::ElideMiddle, avail);
    QSignalBlocker b(filename_);
    filename_->setText(elided);
    filename_->setCursorPosition(0);
}

void TrackStrip_Qt::rebuildRecentMenu() {
    auto* menu = recent_->menu();
    menu->clear();
    const QStringList recents = loadRecentPaths();
    if (recents.isEmpty()) {
        menu->addAction("(no recent files)")->setEnabled(false);
        return;
    }
    for (const QString& path : recents) {
        QAction* a = menu->addAction(path);
        connect(a, &QAction::triggered, this, [this, path]() {
            onRecentSelected(path);
        });
    }
}

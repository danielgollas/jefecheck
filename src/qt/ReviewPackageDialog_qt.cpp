#include "ReviewPackageDialog_qt.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

#include "qticons.h"

namespace {
/** @a path with ".jcreview" appended unless it already ends in it (any case). */
QString withPackageSuffix(const QString& path) {
    return QFileInfo(path).suffix().compare(QLatin1String("jcreview"), Qt::CaseInsensitive) == 0
               ? path
               : path + QStringLiteral(".jcreview");
}
}  // namespace

ReviewPackageDialog_Qt::ReviewPackageDialog_Qt(Gather gather, qint64 mediaBytes, QWidget* parent)
    : QDialog(parent), gather_(std::move(gather)), mediaBytes_(mediaBytes) {
    setWindowTitle(tr("Export Review Package"));
    setObjectName("dialog.package");

    pathEdit_ = new QLineEdit(this);
    pathEdit_->setObjectName("dialog.package.path.edit");
    pathEdit_->setPlaceholderText(tr("Package file (.jcreview)"));
    browseButton_ = new QPushButton(jefe::qticons::folder(), tr("Browse…"), this);
    browseButton_->setObjectName("dialog.package.browse.button");
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(pathEdit_, 1);
    pathRow->addWidget(browseButton_);

    includeMediaCheck_ = new QCheckBox(tr("Include media"), this);
    includeMediaCheck_->setObjectName("dialog.package.includemedia.check");
    includeMediaCheck_->setChecked(true);
    includeMediaCheck_->setToolTip(tr("Copy the footage into the package so it opens anywhere. "
                                      "Untick for shared storage: the package then finds the media by its fingerprint."));
    sizeLabel_ = new QLabel(this);
    sizeLabel_->setObjectName("dialog.package.size.label");
    auto* mediaRow = new QHBoxLayout;
    mediaRow->addWidget(includeMediaCheck_);
    mediaRow->addWidget(sizeLabel_, 1);

    progress_ = new QProgressBar(this);
    progress_->setObjectName("dialog.package.progress");
    progress_->setRange(0, 100);
    progress_->setValue(0);
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName("dialog.package.status.label");
    statusLabel_->setWordWrap(true);

    exportButton_ = new QPushButton(tr("Export"), this);
    exportButton_->setObjectName("dialog.package.export.button");
    exportButton_->setDefault(true);
    cancelButton_ = new QPushButton(tr("Cancel"), this);
    cancelButton_->setObjectName("dialog.package.cancel.button");
    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(cancelButton_);
    buttons->addWidget(exportButton_);

    auto* form = new QFormLayout;
    form->addRow(tr("File:"), pathRow);
    form->addRow(tr("Media:"), mediaRow);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(progress_);
    layout->addWidget(statusLabel_);
    layout->addLayout(buttons);

    connect(browseButton_, &QPushButton::clicked, this, [this]() {
        QString out = QFileDialog::getSaveFileName(this, tr("Export Review Package"), pathEdit_->text(),
                                                   tr("JefeCheck Review Package (*.jcreview)"));
        if (out.isEmpty()) return;
        pathEdit_->setText(withPackageSuffix(out));
    });
    connect(includeMediaCheck_, &QCheckBox::toggled, this, [this]() { updateSizeLabel(); });
    connect(exportButton_, &QPushButton::clicked, this, [this]() { startExport(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this]() {
        if (running_) cancelExport();
        else reject();
    });
    updateSizeLabel();
}

ReviewPackageDialog_Qt::~ReviewPackageDialog_Qt() {
    if (running_) exporter_.cancel();
}

void ReviewPackageDialog_Qt::setOutputPath(const QString& path) { pathEdit_->setText(path); }

void ReviewPackageDialog_Qt::setIncludeMedia(bool on) { includeMediaCheck_->setChecked(on); }

QString ReviewPackageDialog_Qt::formatBytes(qint64 bytes) {
    static const char* const units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = double(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    // Rounding to one decimal can carry the displayed value up to 1024 (e.g.
    // 1048525 bytes is 1023.95 KB, which rounds to "1024.0 KB"); bump the
    // unit once more so the value shown is always what actually displays.
    if (unit > 0 && unit < 4 && std::round(value * 10.0) / 10.0 >= 1024.0) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0) return QString("%1 B").arg(bytes);
    return QString("%1 %2").arg(value, 0, 'f', 1).arg(units[unit]);
}

int ReviewPackageDialog_Qt::progressPercent() const {
    const qint64 total = exporter_.bytesTotal();
    return total > 0 ? int((exporter_.bytesDone() * 100) / total) : 0;
}

void ReviewPackageDialog_Qt::startExport() {
    if (running_) return;
    QString out = pathEdit_->text().trimmed();
    if (out.isEmpty()) {
        finishWith(false, tr("Choose where to write the package"));
        return;
    }
    // Always a .jcreview: a path naming some other file (out.txt) must not
    // have that file replaced by the package.
    out = withPackageSuffix(out);
    pathEdit_->setText(out);

    jefe::qt::package::ExportInput input;
    QString message;
    if (!gather_(out, includeMediaCheck_->isChecked(), input, &message)) {
        finishWith(false, message);
        return;
    }
    if (!exporter_.begin(input, &message)) {
        finishWith(false, message);
        return;
    }
    outPath_ = out;
    running_ = true;
    cancelButton_->setText(tr("Cancel"));
    progress_->setValue(0);
    statusLabel_->setText(tr("Writing %1…").arg(QFileInfo(out).fileName()));
    setInputsEnabled(false);
    QTimer::singleShot(0, this, [this]() { stepOnce(); });
}

void ReviewPackageDialog_Qt::stepOnce() {
    if (!running_) return;
    using State = jefe::qt::package::Exporter::State;
    QString err;
    const State state = exporter_.step(&err);
    progress_->setValue(progressPercent());
    if (state == State::Running) {
        QTimer::singleShot(0, this, [this]() { stepOnce(); });
        return;
    }
    running_ = false;
    if (state == State::Done) {
        finishWith(true, tr("Review package written: %1").arg(outPath_));
    } else {
        finishWith(false, err.isEmpty() ? tr("Export failed") : err);
    }
}

void ReviewPackageDialog_Qt::cancelExport() {
    if (!running_) return;
    exporter_.cancel();
    running_ = false;
    finishWith(false, tr("Export cancelled"));
}

void ReviewPackageDialog_Qt::reject() {
    if (running_) cancelExport();
    QDialog::reject();
}

void ReviewPackageDialog_Qt::finishWith(bool ok, const QString& message) {
    lastMessage_ = message;
    statusLabel_->setText(message);
    setInputsEnabled(true);
    if (ok) {
        progress_->setValue(100);
        cancelButton_->setText(tr("Close"));
    }
}

void ReviewPackageDialog_Qt::setInputsEnabled(bool enabled) {
    exportButton_->setEnabled(enabled);
    pathEdit_->setEnabled(enabled);
    browseButton_->setEnabled(enabled);
    includeMediaCheck_->setEnabled(enabled);
}

void ReviewPackageDialog_Qt::updateSizeLabel() {
    sizeLabel_->setText(includeMediaCheck_->isChecked()
                            ? tr("adds %1 of media").arg(formatBytes(mediaBytes_))
                            : tr("media is referenced, not copied"));
}

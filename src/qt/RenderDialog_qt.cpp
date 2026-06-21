#include "RenderDialog_qt.h"
#include "GlViewport_qt.h"
#include "MainWindow_qt.h"
#include "SequenceLoadBridge_qt.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

// Format combo entries — index here MUST match the gfcRenderFormats
// enum order: 0=JPEG, 1=EXR, 2=TIFF, 3=TGA, 4=BMP, 5=PNG. The
// secondary string is the file-extension/format-string the renderer
// passes through CreateRenderFilename.
struct FormatEntry {
    const char* label;
    const char* ext;
};

constexpr FormatEntry kFormats[] = {
    {"JPEG", "jpg"},
    {"OpenEXR", "exr"},
    {"TIFF", "tif"},
    {"Targa", "tga"},
    {"BMP", "bmp"},
    {"PNG", "png"},
};

constexpr const char* kRenderDirSettingKey = "Render/lastDir";

}  // namespace

RenderDialog_Qt::RenderDialog_Qt(QWidget* parent) : QDialog(parent) {
    setObjectName("dialog.render");
    setWindowTitle("Render");
    setModal(true);
    resize(520, 360);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);

    // Quadrant — 4 plates available.
    quadrantCombo_ = new QComboBox(this);
    quadrantCombo_->setObjectName("dialog.render.quadrant.combo");
    quadrantCombo_->addItems({"Plate 1", "Plate 2", "Plate 3", "Plate 4"});
    form->addRow("Quadrant:", quadrantCombo_);

    // Format.
    formatCombo_ = new QComboBox(this);
    formatCombo_->setObjectName("dialog.render.format.combo");
    for (const auto& f : kFormats) {
        formatCombo_->addItem(f.label);
    }
    form->addRow("Format:", formatCombo_);

    // Format-specific quality controls, one page per format index.
    qualityStack_ = new QStackedWidget(this);
    qualityStack_->setObjectName("dialog.render.quality.stack");

    // 0 — JPEG: quality 0..100.
    jpegQualitySpin_ = new QSpinBox(this);
    jpegQualitySpin_->setObjectName("dialog.render.jpegquality.spin");
    jpegQualitySpin_->setRange(0, 100);
    jpegQualitySpin_->setValue(95);
    jpegQualitySpin_->setSuffix("  (quality)");
    qualityStack_->insertWidget(0, jpegQualitySpin_);

    // 1 — EXR: depth + compression.
    {
        auto* exrRow = new QWidget(this);
        auto* l = new QHBoxLayout(exrRow);
        l->setContentsMargins(0, 0, 0, 0);
        exrDepthCombo_ = new QComboBox(exrRow);
        exrDepthCombo_->setObjectName("dialog.render.exrdepth.combo");
        exrDepthCombo_->addItems({"Half", "Float"});   // GFC_HALF=0, GFC_FLOAT=1
        exrCompCombo_ = new QComboBox(exrRow);
        exrCompCombo_->setObjectName("dialog.render.exrcomp.combo");
        exrCompCombo_->addItems({"ZIP", "PIZ", "None"}); // 0/1/2
        l->addWidget(new QLabel("Depth:", exrRow));
        l->addWidget(exrDepthCombo_);
        l->addWidget(new QLabel("Compression:", exrRow));
        l->addWidget(exrCompCombo_);
        l->addStretch(1);
        qualityStack_->insertWidget(1, exrRow);
    }

    // 2 — TIFF: compression.
    tiffCompCombo_ = new QComboBox(this);
    tiffCompCombo_->setObjectName("dialog.render.tiffcomp.combo");
    tiffCompCombo_->addItems({"LZW", "None", "ZIP"});  // 0/1/2
    qualityStack_->insertWidget(2, tiffCompCombo_);

    // 3 — TGA, 4 — BMP: no options.
    qualityStack_->insertWidget(3, new QLabel("(no options)", this));
    qualityStack_->insertWidget(4, new QLabel("(no options)", this));

    // 5 — PNG: zlib compression level 0..9.
    pngLevelSpin_ = new QSpinBox(this);
    pngLevelSpin_->setObjectName("dialog.render.pnglevel.spin");
    pngLevelSpin_->setRange(0, 9);
    pngLevelSpin_->setValue(6);
    pngLevelSpin_->setSuffix("  (compression 0–9)");
    qualityStack_->insertWidget(5, pngLevelSpin_);

    form->addRow("Quality:", qualityStack_);

    // Frame range — start + end + auto-fill from playback.
    startFrameSpin_ = new QSpinBox(this);
    startFrameSpin_->setObjectName("dialog.render.startframe.spin");
    startFrameSpin_->setRange(0, 999999);
    startFrameSpin_->setValue(1);

    endFrameSpin_ = new QSpinBox(this);
    endFrameSpin_->setObjectName("dialog.render.endframe.spin");
    endFrameSpin_->setRange(0, 999999);
    endFrameSpin_->setValue(1);

    autoRangeBtn_ = new QPushButton("Auto-range", this);
    autoRangeBtn_->setObjectName("dialog.render.autorange.button");
    autoRangeBtn_->setToolTip(
        "Fill range from playback in/out points");

    auto* rangeRow = new QHBoxLayout();
    rangeRow->setContentsMargins(0, 0, 0, 0);
    rangeRow->addWidget(startFrameSpin_);
    rangeRow->addWidget(new QLabel("→", this));
    rangeRow->addWidget(endFrameSpin_);
    rangeRow->addWidget(autoRangeBtn_);
    form->addRow("Frames:", rangeRow);

    // Padding.
    paddingSpin_ = new QSpinBox(this);
    paddingSpin_->setObjectName("dialog.render.padding.spin");
    paddingSpin_->setRange(0, 10);
    paddingSpin_->setValue(4);
    form->addRow("Padding:", paddingSpin_);

    // Scale.
    scaleSpin_ = new QDoubleSpinBox(this);
    scaleSpin_->setObjectName("dialog.render.scale.spin");
    scaleSpin_->setRange(0.05, 4.0);
    scaleSpin_->setSingleStep(0.05);
    scaleSpin_->setValue(1.0);
    scaleSpin_->setDecimals(2);
    form->addRow("Scale:", scaleSpin_);

    // Output path + browse.
    pathEdit_ = new QLineEdit(this);
    pathEdit_->setObjectName("dialog.render.path.edit");
    pathEdit_->setPlaceholderText("Output directory (required)");
    QSettings s;
    pathEdit_->setText(s.value(kRenderDirSettingKey).toString());

    browseBtn_ = new QPushButton("Browse…", this);
    browseBtn_->setObjectName("dialog.render.browse.button");

    auto* pathRow = new QHBoxLayout();
    pathRow->setContentsMargins(0, 0, 0, 0);
    pathRow->addWidget(pathEdit_, /*stretch*/ 1);
    pathRow->addWidget(browseBtn_);
    form->addRow("Output dir:", pathRow);

    // Prefix / postfix.
    prefixEdit_ = new QLineEdit(this);
    prefixEdit_->setObjectName("dialog.render.prefix.edit");
    prefixEdit_->setPlaceholderText("Optional");
    form->addRow("Prefix:", prefixEdit_);

    postfixEdit_ = new QLineEdit(this);
    postfixEdit_->setObjectName("dialog.render.postfix.edit");
    postfixEdit_->setPlaceholderText("Optional");
    form->addRow("Postfix:", postfixEdit_);

    // Preview label — first / last filename.
    previewLabel_ = new QLabel(this);
    previewLabel_->setObjectName("dialog.render.preview.label");
    previewLabel_->setStyleSheet("color: #aaa; font-family: monospace;");
    previewLabel_->setWordWrap(true);
    previewLabel_->setMinimumHeight(40);
    form->addRow("Preview:", previewLabel_);

    // Status (e.g. "Rendered N frames").
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName("dialog.render.status.label");
    statusLabel_->setStyleSheet("color: #888; font-style: italic;");

    // Render / Done buttons.
    renderBtn_ = new QPushButton("Render", this);
    renderBtn_->setObjectName("dialog.render.render.button");
    renderBtn_->setDefault(true);

    doneBtn_ = new QPushButton("Done", this);
    doneBtn_->setObjectName("dialog.render.done.button");

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addWidget(statusLabel_, /*stretch*/ 1);
    buttonRow->addWidget(renderBtn_);
    buttonRow->addWidget(doneBtn_);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(10);
    outer->addLayout(form);
    outer->addLayout(buttonRow);

    // Wire signals. Any field change rebuilds the preview and
    // re-evaluates render-button enable state.
    auto onChange = [this]() { onAnyFieldChanged(); };
    connect(formatCombo_,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, onChange](int) { updateQualityPage(); onChange(); });
    connect(startFrameSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [onChange](int) { onChange(); });
    connect(endFrameSpin_,   QOverload<int>::of(&QSpinBox::valueChanged),
            this, [onChange](int) { onChange(); });
    connect(paddingSpin_,    QOverload<int>::of(&QSpinBox::valueChanged),
            this, [onChange](int) { onChange(); });
    connect(pathEdit_,       &QLineEdit::textChanged,
            this, [onChange](const QString&) { onChange(); });
    connect(prefixEdit_,     &QLineEdit::textChanged,
            this, [onChange](const QString&) { onChange(); });
    connect(postfixEdit_,    &QLineEdit::textChanged,
            this, [onChange](const QString&) { onChange(); });
    connect(quadrantCombo_,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [onChange](int) { onChange(); });

    connect(browseBtn_,    &QPushButton::clicked,
            this, &RenderDialog_Qt::browseForOutputDir);
    connect(autoRangeBtn_, &QPushButton::clicked,
            this, &RenderDialog_Qt::onAutoRangeClicked);
    connect(renderBtn_,    &QPushButton::clicked,
            this, &RenderDialog_Qt::onRenderClicked);
    connect(doneBtn_,      &QPushButton::clicked,
            this, &QDialog::accept);

    // Sensible default range from the playback in/out points.
    onAutoRangeClicked();
    updateQualityPage();
    rebuildPreview();
}

void RenderDialog_Qt::updateQualityPage() {
    const int idx = formatCombo_->currentIndex();
    if (idx >= 0 && idx < qualityStack_->count()) {
        qualityStack_->setCurrentIndex(idx);
    }
}

void RenderDialog_Qt::browseForOutputDir() {
    const QString seed = pathEdit_->text().isEmpty()
        ? QDir::homePath()
        : pathEdit_->text();
    const QString chosen = QFileDialog::getExistingDirectory(
        this, "Select output directory", seed);
    if (!chosen.isEmpty()) {
        pathEdit_->setText(chosen);
        QSettings s;
        s.setValue(kRenderDirSettingKey, chosen);
    }
}

void RenderDialog_Qt::onAutoRangeClicked() {
    const int from = jefe::qt::getFromFrame();
    const int to   = jefe::qt::getToFrame();
    if (from > 0 || to > 0) {
        startFrameSpin_->setValue(from);
        endFrameSpin_->setValue(to);
    }
    rebuildPreview();
}

void RenderDialog_Qt::onAnyFieldChanged() {
    rebuildPreview();
    renderBtn_->setEnabled(inputsValid());
}

bool RenderDialog_Qt::inputsValid() const {
    if (pathEdit_->text().trimmed().isEmpty()) return false;
    if (startFrameSpin_->value() > endFrameSpin_->value()) return false;
    return true;
}

void RenderDialog_Qt::rebuildPreview() {
    jefe::qt::RenderParams p;
    p.quadrant     = quadrantCombo_->currentIndex();
    p.format       = formatCombo_->currentIndex();
    p.formatString = (p.format >= 0 && p.format < int(std::size(kFormats)))
        ? kFormats[p.format].ext : "";
    p.from    = startFrameSpin_->value();
    p.to      = endFrameSpin_->value();
    p.padding = paddingSpin_->value();
    p.scale   = static_cast<float>(scaleSpin_->value());
    p.path    = pathEdit_->text().toStdString();
    p.prefix  = prefixEdit_->text().toStdString();
    p.postfix = postfixEdit_->text().toStdString();

    if (!inputsValid()) {
        previewLabel_->setText("(set output dir + valid frame range)");
        renderBtn_->setEnabled(false);
        return;
    }

    // Render first + last filenames.
    auto firstName = QString::fromStdString(jefe::qt::previewRenderFilename(p));
    p.from = p.to;
    p.to   = p.to;
    auto firstP = p;  // copy for last preview
    firstP.from = endFrameSpin_->value();
    auto lastName = QString::fromStdString(jefe::qt::previewRenderFilename(firstP));
    previewLabel_->setText(
        QString("%1\n…\n%2").arg(firstName).arg(lastName));
    renderBtn_->setEnabled(true);
}

void RenderDialog_Qt::onRenderClicked() {
    if (!inputsValid()) return;

    jefe::qt::RenderParams p;
    p.quadrant     = quadrantCombo_->currentIndex();
    p.format       = formatCombo_->currentIndex();
    p.formatString = (p.format >= 0 && p.format < int(std::size(kFormats)))
        ? kFormats[p.format].ext : "";
    p.from    = startFrameSpin_->value();
    p.to      = endFrameSpin_->value();
    p.padding = paddingSpin_->value();
    p.scale   = static_cast<float>(scaleSpin_->value());
    p.path    = pathEdit_->text().toStdString();
    p.prefix  = prefixEdit_->text().toStdString();
    p.postfix = postfixEdit_->text().toStdString();

    // Format-specific quality knobs (the saver reads whichever apply).
    p.jpegQuality     = jpegQualitySpin_->value();
    p.pngQuality      = pngLevelSpin_->value();
    p.tiffCompression = tiffCompCombo_->currentIndex();
    p.exrCompression  = exrCompCombo_->currentIndex();
    p.exrFormat       = exrDepthCombo_->currentIndex();

    statusLabel_->setText("Rendering…");
    renderBtn_->setEnabled(false);
    doneBtn_->setEnabled(false);
    QApplication::processEvents();

    // renderPlate drives gfcPlate::draw() directly — that issues GL calls
    // (glGetTexImage, FBO binds) that need the viewport's context current,
    // which it isn't outside paintGL. Make it current around the render.
    GlViewport_Qt* vp = nullptr;
    if (auto* mw = qobject_cast<MainWindow_Qt*>(window())) {
        vp = mw->viewport();
    }
    if (vp) vp->makeCurrent();

    // Synchronous — the dialog will appear frozen until the render
    // completes. PR-39b moves this onto a QThread.
    const int n = jefe::qt::triggerSyncRender(p);

    if (vp) vp->doneCurrent();

    statusLabel_->setText(QString("Rendered %1 frame(s)").arg(n));
    renderBtn_->setEnabled(inputsValid());
    doneBtn_->setEnabled(true);
}

#include "RenderDialog_qt.h"
#include "GlViewport_qt.h"
#include "MainWindow_qt.h"
#include "SequenceLoadBridge_qt.h"
#include "VideoEncoder_qt.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QTimer>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QUrl>
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

// Video formats live at combo indices >= kFirstVideoFormat, after the
// still formats. They render a temp PNG sequence, then encode via FFmpeg.
constexpr int kFirstVideoFormat = 6;  // == std::size(kFormats)

struct VideoEntry {
    const char* label;
    VideoEncoder_Qt::Codec codec;
};
const VideoEntry kVideoFormats[] = {
    {"H.264 (MP4)",   VideoEncoder_Qt::Codec::H264},
    {"H.265 (MP4)",   VideoEncoder_Qt::Codec::H265},
    {"ProRes (MOV)",  VideoEncoder_Qt::Codec::ProRes},
};

bool isVideoFormat(int idx) { return idx >= kFirstVideoFormat; }
VideoEncoder_Qt::Codec codecForFormat(int idx) {
    const int v = idx - kFirstVideoFormat;
    if (v >= 0 && v < int(std::size(kVideoFormats))) return kVideoFormats[v].codec;
    return VideoEncoder_Qt::Codec::H264;
}

constexpr const char* kRenderDirSettingKey = "Render/lastDir";

// Wrap a success message as a clickable link (href is a sentinel; the
// dialog opens lastOutputDir_ on linkActivated).
QString folderLink(const QString& message) {
    return QString("<a href=\"#open\" style=\"color:#6db3f2; "
                   "text-decoration:none;\">%1 — Show in folder ↗</a>")
        .arg(message.toHtmlEscaped());
}

}  // namespace

RenderDialog_Qt::RenderDialog_Qt(QWidget* parent) : QDialog(parent) {
    setObjectName("dialog.render");
    setWindowTitle("Render");
    setModal(true);
    resize(560, 480);

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
    const bool ffmpegOk = VideoEncoder_Qt::available();
    for (const auto& v : kVideoFormats) {
        formatCombo_->addItem(ffmpegOk ? v.label
                                       : QString("%1 — ffmpeg not found").arg(v.label));
    }
    form->addRow("Format:", formatCombo_);

    // Format-specific quality controls, one page per format index.
    qualityStack_ = new QStackedWidget(this);
    qualityStack_->setObjectName("dialog.render.quality.stack");

    // 0 — JPEG: quality + progressive + chroma subsampling.
    {
        auto* jpegPage = new QWidget(this);
        auto* l = new QHBoxLayout(jpegPage);
        l->setContentsMargins(0, 0, 0, 0);
        jpegQualitySpin_ = new QSpinBox(jpegPage);
        jpegQualitySpin_->setObjectName("dialog.render.jpegquality.spin");
        jpegQualitySpin_->setRange(0, 100);
        jpegQualitySpin_->setValue(95);
        jpegProgressiveCheck_ = new QCheckBox("Progressive", jpegPage);
        jpegProgressiveCheck_->setObjectName("dialog.render.jpegprogressive.check");
        jpegSubsamplingCombo_ = new QComboBox(jpegPage);
        jpegSubsamplingCombo_->setObjectName("dialog.render.jpegsubsampling.combo");
        jpegSubsamplingCombo_->addItems({"4:4:4", "4:2:2", "4:2:0"});  // 0/1/2
        l->addWidget(new QLabel("Quality:", jpegPage));
        l->addWidget(jpegQualitySpin_);
        l->addWidget(jpegProgressiveCheck_);
        l->addWidget(new QLabel("Chroma:", jpegPage));
        l->addWidget(jpegSubsamplingCombo_);
        l->addStretch(1);
        qualityStack_->insertWidget(0, jpegPage);
    }

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
        // Index order must match kExrComp in gfcimagesaver.cpp.
        exrCompCombo_->addItems({"None", "RLE", "ZIPS", "ZIP", "PIZ",
                                 "PXR24", "B44", "B44A", "DWAA", "DWAB"});
        exrCompCombo_->setCurrentIndex(3);  // ZIP
        l->addWidget(new QLabel("Depth:", exrRow));
        l->addWidget(exrDepthCombo_);
        l->addWidget(new QLabel("Compression:", exrRow));
        l->addWidget(exrCompCombo_);
        l->addStretch(1);
        qualityStack_->insertWidget(1, exrRow);
    }

    // 2 — TIFF: compression + bit depth.
    {
        auto* tiffPage = new QWidget(this);
        auto* l = new QHBoxLayout(tiffPage);
        l->setContentsMargins(0, 0, 0, 0);
        tiffCompCombo_ = new QComboBox(tiffPage);
        tiffCompCombo_->setObjectName("dialog.render.tiffcomp.combo");
        tiffCompCombo_->addItems({"LZW", "None", "ZIP"});  // 0/1/2
        tiffBitDepthCombo_ = new QComboBox(tiffPage);
        tiffBitDepthCombo_->setObjectName("dialog.render.tiffbitdepth.combo");
        tiffBitDepthCombo_->addItems({"8-bit", "16-bit"});
        l->addWidget(new QLabel("Compression:", tiffPage));
        l->addWidget(tiffCompCombo_);
        l->addWidget(new QLabel("Depth:", tiffPage));
        l->addWidget(tiffBitDepthCombo_);
        l->addStretch(1);
        qualityStack_->insertWidget(2, tiffPage);
    }

    // 3 — TGA, 4 — BMP: no options.
    qualityStack_->insertWidget(3, new QLabel("(no options)", this));
    qualityStack_->insertWidget(4, new QLabel("(no options)", this));

    // 5 — PNG: zlib compression level + bit depth.
    {
        auto* pngPage = new QWidget(this);
        auto* l = new QHBoxLayout(pngPage);
        l->setContentsMargins(0, 0, 0, 0);
        pngLevelSpin_ = new QSpinBox(pngPage);
        pngLevelSpin_->setObjectName("dialog.render.pnglevel.spin");
        pngLevelSpin_->setRange(0, 9);
        pngLevelSpin_->setValue(6);
        pngBitDepthCombo_ = new QComboBox(pngPage);
        pngBitDepthCombo_->setObjectName("dialog.render.pngbitdepth.combo");
        pngBitDepthCombo_->addItems({"8-bit", "16-bit"});
        l->addWidget(new QLabel("Compression (0–9):", pngPage));
        l->addWidget(pngLevelSpin_);
        l->addWidget(new QLabel("Depth:", pngPage));
        l->addWidget(pngBitDepthCombo_);
        l->addStretch(1);
        qualityStack_->insertWidget(5, pngPage);
    }

    // 6 — Video (shared by all video codecs): fps + quality. The codec is
    // chosen by the format combo, so updateQualityPage() maps every video
    // format index to this single page.
    {
        auto* vpage = new QWidget(this);
        auto* l = new QHBoxLayout(vpage);
        l->setContentsMargins(0, 0, 0, 0);
        videoFpsSpin_ = new QSpinBox(vpage);
        videoFpsSpin_->setObjectName("dialog.render.videofps.spin");
        videoFpsSpin_->setRange(1, 120);
        videoFpsSpin_->setValue(24);
        videoQualitySpin_ = new QSpinBox(vpage);
        videoQualitySpin_->setObjectName("dialog.render.videoquality.spin");
        videoQualitySpin_->setRange(0, 100);
        videoQualitySpin_->setValue(80);
        videoBitrateModeCombo_ = new QComboBox(vpage);
        videoBitrateModeCombo_->setObjectName("dialog.render.videobitratemode.combo");
        videoBitrateModeCombo_->addItems({"Quality", "Bitrate"});
        videoBitrateSpin_ = new QSpinBox(vpage);
        videoBitrateSpin_->setObjectName("dialog.render.videobitrate.spin");
        videoBitrateSpin_->setRange(1, 500);
        videoBitrateSpin_->setValue(20);
        videoBitrateSpin_->setSuffix(" Mbps");
        videoBitrateSpin_->setEnabled(false);  // enabled only in Bitrate mode
        videoPresetCombo_ = new QComboBox(vpage);
        videoPresetCombo_->setObjectName("dialog.render.videopreset.combo");
        videoPresetCombo_->addItems({"ultrafast", "superfast", "veryfast",
                                     "faster", "medium", "slow", "slower",
                                     "veryslow", "placebo"});
        videoPresetCombo_->setCurrentIndex(4);  // medium
        // Quality spin vs bitrate spin enable by mode.
        connect(videoBitrateModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int idx) {
            const bool bitrate = (idx == 1);
            videoBitrateSpin_->setEnabled(bitrate);
            videoQualitySpin_->setEnabled(!bitrate);
        });
        l->addWidget(new QLabel("FPS:", vpage));
        l->addWidget(videoFpsSpin_);
        l->addWidget(new QLabel("Rate:", vpage));
        l->addWidget(videoBitrateModeCombo_);
        l->addWidget(videoQualitySpin_);
        l->addWidget(videoBitrateSpin_);
        l->addWidget(new QLabel("Preset:", vpage));
        l->addWidget(videoPresetCombo_);
        l->addStretch(1);
        qualityStack_->insertWidget(6, vpage);
    }

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

    // Resolution: a scale preset combo + explicit W×H spinners. The combo
    // scales relative to the source; the spinners show (and let you override)
    // the final output size and are what the render actually uses.
    resolutionCombo_ = new QComboBox(this);
    resolutionCombo_->setObjectName("dialog.render.resolution.combo");
    resolutionCombo_->addItems({"Source (100%)", "75%", "50%", "25%", "Custom"});
    widthSpin_ = new QSpinBox(this);
    widthSpin_->setObjectName("dialog.render.width.spin");
    widthSpin_->setRange(1, 16384);
    widthSpin_->setValue(1920);
    heightSpin_ = new QSpinBox(this);
    heightSpin_->setObjectName("dialog.render.height.spin");
    heightSpin_->setRange(1, 16384);
    heightSpin_->setValue(1080);
    auto* resRow = new QHBoxLayout();
    resRow->setContentsMargins(0, 0, 0, 0);
    resRow->addWidget(resolutionCombo_);
    resRow->addWidget(widthSpin_);
    resRow->addWidget(new QLabel("×", this));
    resRow->addWidget(heightSpin_);
    resRow->addStretch(1);
    form->addRow("Resolution:", resRow);

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

    // Preview label — first / last filename. Shows three lines
    // (first … last), so give it room and top-align it so long output
    // paths aren't clipped/squashed.
    previewLabel_ = new QLabel(this);
    previewLabel_->setObjectName("dialog.render.preview.label");
    previewLabel_->setStyleSheet("color: #aaa; font-family: monospace;");
    previewLabel_->setWordWrap(true);
    previewLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    previewLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    previewLabel_->setMinimumHeight(72);
    previewLabel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
    form->addRow("Preview:", previewLabel_);

    // Progress bar — fills per frame during render and stays at 100% when
    // done so it's clear the render finished. Hidden until the first render.
    progressBar_ = new QProgressBar(this);
    progressBar_->setObjectName("dialog.render.progress.bar");
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(true);
    progressBar_->setVisible(false);
    // Explicit style: the native macOS progress bar ignores setTextVisible,
    // so the "Rendering… X/Y" / "Done" text wouldn't show. This also gives
    // it a clear height and a green chunk when complete reads as "done".
    progressBar_->setStyleSheet(
        "QProgressBar {"
        "  border: 1px solid #555; border-radius: 3px; background: #222;"
        "  text-align: center; color: #eee; min-height: 20px; }"
        "QProgressBar::chunk { background-color: #3b7dd8; border-radius: 2px; }");

    // Status (e.g. "Rendered N frames"). After a successful render the text
    // becomes a link that reveals the output folder (lastOutputDir_).
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName("dialog.render.status.label");
    statusLabel_->setStyleSheet("color: #888; font-style: italic;");
    statusLabel_->setOpenExternalLinks(false);
    statusLabel_->setTextInteractionFlags(Qt::LinksAccessibleByMouse |
                                          Qt::LinksAccessibleByKeyboard);
    statusLabel_->setToolTip("Open the output folder");
    connect(statusLabel_, &QLabel::linkActivated, this, [this](const QString&) {
        if (!lastOutputDir_.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(lastOutputDir_));
    });

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
    outer->addWidget(progressBar_);
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
            this, [this, onChange](int) {
        refreshSourceSize();
        applyResolutionPreset();
        onChange();
    });

    // Resolution: preset combo recomputes W×H; editing W/H flips to Custom.
    connect(resolutionCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, onChange](int) { applyResolutionPreset(); onChange(); });
    auto onSizeEdited = [this, onChange]() {
        QSignalBlocker b(resolutionCombo_);
        resolutionCombo_->setCurrentIndex(4);   // Custom
        onChange();
    };
    connect(widthSpin_,  QOverload<int>::of(&QSpinBox::valueChanged),
            this, [onSizeEdited](int) { onSizeEdited(); });
    connect(heightSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [onSizeEdited](int) { onSizeEdited(); });

    connect(browseBtn_,    &QPushButton::clicked,
            this, &RenderDialog_Qt::browseForOutputDir);
    connect(autoRangeBtn_, &QPushButton::clicked,
            this, &RenderDialog_Qt::onAutoRangeClicked);
    connect(renderBtn_,    &QPushButton::clicked,
            this, &RenderDialog_Qt::onRenderClicked);
    connect(doneBtn_,      &QPushButton::clicked,
            this, &QDialog::accept);

    // Sensible default range from the playback in/out points, and seed the
    // resolution from the active plate's source size (combo defaults to
    // Source → spinners show the source dims).
    onAutoRangeClicked();
    refreshSourceSize();
    applyResolutionPreset();
    updateQualityPage();
    rebuildPreview();
}

void RenderDialog_Qt::refreshSourceSize() {
    int w = 0, h = 0;
    jefe::qt::getRenderSourceSize(quadrantCombo_->currentIndex(), w, h);
    sourceW_ = w;
    sourceH_ = h;
}

void RenderDialog_Qt::applyResolutionPreset() {
    const int idx = resolutionCombo_->currentIndex();
    if (idx >= 4) return;                 // Custom — leave the spinners alone
    if (sourceW_ <= 0 || sourceH_ <= 0) return;  // no footage loaded yet
    double frac = 1.0;
    if (idx == 1) frac = 0.75;
    else if (idx == 2) frac = 0.50;
    else if (idx == 3) frac = 0.25;
    const int w = qMax(1, int(sourceW_ * frac + 0.5));
    const int h = qMax(1, int(sourceH_ * frac + 0.5));
    QSignalBlocker bw(widthSpin_), bh(heightSpin_);
    widthSpin_->setValue(w);
    heightSpin_->setValue(h);
}

void RenderDialog_Qt::updateQualityPage() {
    const int idx = formatCombo_->currentIndex();
    // All video formats share the single video page (index 6).
    const int page = isVideoFormat(idx) ? 6 : idx;
    if (page >= 0 && page < qualityStack_->count()) {
        qualityStack_->setCurrentIndex(page);
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
    p.scale   = 1.0f;
    p.path    = pathEdit_->text().toStdString();
    p.prefix  = prefixEdit_->text().toStdString();
    p.postfix = postfixEdit_->text().toStdString();

    if (!inputsValid()) {
        previewLabel_->setText("(set output dir + valid frame range)");
        renderBtn_->setEnabled(false);
        return;
    }

    const int fmt = formatCombo_->currentIndex();
    if (isVideoFormat(fmt)) {
        // Video: a single output file in the chosen dir. Prefix names it;
        // fall back to "render" if none given.
        const auto codec = codecForFormat(fmt);
        QString base = prefixEdit_->text().trimmed();
        if (base.isEmpty()) base = "render";
        const QString out = QDir(pathEdit_->text()).filePath(
            base + "." + VideoEncoder_Qt::containerExt(codec));
        const int frames = endFrameSpin_->value() - startFrameSpin_->value() + 1;
        QString text = QString("%1\n(%2, %3 frames @ %4 fps)")
            .arg(out)
            .arg(VideoEncoder_Qt::codecLabel(codec))
            .arg(frames)
            .arg(videoFpsSpin_ ? videoFpsSpin_->value() : 24);
        if (!VideoEncoder_Qt::available())
            text += "\n⚠ FFmpeg not found — set its path in Preferences.";
        previewLabel_->setText(text);
        renderBtn_->setEnabled(VideoEncoder_Qt::available());
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

namespace {
const char* kBarBlue =
    "QProgressBar {"
    "  border: 1px solid #555; border-radius: 3px; background: #222;"
    "  text-align: center; color: #fff; min-height: 20px; }"
    "QProgressBar::chunk { background-color: #3b7dd8; border-radius: 2px; }";
const char* kBarGreen =
    "QProgressBar {"
    "  border: 1px solid #555; border-radius: 3px; background: #222;"
    "  text-align: center; color: #fff; min-height: 20px; }"
    "QProgressBar::chunk { background-color: #2e9e4f; border-radius: 2px; }";
const char* kBarAmber =
    "QProgressBar {"
    "  border: 1px solid #555; border-radius: 3px; background: #222;"
    "  text-align: center; color: #fff; min-height: 20px; }"
    "QProgressBar::chunk { background-color: #b07a1e; border-radius: 2px; }";
}  // namespace

void RenderDialog_Qt::onRenderClicked() {
    // The Render button doubles as Cancel while a render is running.
    if (rendering_) {
        cancelRequested_ = true;
        if (videoEncoder_ && videoEncoder_->isRunning())
            videoEncoder_->cancel();         // interrupt the ffmpeg pass
        renderBtn_->setEnabled(false);       // until the current step ends
        return;
    }
    startRender();
}

void RenderDialog_Qt::startRender() {
    if (!inputsValid()) return;

    const int fmt = formatCombo_->currentIndex();
    renderIsVideo_  = isVideoFormat(fmt);
    videoFormatIdx_ = fmt;
    videoTmpDir_.clear();
    videoOutFile_.clear();

    if (renderIsVideo_ && !VideoEncoder_Qt::available()) {
        statusLabel_->setText("FFmpeg not found — set its path in Preferences.");
        return;
    }

    renderParams_ = jefe::qt::RenderParams{};
    renderParams_.quadrant     = quadrantCombo_->currentIndex();
    renderParams_.scale   = 1.0f;
    renderParams_.outWidth  = widthSpin_->value();
    renderParams_.outHeight = heightSpin_->value();
    renderParams_.jpegQuality     = jpegQualitySpin_->value();
    renderParams_.jpegProgressive = jpegProgressiveCheck_->isChecked();
    renderParams_.jpegSubsampling = jpegSubsamplingCombo_->currentIndex();
    renderParams_.pngQuality      = pngLevelSpin_->value();
    renderParams_.tiffCompression = tiffCompCombo_->currentIndex();
    renderParams_.exrCompression  = exrCompCombo_->currentIndex();
    renderParams_.exrFormat       = exrDepthCombo_->currentIndex();
    // 16-bit only applies to PNG/TIFF (combo index 1 = 16-bit).
    if (fmt == 5)      renderParams_.bitsPerChannel = pngBitDepthCombo_->currentIndex() ? 16 : 8;
    else if (fmt == 2) renderParams_.bitsPerChannel = tiffBitDepthCombo_->currentIndex() ? 16 : 8;
    else               renderParams_.bitsPerChannel = 8;

    if (renderIsVideo_) {
        // Render a PNG sequence into a temp dir, then encode it. f_%04d.png.
        const auto codec = codecForFormat(fmt);
        QString base = prefixEdit_->text().trimmed();
        if (base.isEmpty()) base = "render";
        videoOutFile_ = QDir(pathEdit_->text())
            .filePath(base + "." + VideoEncoder_Qt::containerExt(codec));
        videoTmpDir_ = QDir(QDir::tempPath())
            .filePath(QString("jefecheck_vid_%1").arg(quintptr(this)));
        QDir().mkpath(videoTmpDir_);
        renderParams_.format       = 5;     // PNG
        renderParams_.formatString = "png";
        renderParams_.padding      = 4;
        renderParams_.prefix       = "f_";
        renderParams_.postfix      = "";
        renderParams_.path         = videoTmpDir_.toStdString();
    } else {
        renderParams_.format       = fmt;
        renderParams_.formatString =
            (fmt >= 0 && fmt < int(std::size(kFormats))) ? kFormats[fmt].ext : "";
        renderParams_.padding = paddingSpin_->value();
        renderParams_.path    = pathEdit_->text().toStdString();
        renderParams_.prefix  = prefixEdit_->text().toStdString();
        renderParams_.postfix = postfixEdit_->text().toStdString();
    }

    // Resolve the viewport once (walk parentWidget(), NOT window(): this is a
    // modal QDialog so window() returns the dialog itself, not the MainWindow).
    renderVp_ = nullptr;
    for (QWidget* w = parentWidget(); w; w = w->parentWidget()) {
        if (auto* mw = qobject_cast<MainWindow_Qt*>(w)) {
            renderVp_ = mw->viewport();
            break;
        }
    }

    renderCur_   = startFrameSpin_->value();
    renderTo_    = endFrameSpin_->value();
    renderDone_  = 0;
    renderTotal_ = renderTo_ - renderCur_ + 1;
    cancelRequested_ = false;
    rendering_   = true;

    progressBar_->setVisible(true);
    progressBar_->setStyleSheet(kBarBlue);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setFormat(QString("Rendering… 0/%1").arg(renderTotal_));
    statusLabel_->setText("Rendering…");
    renderBtn_->setText("Cancel");
    doneBtn_->setEnabled(false);
    // Lock the inputs while rendering so the snapshot in renderParams_ stays
    // consistent with what the progress reflects.
    for (QWidget* w : {(QWidget*)quadrantCombo_, (QWidget*)formatCombo_,
                       (QWidget*)startFrameSpin_, (QWidget*)endFrameSpin_,
                       (QWidget*)pathEdit_, (QWidget*)browseBtn_}) {
        if (w) w->setEnabled(false);
    }

    // Kick the first frame on the event loop so the UI paints first.
    QTimer::singleShot(0, this, &RenderDialog_Qt::renderStep);
}

void RenderDialog_Qt::renderStep() {
    if (cancelRequested_) {
        finishRender(true);
        return;
    }
    if (renderCur_ > renderTo_) {
        // Stills done. For video, hand off to the ffmpeg encode pass.
        if (renderIsVideo_) startEncode();
        else                finishRender(false);
        return;
    }

    // Render exactly one frame. renderPlate drives gfcPlate::draw() directly,
    // which issues GL calls needing the viewport context current.
    jefe::qt::RenderParams p = renderParams_;
    p.from = renderCur_;
    p.to   = renderCur_;
    if (renderVp_) renderVp_->makeCurrent();
    const int n = jefe::qt::triggerSyncRender(p);
    if (renderVp_) renderVp_->doneCurrent();

    renderDone_ += n;
    ++renderCur_;

    // Video renders frames as the first half of the job; show ~0–50% so the
    // encode pass can fill the rest.
    const double frac = (renderTotal_ > 0) ? double(renderDone_) / renderTotal_ : 0.0;
    const int pct = int((renderIsVideo_ ? frac * 50.0 : frac * 100.0) + 0.5);
    progressBar_->setValue(pct);
    progressBar_->setFormat(
        QString("Rendering frames… %1/%2").arg(renderDone_).arg(renderTotal_));
    statusLabel_->setText(
        QString("Rendering frame %1 of %2…").arg(renderDone_).arg(renderTotal_));

    // Schedule the next frame; the event loop runs in between so the dialog
    // stays responsive and a Cancel click is processed.
    QTimer::singleShot(0, this, &RenderDialog_Qt::renderStep);
}

void RenderDialog_Qt::startEncode() {
    progressBar_->setValue(50);
    progressBar_->setFormat("Encoding video… 0%");
    statusLabel_->setText("Encoding video…");

    videoEncoder_ = new VideoEncoder_Qt(this);
    connect(videoEncoder_, &VideoEncoder_Qt::progress, this,
            [this](int done, int totalFrames) {
        const double frac = (totalFrames > 0) ? double(done) / totalFrames : 0.0;
        progressBar_->setValue(50 + int(frac * 50.0 + 0.5));   // 50–100%
        progressBar_->setFormat(
            QString("Encoding video… %1/%2").arg(done).arg(totalFrames));
        statusLabel_->setText(
            QString("Encoding frame %1 of %2…").arg(done).arg(totalFrames));
    });
    connect(videoEncoder_, &VideoEncoder_Qt::finished, this,
            [this](bool ok, const QString& msg) {
        cleanupVideoTemp();
        const QString out = QFileInfo(videoOutFile_).fileName();
        if (cancelRequested_ || (!ok && msg == "Encoding cancelled.")) {
            progressBar_->setStyleSheet(kBarAmber);
            progressBar_->setFormat("Cancelled");
            statusLabel_->setText("Render cancelled.");
        } else if (!ok) {
            progressBar_->setStyleSheet(kBarAmber);
            progressBar_->setFormat("Encode failed");
            statusLabel_->setText(msg);
        } else {
            progressBar_->setStyleSheet(kBarGreen);
            progressBar_->setValue(100);
            progressBar_->setFormat(QString("Done — %1 ✓").arg(out));
            lastOutputDir_ = QFileInfo(videoOutFile_).absolutePath();
            statusLabel_->setText(folderLink(QString("Wrote %1").arg(out)));
        }
        if (videoEncoder_) { videoEncoder_->deleteLater(); videoEncoder_ = nullptr; }
        rendering_ = false;
        renderVp_ = nullptr;
        renderBtn_->setText("Render");
        renderBtn_->setEnabled(inputsValid());
        doneBtn_->setEnabled(true);
        for (QWidget* w : {(QWidget*)quadrantCombo_, (QWidget*)formatCombo_,
                           (QWidget*)startFrameSpin_, (QWidget*)endFrameSpin_,
                           (QWidget*)pathEdit_, (QWidget*)browseBtn_}) {
            if (w) w->setEnabled(true);
        }
    });

    VideoEncoder_Qt::Params ep;
    ep.framePattern = videoTmpDir_ + "/f_%04d.png";
    ep.startNumber  = renderCur_ - renderTotal_;   // = original from frame
    ep.frameCount   = renderTotal_;
    ep.fps          = videoFpsSpin_->value();
    ep.codec        = codecForFormat(videoFormatIdx_);
    ep.quality      = videoQualitySpin_->value();
    ep.bitrateKbps  = (videoBitrateModeCombo_->currentIndex() == 1)
                          ? videoBitrateSpin_->value() * 1000 : 0;
    ep.preset       = videoPresetCombo_->currentIndex();
    ep.outFile      = videoOutFile_;
    videoEncoder_->start(ep);
}

void RenderDialog_Qt::cleanupVideoTemp() {
    if (!videoTmpDir_.isEmpty()) {
        QDir(videoTmpDir_).removeRecursively();
        videoTmpDir_.clear();
    }
}

void RenderDialog_Qt::finishRender(bool cancelled) {
    rendering_ = false;
    renderVp_ = nullptr;
    if (renderIsVideo_) cleanupVideoTemp();
    renderBtn_->setText("Render");
    renderBtn_->setEnabled(inputsValid());
    doneBtn_->setEnabled(true);
    for (QWidget* w : {(QWidget*)quadrantCombo_, (QWidget*)formatCombo_,
                       (QWidget*)startFrameSpin_, (QWidget*)endFrameSpin_,
                       (QWidget*)pathEdit_, (QWidget*)browseBtn_}) {
        if (w) w->setEnabled(true);
    }

    if (cancelled) {
        progressBar_->setStyleSheet(kBarAmber);
        progressBar_->setFormat(
            QString("Cancelled — %1 frame(s)").arg(renderDone_));
        statusLabel_->setText(
            QString("Render cancelled after %1 frame(s)").arg(renderDone_));
    } else {
        progressBar_->setStyleSheet(kBarGreen);
        progressBar_->setValue(100);
        progressBar_->setFormat(QString("Done — %1 frame(s) ✓").arg(renderDone_));
        lastOutputDir_ = pathEdit_->text();
        statusLabel_->setText(
            folderLink(QString("Rendered %1 frame(s)").arg(renderDone_)));
    }
}

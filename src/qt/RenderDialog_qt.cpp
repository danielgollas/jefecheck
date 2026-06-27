#include "RenderDialog_qt.h"
#include "GlViewport_qt.h"
#include "MainWindow_qt.h"
#include "SequenceLoadBridge_qt.h"
#include "VideoEncoder_qt.h"

#include <QApplication>
#include <QComboBox>
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
        l->addWidget(new QLabel("FPS:", vpage));
        l->addWidget(videoFpsSpin_);
        l->addWidget(new QLabel("Quality:", vpage));
        l->addWidget(videoQualitySpin_);
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
    p.scale   = static_cast<float>(scaleSpin_->value());
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
    renderParams_.scale   = static_cast<float>(scaleSpin_->value());
    renderParams_.jpegQuality     = jpegQualitySpin_->value();
    renderParams_.pngQuality      = pngLevelSpin_->value();
    renderParams_.tiffCompression = tiffCompCombo_->currentIndex();
    renderParams_.exrCompression  = exrCompCombo_->currentIndex();
    renderParams_.exrFormat       = exrDepthCombo_->currentIndex();

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
            statusLabel_->setText(QString("Wrote %1").arg(out));
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
        statusLabel_->setText(QString("Rendered %1 frame(s)").arg(renderDone_));
    }
}

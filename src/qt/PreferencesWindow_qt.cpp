#include "PreferencesWindow_qt.h"

#include "../gfcStructures.h"
#include "../gfcTextRenderer.h"
#include "../UIConstants.h"
#include "qt_prefs_persist.h"

#include <QCheckBox>
#include <QTimer>
#include <QColorDialog>

#include <functional>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

extern gfcSettings sett;

PreferencesWindow_Qt::PreferencesWindow_Qt(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Preferences");
    setObjectName("preferences.dialog");
    setStyleSheet(R"(
        QLabel[role="section"] { color:#9a9a9a; font-size:11px; font-weight:600; }
        QWidget[card="true"]   { background:#232327; border:1px solid #333; border-radius:8px; }
        QPushButton[accent="true"] { border-color:#4c6577; color:#a6c0d2; font-weight:600; }
    )");
    setAccessibleName("Preferences");
    setModal(true);
    resize(640, 480);

    // Snapshot the live settings on open so Cancel can restore them verbatim
    // — pages below mutate `sett` directly as the user edits.
    sett_backup_ = std::make_unique<gfcSettings>(sett);

    sidebar_ = new QListWidget(this);
    sidebar_->setFixedWidth(140);
    sidebar_->setObjectName("preferences.sidebar");
    sidebar_->setAccessibleName("Section list");
    pages_ = new QStackedWidget(this);
    pages_->setObjectName("preferences.pages");

    // The stacked widget only changes pages when the sidebar selection
    // does — Qt gives us currentRowChanged for free.
    connect(sidebar_, &QListWidget::currentRowChanged,
            pages_, &QStackedWidget::setCurrentIndex);

    // Sidebar order mirrors page-build order below. Playback & Engine controls
    // are folded into the General page (no separate section).
    buildGeneralPage();
    buildTextPage();
    buildFormatsPage();
    buildSearchPathsPage();
    buildRemotePage();

    sidebar_->setCurrentRow(0);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("preferences.buttons");
    buttons->button(QDialogButtonBox::Save)->setText("Done");
    buttons->button(QDialogButtonBox::Save)->setObjectName("preferences.done.button");
    buttons->button(QDialogButtonBox::Cancel)->setObjectName("preferences.cancel.button");
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        writeTextPrefs();                // Text/* (deferred persistence — see header)
        jefe::qt::writePreferences();   // real persistence (was a no-op)
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, [this]() {
        sett = *sett_backup_;           // revert live mutations
        jefe::qt::applyTextPrefs();     // revert live text-renderer edits (Text/*
                                         // QSettings are untouched since Text is
                                         // deferred-persistence, so this reapplies
                                         // the pre-dialog state)
        emit viewportRepaintRequested(); // paint the reverted state before closing
        reject();
    });

    // Live preview: while this (modal) dialog is open, repaint the viewport at
    // ~30 Hz so text style / background color / checkerboard / aspect-bar changes
    // show in real time. A modal exec() still processes the parent's paint
    // events, so the owner's connected viewport->update() runs. The timer is
    // parented to `this`, so it stops and is destroyed with the dialog.
    liveTimer_ = new QTimer(this);
    liveTimer_->setInterval(33);
    connect(liveTimer_, &QTimer::timeout, this,
            [this]() { emit viewportRepaintRequested(); });
    liveTimer_->start();

    auto* split = new QHBoxLayout();
    split->setContentsMargins(0, 0, 0, 0);
    split->addWidget(sidebar_);
    split->addWidget(pages_, /*stretch*/ 1);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->addLayout(split, /*stretch*/ 1);
    outer->addWidget(buttons);
}

// Out-of-line so the unique_ptr<gfcSettings> destructor is instantiated here,
// where gfcStructures.h (included above) provides the complete type — the
// header only forward-declares gfcSettings (see PreferencesWindow_qt.h).
PreferencesWindow_Qt::~PreferencesWindow_Qt() = default;

void PreferencesWindow_Qt::addPage(const QString& title, QWidget* page) {
    sidebar_->addItem(title);
    pages_->addWidget(page);
}

namespace {
// The viewport background is a full RGB color (sett.bgColorR/G/B). bgColor is
// kept as its luminance for legacy readers.
QColor bgColorToQ() {
    return QColor::fromRgbF(std::clamp(sett.bgColorR, 0.0f, 1.0f),
                            std::clamp(sett.bgColorG, 0.0f, 1.0f),
                            std::clamp(sett.bgColorB, 0.0f, 1.0f));
}
void qToBgColor(const QColor& c) {
    sett.bgColorR = float(c.redF());
    sett.bgColorG = float(c.greenF());
    sett.bgColorB = float(c.blueF());
    sett.bgColor  = (sett.bgColorR + sett.bgColorG + sett.bgColorB) / 3.0f;
}

// Style a small button as a pure color swatch (no text) showing `c`.
void styleSwatch(QPushButton* btn, const QColor& c) {
    btn->setText(QString());
    btn->setStyleSheet(QString("background:%1; border:1px solid #555; border-radius:4px;")
                           .arg(c.name()));
}

// Runs a color dialog that applies each intermediate color live (via `apply`)
// so the viewport previews in real time as the user drags. Reverts to `start`
// if the dialog is cancelled. Returns the accepted color (or `start`).
QColor liveColorDialog(QWidget* parent, const QColor& start, const QString& title,
                       bool withAlpha, const std::function<void(const QColor&)>& apply) {
    QColorDialog dlg(start, parent);
    dlg.setWindowTitle(title);
    dlg.setOption(QColorDialog::ShowAlphaChannel, withAlpha);
    QObject::connect(&dlg, &QColorDialog::currentColorChanged, parent,
                     [&apply](const QColor& c) { if (c.isValid()) apply(c); });
    if (dlg.exec() == QDialog::Accepted && dlg.selectedColor().isValid()) {
        apply(dlg.selectedColor());
        return dlg.selectedColor();
    }
    apply(start);
    return start;
}

// Uniform look for every Preferences page's form: left-aligned labels, compact
// top-left layout, consistent margins/spacing.
void configureForm(QFormLayout* form) {
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setContentsMargins(14, 12, 14, 12);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(8);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
}
}  // namespace

void PreferencesWindow_Qt::buildGeneralPage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    configureForm(form);

    // Background color — a pure color swatch that opens a live RGB picker
    // (the viewport repaint timer previews each intermediate color).
    auto* bgBtn = new QPushButton(page);
    bgBtn->setFixedSize(60, 22);
    bgBtn->setObjectName("preferences.general.bgcolor.button");
    bgBtn->setToolTip(QStringLiteral("The viewport background color, shown behind and around image plates. Opens a color picker with live preview."));
    bgBtn->setAccessibleName("Background color");
    auto applyBg = [bgBtn]() { styleSwatch(bgBtn, bgColorToQ()); };
    applyBg();
    connect(bgBtn, &QPushButton::clicked, page, [page, applyBg]() {
        liveColorDialog(page, bgColorToQ(), "Background color", /*withAlpha*/ false,
                        [applyBg](const QColor& c) { qToBgColor(c); applyBg(); });
    });
    form->addRow("Background color", bgBtn);

    auto* checker = new QCheckBox("Checkerboard background", page);
    checker->setChecked(sett.bgCheckerboard != 0);
    checker->setObjectName("preferences.general.checkerboard.check");
    checker->setToolTip(QStringLiteral("Draw a checkerboard (two shades derived from the background color) instead of a flat fill — useful for judging image edges and alpha."));
    checker->setAccessibleName("Checkerboard background");
    connect(checker, &QCheckBox::toggled, page, [](bool on){ sett.bgCheckerboard = on ? 1 : 0; });
    form->addRow(checker);

    auto* browseRow = new QWidget(page);
    auto* browseLay = new QHBoxLayout(browseRow);
    browseLay->setContentsMargins(0, 0, 0, 0);
    browseLay->setSpacing(6);
    auto* browsePath = new QLineEdit(QString::fromStdString(sett.defaultBrowsePath), browseRow);
    browsePath->setObjectName("preferences.general.browsepath.edit");
    browsePath->setToolTip(QStringLiteral("The default folder that file dialogs open to when there is no more-recent location."));
    browsePath->setAccessibleName("Default browse path");
    browsePath->setMinimumWidth(280);
    connect(browsePath, &QLineEdit::editingFinished, page, [browsePath]() {
        sett.defaultBrowsePath = browsePath->text().toStdString();
    });
    auto* browseBtn = new QPushButton("Browse…", browseRow);
    browseBtn->setObjectName("preferences.general.browsepath.button");
    browseBtn->setToolTip(QStringLiteral("Browse for the default folder that file dialogs open to."));
    browseBtn->setAccessibleName("Choose default browse path");
    connect(browseBtn, &QPushButton::clicked, page, [browsePath]() {
        const QString start = browsePath->text().isEmpty()
            ? QString::fromStdString(sett.defaultBrowsePath) : browsePath->text();
        const QString dir = QFileDialog::getExistingDirectory(
            browsePath->window(), "Default browse path", start);
        if (!dir.isEmpty()) {
            browsePath->setText(dir);
            sett.defaultBrowsePath = dir.toStdString();
        }
    });
    browseLay->addWidget(browsePath, /*stretch*/ 1);
    browseLay->addWidget(browseBtn);
    form->addRow("Default browse path", browseRow);

    auto* fullscreen = new QCheckBox("Start in fullscreen", page);
    fullscreen->setChecked(sett.startFullscreen != 0);
    fullscreen->setObjectName("preferences.general.fullscreen.check");
    fullscreen->setToolTip(QStringLiteral("Open the main window in fullscreen when the application starts."));
    fullscreen->setAccessibleName("Start in fullscreen");
    connect(fullscreen, &QCheckBox::toggled, page,
            [](bool on) { sett.startFullscreen = on ? 1 : 0; });
    form->addRow(fullscreen);

    auto* showLoad = new QCheckBox("Open Load window at startup", page);
    showLoad->setChecked(sett.openLoadWindowAtStartup != 0);
    showLoad->setObjectName("preferences.general.openloadatstart.check");
    showLoad->setToolTip(QStringLiteral("Automatically open the Load window when the application starts."));
    showLoad->setAccessibleName("Open Load window at startup");
    connect(showLoad, &QCheckBox::toggled, page,
            [](bool on) { sett.openLoadWindowAtStartup = on ? 1 : 0; });
    form->addRow(showLoad);

    auto* recovery = new QCheckBox("Enable crash recovery session", page);
    recovery->setChecked(sett.enableCrashRecoverySession != 0);
    recovery->setObjectName("preferences.general.recovery.check");
    recovery->setToolTip(QStringLiteral("Keep a crash-recovery snapshot of the current session so it can be restored after an unexpected exit."));
    recovery->setAccessibleName("Enable crash recovery session");
    connect(recovery, &QCheckBox::toggled, page,
            [](bool on) { sett.enableCrashRecoverySession = on ? 1 : 0; });
    form->addRow(recovery);

    // On launch: what to do with the previous session. Persisted under
    // Session/startupBehavior; the MainWindow constructor seeds sett from it.
    auto* startup = new QComboBox(page);
    startup->addItem("Start empty");          // 0
    startup->addItem("Reopen last session");  // 1
    startup->addItem("Ask");                  // 2
    startup->setObjectName("preferences.general.startup.combo");
    startup->setToolTip(QStringLiteral("What to do with the previous session on launch: start empty, reopen the last session, or ask each time."));
    startup->setAccessibleName("On launch session behavior");
    startup->setCurrentIndex(sett.startupSessionBehavior);
    connect(startup, QOverload<int>::of(&QComboBox::currentIndexChanged),
            page, [](int idx) {
        sett.startupSessionBehavior = idx;
    });
    form->addRow("On launch", startup);

    auto* aspect = new QDoubleSpinBox(page);
    aspect->setRange(0.0, 1.0);
    aspect->setSingleStep(0.05);
    aspect->setValue(sett.aspectBarsOpacity);
    aspect->setObjectName("preferences.general.aspectopacity.spin");
    aspect->setToolTip(QStringLiteral("Opacity of the letterbox/pillarbox aspect-ratio bars drawn over the image (0 = invisible, 1 = solid)."));
    aspect->setAccessibleName("Aspect-bar opacity");
    connect(aspect, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            page, [](double v) { sett.aspectBarsOpacity = float(v); });
    form->addRow("Aspect-bar opacity", aspect);

    auto* thumbs = new QCheckBox("Timeline thumbnails", page);
    thumbs->setChecked(sett.showThumbnails);
    thumbs->setObjectName("preferences.general.thumbnails.check");
    thumbs->setToolTip(QStringLiteral("Show filmstrip thumbnails in the timeline."));
    thumbs->setAccessibleName("Timeline thumbnails");
    connect(thumbs, &QCheckBox::toggled, page, [](bool on){ sett.showThumbnails = on; });
    form->addRow(thumbs);

    auto* fbSize = new QSpinBox(page);
    fbSize->setRange(6, 72);
    fbSize->setValue(sett.feedbackMessageSize);
    fbSize->setObjectName("preferences.general.feedbacksize.spin");
    fbSize->setToolTip(QStringLiteral("Font size of the on-screen feedback messages (load and playback notifications)."));
    fbSize->setAccessibleName("Feedback message size");
    connect(fbSize, QOverload<int>::of(&QSpinBox::valueChanged), page,
            [](int v){ sett.feedbackMessageSize = v; });
    form->addRow("Feedback message size", fbSize);

    auto* fbFade = new QDoubleSpinBox(page);
    fbFade->setRange(0.0, 30.0);
    fbFade->setSingleStep(0.5);
    fbFade->setValue(sett.feedbackMessageFadeDelay);
    fbFade->setObjectName("preferences.general.feedbackfade.spin");
    fbFade->setToolTip(QStringLiteral("How long on-screen feedback messages stay before fading out, in seconds."));
    fbFade->setAccessibleName("Feedback message fade delay");
    connect(fbFade, QOverload<double>::of(&QDoubleSpinBox::valueChanged), page,
            [](double v){ sett.feedbackMessageFadeDelay = float(v); });
    form->addRow("Feedback fade delay (s)", fbFade);

    // ---- Playback & Engine (folded into General) -----------------------------
    // renderingEngine, vsync, numOfPartitions and forcePBO are intentionally not
    // exposed. vsync/renderingEngine are genuinely inert (only a hardcoded swap
    // interval / commented-out reads). numOfPartitions/forcePBO ARE read live in
    // gfcSequence.cpp, but only gate a partitioned-PBO upload path that is
    // half-implemented (a //TODO stub + a redundant glDeleteTextures loop), so
    // enabling them takes an unfinished path rather than a working alternative.
    // Pinned to their safe ctor defaults (1 / 0); revisit if the PBO path lands.
    auto* engineHeader = new QLabel("Playback & Engine", page);
    engineHeader->setProperty("role", "section");
    form->addRow(engineHeader);

    auto* queue = new QSpinBox(page);
    queue->setRange(0, 32);
    queue->setValue(sett.maximumFramesInQueue);
    queue->setObjectName("preferences.engine.queue.spin");
    queue->setAccessibleName("Max frames in raw queue");
    const QString queueTip =
        "How many decoded frames each track may read ahead of playback and hold "
        "in RAM before its loader pauses.\n\n"
        "This keeps multiple sequences aligned by frame position: a track with "
        "small, fast-loading frames can't race ahead and fill memory while a "
        "track with large frames lags — it fills its queue and waits. Lower "
        "values use less RAM; higher values buffer further ahead for smoother "
        "playback of slow sources.";
    queue->setToolTip(queueTip);
    connect(queue, QOverload<int>::of(&QSpinBox::valueChanged),
            page, [](int v) { sett.maximumFramesInQueue = v; });
    auto* queueLabel = new QLabel("Frames to read ahead", page);
    queueLabel->setToolTip(queueTip);
    form->addRow(queueLabel, queue);

    auto* oiioThreads = new QSpinBox(page);
    oiioThreads->setRange(0, 64);
    oiioThreads->setValue(sett.oiioThreads);
    oiioThreads->setSpecialValueText("auto");   // 0 shows as "auto"
    oiioThreads->setObjectName("preferences.engine.oiiothreads.spin");
    oiioThreads->setAccessibleName("OIIO decode threads");
    const QString oiioTip =
        "Size of OpenImageIO's internal worker-thread pool (0 = auto / all cores), "
        "used for image resize and some decoders (e.g. EXR decompression).\n\n"
        "This is separate from — and stacks on top of — JefeCheck's per-track loader "
        "threads (one per active track). A high value speeds up loading a single "
        "sequence, but can oversubscribe the CPU when several tracks load at once. "
        "If you routinely load many tracks together, try a low value (1–2); for a "
        "single heavy sequence, leave it on auto.";
    oiioThreads->setToolTip(oiioTip);
    connect(oiioThreads, QOverload<int>::of(&QSpinBox::valueChanged),
            page, [](int v) { sett.oiioThreads = v; });
    auto* oiioThreadsLabel = new QLabel("OIIO decode threads", page);
    oiioThreadsLabel->setToolTip(oiioTip);
    form->addRow(oiioThreadsLabel, oiioThreads);

    // Default decode filter — shared by all tracks via the OIIO loader's
    // Filter2D resize path. In-memory only; persisted centrally on Done.
    auto* filterCombo = new QComboBox(page);
    filterCombo->setObjectName("preferences.engine.decodefilter.combo");
    filterCombo->setToolTip(QStringLiteral("Resampling filter used when a sequence is loaded at a reduced decode scale. nearest = fastest and blockiest; lanczos3 = sharpest and slowest."));
    filterCombo->setAccessibleName("Default decode filter");
    filterCombo->addItem("nearest",   FILTERBOX_ID);
    filterCombo->addItem("triangle",  FILTERTRIANGLE_ID);
    filterCombo->addItem("mitchell",  FILTERMITCHELL_ID);
    filterCombo->addItem("lanczos3",  FILTERLANCZOS_ID);
    int fidx = filterCombo->findData(sett.defaultDecodeFilter);
    if (fidx < 0) fidx = filterCombo->findData(FILTERLANCZOS_ID);
    filterCombo->setCurrentIndex(fidx);
    connect(filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [filterCombo](int i) {
        sett.defaultDecodeFilter = filterCombo->itemData(i).toInt();
    });
    form->addRow("Default decode filter", filterCombo);

    // Default bit depth for new loads (Quick Load / drag-drop). Existing plates
    // keep their depth until reloaded. In-memory only; persisted on Done.
    auto* depthCombo = new QComboBox(page);
    depthCombo->setObjectName("preferences.engine.bitdepth.combo");
    depthCombo->setToolTip(QStringLiteral("Texture bit depth for newly loaded sequences (drag-drop / Quick Load). Higher depth keeps more tonal precision but uses more memory. Existing plates keep their depth until reloaded."));
    depthCombo->setAccessibleName("Default bit depth for new loads");
    depthCombo->addItem("8",        GFC_8BPC);
    depthCombo->addItem("16",       GFC_16BPC);
    depthCombo->addItem("16-half",  GFC_16HALF);
    depthCombo->addItem("32-float", GFC_4BPC);
    int didx = depthCombo->findData(sett.defaultTextureFormat);
    if (didx < 0) didx = depthCombo->findData(GFC_16HALF);
    depthCombo->setCurrentIndex(didx);
    connect(depthCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [depthCombo](int i) {
        sett.defaultTextureFormat = depthCombo->itemData(i).toInt();
    });
    form->addRow("Default bit depth", depthCombo);

    addPage("General", page);
}

namespace {
QColor floatsToQColor(float r, float g, float b, float a) {
    const auto c01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
    return QColor::fromRgbF(c01(r), c01(g), c01(b), c01(a));
}
}  // namespace

// Text page binds to the GfcTextRenderer singleton (declared in
// gfcTextRenderer.h), not `sett` — there is no gfcSettings storage for text
// prefs. Unlike the rest of this dialog, edits here call ONLY the renderer's
// setters for live preview; QSettings ("Text/*") is written once on Done
// (writeTextPrefs(), below) and reapplied via jefe::qt::applyTextPrefs() on
// Cancel, matching the deferred-persistence pattern the Engine combos use
// (see qt_prefs_persist.cpp and developer_notes.md).
void PreferencesWindow_Qt::buildTextPage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    configureForm(form);

    // Seed every control from persisted Text/* QSettings, falling back to the
    // shared GfcTextDefaults (gfcTextRenderer.h) so a first run (no keys yet)
    // matches the renderer's live state and applyTextPrefs exactly.
    using namespace GfcTextDefaults;
    QSettings s;
    const float initSize          = s.value("Text/size", kLabelSize).toFloat();
    textColor_ = floatsToQColor(s.value("Text/colorR", kColorR).toFloat(),
                                 s.value("Text/colorG", kColorG).toFloat(),
                                 s.value("Text/colorB", kColorB).toFloat(),
                                 1.0f);   // text label color has no alpha
    const int initHintMode        = s.value("Text/hintMode", kHintMode).toInt();
    const bool initFilterNearest  = s.value("Text/filterNearest", kFilterNearest).toBool();
    const float initGamma         = s.value("Text/gamma", kGamma).toFloat();
    const bool initShadowEnabled  = s.value("Text/shadowEnabled", kShadowEnabled).toBool();
    const float initShadowOffX    = s.value("Text/shadowOffX", kShadowOffX).toFloat();
    const float initShadowOffY    = s.value("Text/shadowOffY", kShadowOffY).toFloat();
    const float initShadowBlur    = s.value("Text/shadowBlur", kShadowBlur).toFloat();
    textShadowColor_ = floatsToQColor(s.value("Text/shadowColorR", kShadowColorR).toFloat(),
                                       s.value("Text/shadowColorG", kShadowColorG).toFloat(),
                                       s.value("Text/shadowColorB", kShadowColorB).toFloat(),
                                       s.value("Text/shadowColorA", kShadowColorA).toFloat());

    // Size.
    textSizeSpin_ = new QSpinBox(page);
    textSizeSpin_->setRange(6, 72);
    textSizeSpin_->setValue(qRound(initSize));
    textSizeSpin_->setObjectName("preferences.text.size.spin");
    textSizeSpin_->setToolTip(QStringLiteral("Font size (px) of the on-plate label overlay — the filename/info drawn on each plate."));
    textSizeSpin_->setAccessibleName("Text size");
    connect(textSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), page,
            [](int v) { textRenderer().setLabelSize(float(v)); });
    form->addRow("Size", textSizeSpin_);

    // Color.
    textColorBtn_ = new QPushButton(page);
    textColorBtn_->setFixedSize(60, 22);
    textColorBtn_->setObjectName("preferences.text.color.button");
    textColorBtn_->setToolTip(QStringLiteral("Color of the on-plate label text. Live preview."));
    textColorBtn_->setAccessibleName("Text color");
    auto applyTextColorSwatch = [this]() { styleSwatch(textColorBtn_, textColor_); };
    applyTextColorSwatch();
    connect(textColorBtn_, &QPushButton::clicked, page, [this, page, applyTextColorSwatch]() {
        textColor_ = liveColorDialog(page, textColor_, "Text color", /*withAlpha*/ false,
            [this, applyTextColorSwatch](const QColor& c) {
                textColor_ = c;
                textRenderer().setLabelColor(float(c.redF()), float(c.greenF()), float(c.blueF()));
                applyTextColorSwatch();
            });
    });
    form->addRow("Color", textColorBtn_);

    // Hint mode.
    textHintCombo_ = new QComboBox(page);
    textHintCombo_->addItem("Light",          int(GfcTextRenderer::HINT_LIGHT));
    textHintCombo_->addItem("Normal",         int(GfcTextRenderer::HINT_NORMAL));
    textHintCombo_->addItem("Force autohint", int(GfcTextRenderer::HINT_AUTO));
    textHintCombo_->setObjectName("preferences.text.hint.combo");
    textHintCombo_->setToolTip(QStringLiteral("FreeType hinting for glyphs. Light = smooth (best for diagonals); Normal = crisper stems; Force autohint = FreeType's own hinter."));
    textHintCombo_->setAccessibleName("Text hinting");
    {
        int idx = textHintCombo_->findData(initHintMode);
        if (idx < 0) idx = textHintCombo_->findData(int(GfcTextRenderer::HINT_LIGHT));
        textHintCombo_->setCurrentIndex(idx);
    }
    connect(textHintCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            page, [this](int i) {
        textRenderer().setHintMode(
            static_cast<GfcTextRenderer::HintMode>(textHintCombo_->itemData(i).toInt()));
    });
    form->addRow("Hinting", textHintCombo_);

    // Filter — atlas texture sampling. true=GL_NEAREST (pixel-exact, default).
    textFilterCombo_ = new QComboBox(page);
    textFilterCombo_->addItem("Nearest (pixel-exact)", true);
    textFilterCombo_->addItem("Linear",                false);
    textFilterCombo_->setObjectName("preferences.text.filter.combo");
    textFilterCombo_->setToolTip(QStringLiteral("Texture filtering for the glyph atlas. Nearest = pixel-exact and crisp; Linear = smoother when scaled."));
    textFilterCombo_->setAccessibleName("Text atlas filter");
    {
        int idx = textFilterCombo_->findData(initFilterNearest);
        if (idx < 0) idx = 0;
        textFilterCombo_->setCurrentIndex(idx);
    }
    connect(textFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            page, [this](int i) {
        textRenderer().setFilterNearest(textFilterCombo_->itemData(i).toBool());
    });
    form->addRow("Filter", textFilterCombo_);

    // Gamma — atlas coverage correction (0.5-1.0).
    textGammaSpin_ = new QDoubleSpinBox(page);
    textGammaSpin_->setRange(0.5, 1.0);
    textGammaSpin_->setSingleStep(0.05);
    textGammaSpin_->setValue(initGamma);
    textGammaSpin_->setObjectName("preferences.text.gamma.spin");
    textGammaSpin_->setToolTip(QStringLiteral("Gamma correction applied to glyph edge coverage. Lower values render text bolder and darker at the edges (0.5–1.0)."));
    textGammaSpin_->setAccessibleName("Text gamma");
    connect(textGammaSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), page,
            [](double v) { textRenderer().setGamma(float(v)); });
    form->addRow("Gamma", textGammaSpin_);

    // Shadow.
    textShadowEnabledCheck_ = new QCheckBox("Shadow enabled", page);
    textShadowEnabledCheck_->setChecked(initShadowEnabled);
    textShadowEnabledCheck_->setObjectName("preferences.text.shadowenabled.check");
    textShadowEnabledCheck_->setToolTip(QStringLiteral("Draw a drop shadow behind text so it stays legible over bright images."));
    textShadowEnabledCheck_->setAccessibleName("Text shadow enabled");
    connect(textShadowEnabledCheck_, &QCheckBox::toggled, page,
            [](bool on) { textRenderer().setShadowEnabled(on); });
    form->addRow(textShadowEnabledCheck_);

    textShadowOffXSpin_ = new QDoubleSpinBox(page);
    textShadowOffXSpin_->setRange(-20.0, 20.0);
    textShadowOffXSpin_->setSingleStep(0.5);
    textShadowOffXSpin_->setValue(initShadowOffX);
    textShadowOffXSpin_->setObjectName("preferences.text.shadowoffx.spin");
    textShadowOffXSpin_->setToolTip(QStringLiteral("Horizontal offset of the text shadow, in pixels."));
    textShadowOffXSpin_->setAccessibleName("Text shadow X offset");

    textShadowOffYSpin_ = new QDoubleSpinBox(page);
    textShadowOffYSpin_->setRange(-20.0, 20.0);
    textShadowOffYSpin_->setSingleStep(0.5);
    textShadowOffYSpin_->setValue(initShadowOffY);
    textShadowOffYSpin_->setObjectName("preferences.text.shadowoffy.spin");
    textShadowOffYSpin_->setToolTip(QStringLiteral("Vertical offset of the text shadow, in pixels."));
    textShadowOffYSpin_->setAccessibleName("Text shadow Y offset");

    auto applyShadowOffset = [this](double) {
        textRenderer().setShadowOffset(float(textShadowOffXSpin_->value()),
                                        float(textShadowOffYSpin_->value()));
    };
    connect(textShadowOffXSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            page, applyShadowOffset);
    connect(textShadowOffYSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            page, applyShadowOffset);
    form->addRow("Shadow offset X", textShadowOffXSpin_);
    form->addRow("Shadow offset Y", textShadowOffYSpin_);

    textShadowBlurSpin_ = new QDoubleSpinBox(page);
    textShadowBlurSpin_->setRange(0.0, 10.0);
    textShadowBlurSpin_->setSingleStep(0.5);
    textShadowBlurSpin_->setValue(initShadowBlur);
    textShadowBlurSpin_->setObjectName("preferences.text.shadowblur.spin");
    textShadowBlurSpin_->setToolTip(QStringLiteral("Blur radius of the text shadow, in pixels (0 = hard shadow)."));
    textShadowBlurSpin_->setAccessibleName("Text shadow blur radius");
    connect(textShadowBlurSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            page, [](double v) { textRenderer().setShadowBlur(float(v)); });
    form->addRow("Shadow blur radius", textShadowBlurSpin_);

    textShadowColorBtn_ = new QPushButton(page);
    textShadowColorBtn_->setFixedSize(60, 22);
    textShadowColorBtn_->setObjectName("preferences.text.shadowcolor.button");
    textShadowColorBtn_->setToolTip(QStringLiteral("Color and opacity of the text shadow. Live preview."));
    textShadowColorBtn_->setAccessibleName("Text shadow color");
    auto applyShadowColorSwatch = [this]() { styleSwatch(textShadowColorBtn_, textShadowColor_); };
    applyShadowColorSwatch();
    connect(textShadowColorBtn_, &QPushButton::clicked, page,
            [this, page, applyShadowColorSwatch]() {
        textShadowColor_ = liveColorDialog(page, textShadowColor_, "Text shadow color",
            /*withAlpha*/ true, [this, applyShadowColorSwatch](const QColor& c) {
                textShadowColor_ = c;
                textRenderer().setShadowColor(float(c.redF()), float(c.greenF()),
                                              float(c.blueF()), float(c.alphaF()));
                applyShadowColorSwatch();
            });
    });
    form->addRow("Shadow color", textShadowColorBtn_);

    addPage("Text", page);
}

// Reads the Text page widgets' current values and persists them to
// `Text/*` QSettings — called from the Done handler alongside
// jefe::qt::writePreferences(). Text prefs are deferred-write (see the
// buildTextPage() comment above): live edits only touch the renderer, so
// this is the single point where they land in QSettings.
void PreferencesWindow_Qt::writeTextPrefs() {
    QSettings s;
    s.setValue("Text/size", textSizeSpin_->value());
    s.setValue("Text/colorR", textColor_.redF());
    s.setValue("Text/colorG", textColor_.greenF());
    s.setValue("Text/colorB", textColor_.blueF());
    s.setValue("Text/hintMode", textHintCombo_->currentData().toInt());
    s.setValue("Text/filterNearest", textFilterCombo_->currentData().toBool());
    s.setValue("Text/gamma", textGammaSpin_->value());
    s.setValue("Text/shadowEnabled", textShadowEnabledCheck_->isChecked());
    s.setValue("Text/shadowOffX", textShadowOffXSpin_->value());
    s.setValue("Text/shadowOffY", textShadowOffYSpin_->value());
    s.setValue("Text/shadowBlur", textShadowBlurSpin_->value());
    s.setValue("Text/shadowColorR", textShadowColor_.redF());
    s.setValue("Text/shadowColorG", textShadowColor_.greenF());
    s.setValue("Text/shadowColorB", textShadowColor_.blueF());
    s.setValue("Text/shadowColorA", textShadowColor_.alphaF());
}

void PreferencesWindow_Qt::buildFormatsPage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    configureForm(form);

    auto* exrIgnoreDisplay = new QCheckBox("EXR: ignore display window", page);
    exrIgnoreDisplay->setChecked(sett.exrIgnoreDisplayWindow != 0);
    exrIgnoreDisplay->setObjectName("preferences.formats.exrignoredisplay.check");
    exrIgnoreDisplay->setToolTip(QStringLiteral("For EXR files, ignore the header display window and show only the data (pixel) window instead of compositing it into the full frame."));
    exrIgnoreDisplay->setAccessibleName("EXR: ignore display window");
    connect(exrIgnoreDisplay, &QCheckBox::toggled, page,
            [](bool on) { sett.exrIgnoreDisplayWindow = on ? 1 : 0; });
    form->addRow(exrIgnoreDisplay);

    auto* exrIgnoreAspect = new QCheckBox("EXR: ignore header aspect ratio", page);
    exrIgnoreAspect->setChecked(sett.exrIgnoreHeadersAspectRatio != 0);
    exrIgnoreAspect->setObjectName("preferences.formats.exrignoreaspect.check");
    exrIgnoreAspect->setToolTip(QStringLiteral("For EXR files, ignore the header pixel aspect ratio (do not stretch to correct for non-square pixels)."));
    exrIgnoreAspect->setAccessibleName("EXR: ignore header aspect ratio");
    connect(exrIgnoreAspect, &QCheckBox::toggled, page,
            [](bool on) { sett.exrIgnoreHeadersAspectRatio = on ? 1 : 0; });
    form->addRow(exrIgnoreAspect);

    auto* straightAlpha = new QCheckBox("Read straight (unassociated) alpha", page);
    straightAlpha->setChecked(sett.oiioUnassociatedAlpha != 0);
    straightAlpha->setObjectName("preferences.formats.straightalpha.check");
    straightAlpha->setToolTip(QStringLiteral("Read non-premultiplied (straight) alpha instead of OIIO's default associated/premultiplied alpha. Match this to how your images were authored for correct compositing. Takes effect on next load."));
    straightAlpha->setAccessibleName("Read straight (unassociated) alpha");
    connect(straightAlpha, &QCheckBox::toggled, page,
            [](bool on) { sett.oiioUnassociatedAlpha = on ? 1 : 0; });
    form->addRow(straightAlpha);

    auto* applyOrient = new QCheckBox("Apply embedded orientation", page);
    applyOrient->setChecked(sett.applyExifOrientation != 0);
    applyOrient->setObjectName("preferences.formats.applyorientation.check");
    applyOrient->setToolTip(QStringLiteral("Rotate/flip stills to match the file's EXIF/TIFF Orientation metadata so they display upright (JPEG/TIFF). Off by default; irrelevant to rendered EXR/DPX sequences. Takes effect on next load."));
    applyOrient->setAccessibleName("Apply embedded orientation");
    connect(applyOrient, &QCheckBox::toggled, page,
            [](bool on) { sett.applyExifOrientation = on ? 1 : 0; });
    form->addRow(applyOrient);

    addPage("Formats", page);
}

// Consumer: gfcSequence.cpp:114/:1660 call findFileInSearchPaths(...) when
// !fileExists(...) && sett.useSearchPaths — that function (gfcStructures.cpp:944+)
// iterates sett.searchPaths and honors sett.searchPathsRecursive via
// findFileInPath. Wiring here is UI-only: bind the checkboxes/list to `sett`
// and persist; no consumer changes needed.
void PreferencesWindow_Qt::buildSearchPathsPage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    configureForm(form);

    auto* enable = new QCheckBox("Use search paths", page);
    enable->setChecked(sett.useSearchPaths);
    enable->setObjectName("preferences.search.enable.check");
    enable->setToolTip(QStringLiteral("When a sequence file is not found at its original path, look for it in the search paths below."));
    connect(enable, &QCheckBox::toggled, page, [](bool on){ sett.useSearchPaths = on; });
    form->addRow(enable);

    auto* recursive = new QCheckBox("Search recursively", page);
    recursive->setChecked(sett.searchPathsRecursive);
    recursive->setObjectName("preferences.search.recursive.check");
    recursive->setToolTip(QStringLiteral("Search inside subfolders of each search path as well."));
    connect(recursive, &QCheckBox::toggled, page, [](bool on){ sett.searchPathsRecursive = on; });
    form->addRow(recursive);

    auto* list = new QListWidget(page);
    list->setObjectName("preferences.search.paths.list");
    list->setToolTip(QStringLiteral("Folders searched (in order) to relocate missing sequence files."));
    for (const auto& p : sett.searchPaths) list->addItem(QString::fromStdString(p));
    form->addRow(list);

    auto* row = new QHBoxLayout();
    auto* add = new QPushButton("Add…", page);
    add->setObjectName("preferences.search.add.button");
    add->setToolTip(QStringLiteral("Add a folder to the search paths."));
    auto* rem = new QPushButton("Remove", page);
    rem->setObjectName("preferences.search.remove.button");
    rem->setToolTip(QStringLiteral("Remove the selected folder(s) from the search paths."));
    row->addWidget(add); row->addWidget(rem); row->addStretch(1);
    auto* buttonRow = new QWidget(page);
    buttonRow->setLayout(row);
    row->setContentsMargins(0, 0, 0, 0);
    form->addRow(buttonRow);

    auto syncToSett = [list]() {
        sett.searchPaths.clear();
        for (int i = 0; i < list->count(); ++i)
            sett.searchPaths.push_back(list->item(i)->text().toStdString());
    };
    connect(add, &QPushButton::clicked, page, [page, list, syncToSett]() {
        const QString d = QFileDialog::getExistingDirectory(page, "Add search path");
        if (d.isEmpty()) return;
        if (!list->findItems(d, Qt::MatchExactly).isEmpty()) return;  // no duplicates
        list->addItem(d);
        syncToSett();
    });
    connect(rem, &QPushButton::clicked, page, [list, syncToSett]() {
        qDeleteAll(list->selectedItems()); syncToSett();
    });

    addPage("Search Paths", page);
}

namespace {
// remotePointerColor packs (r<<24)|(g<<16)|(b<<8) with the low (alpha) byte
// zero; 0 is a sentinel meaning "unset -> default gray 0.6" (mirrors
// gfcUnpackPointerColor in gfcPlate.cpp and unpackRGB in gfcnetworkmanager.cpp).
QColor pointerColorToQ() {
    if (sett.remotePointerColor == 0) return QColor(153, 153, 153);
    const int r = (sett.remotePointerColor >> 24) & 0xff;
    const int g = (sett.remotePointerColor >> 16) & 0xff;
    const int b = (sett.remotePointerColor >> 8) & 0xff;
    return QColor(r, g, b);
}
void qToPointerColor(const QColor& c) {
    int packed = (c.red() << 24) | (c.green() << 16) | (c.blue() << 8);
    // 0 is the "unset → default gray" sentinel (see pointerColorToQ and
    // gfcPlate's gfcUnpackPointerColor), so a deliberately-picked pure black
    // would render as gray. Nudge it to a near-black that isn't the sentinel.
    if (packed == 0) packed = (1 << 8);   // B=1/255 ≈ black, non-zero
    sett.remotePointerColor = packed;
}
}  // namespace

// Consumers: gfcnetworkclient.cpp / gfcnetworkmanager.cpp read nickName,
// chat*, remotePointerFadeDelay, remotePointerColor, sendRemoteLoadRequests
// and autoAcceptRemoteLoadRequests at connect/render time; gfcPlate.cpp
// unpacks remotePointerColor for the pointer overlay and gfcTrackManager.cpp
// consults the load-request toggles. Wiring here is UI-only: bind widgets to
// `sett` and persist; no consumer changes needed (see task-5-brief.md).
void PreferencesWindow_Qt::buildRemotePage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    configureForm(form);

    auto* nickname = new QLineEdit(QString::fromStdString(sett.nickName), page);
    nickname->setObjectName("preferences.remote.nickname.edit");
    nickname->setToolTip(QStringLiteral("Your display name shown to other participants in a remote session."));
    nickname->setAccessibleName("Nickname");
    connect(nickname, &QLineEdit::editingFinished, page, [nickname]() {
        sett.nickName = nickname->text().toStdString();
    });
    form->addRow("Nickname", nickname);

    // Send / auto-accept remote-load-request toggles removed — loads come only
    // through playlists, so these are not user-configurable.

    // Chat group.
    auto* chatHeader = new QLabel("Chat", page);
    chatHeader->setProperty("role", "section");
    form->addRow(chatHeader);

    auto* chatFade = new QDoubleSpinBox(page);
    chatFade->setRange(0.0, 60.0);
    chatFade->setSingleStep(0.5);
    chatFade->setValue(sett.chatFadeDelay);
    chatFade->setObjectName("preferences.remote.chatfade.spin");
    chatFade->setToolTip(QStringLiteral("How long chat messages stay before fading out, in seconds."));
    chatFade->setAccessibleName("Chat fade delay");
    connect(chatFade, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            page, [](double v) { sett.chatFadeDelay = float(v); });
    form->addRow("Fade delay (s)", chatFade);

    auto* chatAutoFade = new QCheckBox("Auto-fade chat", page);
    chatAutoFade->setChecked(sett.chatAutoFade != 0);
    chatAutoFade->setObjectName("preferences.remote.chatautofade.check");
    chatAutoFade->setToolTip(QStringLiteral("Automatically fade chat messages after the fade delay (off = keep them visible)."));
    chatAutoFade->setAccessibleName("Auto-fade chat");
    connect(chatAutoFade, &QCheckBox::toggled, page,
            [](bool on) { sett.chatAutoFade = on ? 1 : 0; });
    form->addRow(chatAutoFade);

    auto* chatTextBG = new QCheckBox("Chat text background", page);
    chatTextBG->setChecked(sett.chatTextBG != 0);
    chatTextBG->setObjectName("preferences.remote.chattextbg.check");
    chatTextBG->setToolTip(QStringLiteral("Draw a semi-opaque background behind chat text for legibility."));
    chatTextBG->setAccessibleName("Chat text background");
    connect(chatTextBG, &QCheckBox::toggled, page,
            [](bool on) { sett.chatTextBG = on ? 1 : 0; });
    form->addRow(chatTextBG);

    auto* chatFontSize = new QSpinBox(page);
    chatFontSize->setRange(6, 72);
    chatFontSize->setValue(sett.chatFontSize);
    chatFontSize->setObjectName("preferences.remote.chatfontsize.spin");
    chatFontSize->setToolTip(QStringLiteral("Font size of on-screen chat messages."));
    chatFontSize->setAccessibleName("Chat font size");
    connect(chatFontSize, QOverload<int>::of(&QSpinBox::valueChanged),
            page, [](int v) { sett.chatFontSize = v; });
    form->addRow("Font size", chatFontSize);

    auto* chatOpacity = new QDoubleSpinBox(page);
    chatOpacity->setRange(0.0, 1.0);
    chatOpacity->setSingleStep(0.05);
    chatOpacity->setValue(sett.chatOpacity);
    chatOpacity->setObjectName("preferences.remote.chatopacity.spin");
    chatOpacity->setToolTip(QStringLiteral("Opacity of the on-screen chat overlay (0–1)."));
    chatOpacity->setAccessibleName("Chat opacity");
    connect(chatOpacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            page, [](double v) { sett.chatOpacity = float(v); });
    form->addRow("Opacity", chatOpacity);

    auto* chatLines = new QSpinBox(page);
    chatLines->setRange(1, 50);
    chatLines->setValue(sett.chatDisplayLines);
    chatLines->setObjectName("preferences.remote.chatlines.spin");
    chatLines->setToolTip(QStringLiteral("Maximum number of chat lines shown on screen at once."));
    chatLines->setAccessibleName("Chat display lines");
    connect(chatLines, QOverload<int>::of(&QSpinBox::valueChanged),
            page, [](int v) { sett.chatDisplayLines = v; });
    form->addRow("Display lines", chatLines);

    // Remote pointer group.
    auto* pointerHeader = new QLabel("Remote pointer", page);
    pointerHeader->setProperty("role", "section");
    form->addRow(pointerHeader);

    auto* pointerFade = new QDoubleSpinBox(page);
    pointerFade->setRange(0.0, 60.0);
    pointerFade->setSingleStep(0.5);
    pointerFade->setValue(sett.remotePointerFadeDelay);
    pointerFade->setObjectName("preferences.remote.pointerfade.spin");
    pointerFade->setToolTip(QStringLiteral("How long a remote participant's pointer stays visible after they stop moving it, in seconds."));
    pointerFade->setAccessibleName("Remote pointer fade delay");
    connect(pointerFade, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            page, [](double v) { sett.remotePointerFadeDelay = float(v); });
    form->addRow("Fade delay (s)", pointerFade);

    auto* pointerColorBtn = new QPushButton(page);
    pointerColorBtn->setFixedSize(60, 22);
    pointerColorBtn->setObjectName("preferences.remote.pointercolor.button");
    pointerColorBtn->setToolTip(QStringLiteral("The color of your pointer as seen by other participants. Live preview."));
    pointerColorBtn->setAccessibleName("Remote pointer color");
    auto applyPointerColor = [pointerColorBtn]() { styleSwatch(pointerColorBtn, pointerColorToQ()); };
    applyPointerColor();
    connect(pointerColorBtn, &QPushButton::clicked, page,
            [page, applyPointerColor]() {
        liveColorDialog(page, pointerColorToQ(), "Remote pointer color",
                        /*withAlpha*/ false,
                        [applyPointerColor](const QColor& c) {
            qToPointerColor(c);
            applyPointerColor();
        });
    });
    form->addRow("Color", pointerColorBtn);

    addPage("Remote", page);
}

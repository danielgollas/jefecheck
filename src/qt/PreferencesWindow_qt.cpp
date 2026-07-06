#include "PreferencesWindow_qt.h"

#include "../gfcStructures.h"
#include "../UIConstants.h"
#include "CollapsibleSection_qt.h"
#include "qt_prefs_persist.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
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

    buildGeneralPage();
    buildPlaceholderPage("Text", "Text rendering settings — coming soon.");
    buildEnginePage();
    buildFormatsPage();
    buildPlaceholderPage("Remote", "Remote-session settings — coming soon.");
    buildPlaceholderPage("Paths", "Path / mirror settings — coming soon.");

    sidebar_->setCurrentRow(0);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("preferences.buttons");
    buttons->button(QDialogButtonBox::Save)->setText("Done");
    buttons->button(QDialogButtonBox::Save)->setObjectName("preferences.done.button");
    buttons->button(QDialogButtonBox::Cancel)->setObjectName("preferences.cancel.button");
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        jefe::qt::writePreferences();   // real persistence (was a no-op)
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, [this]() {
        sett = *sett_backup_;           // revert live mutations
        reject();
    });

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

QWidget* PreferencesWindow_Qt::section(const QString& title, QWidget* content) {
    auto* sec = new CollapsibleSection(title, this);
    sec->setContentWidget(content);
    sec->setExpanded(true);
    return sec;
}

namespace {
// FLTK stores bgColor as a 0..1 grayscale (`float bgColor`). Bridge to
// a Qt color picker by reading/writing a QColor with that gray value.
QColor bgColorToQ() {
    const int g = std::clamp(int(std::round(sett.bgColor * 255.0f)), 0, 255);
    return QColor(g, g, g);
}
void qToBgColor(const QColor& c) {
    // Average the channels so picking a tinted color still degrades
    // gracefully into FLTK's gray scalar.
    const float g = (c.redF() + c.greenF() + c.blueF()) / 3.0f;
    sett.bgColor = g;
}
}  // namespace

void PreferencesWindow_Qt::buildGeneralPage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    // Background color — clickable swatch button that pops a color picker.
    auto* bgBtn = new QPushButton(page);
    bgBtn->setFixedSize(60, 22);
    bgBtn->setObjectName("preferences.general.bgcolor.button");
    bgBtn->setAccessibleName("Background color");
    auto applyBg = [bgBtn]() {
        const QColor c = bgColorToQ();
        bgBtn->setStyleSheet(QString("background: %1;").arg(c.name()));
        bgBtn->setText(QString::number(int(sett.bgColor * 255)));
    };
    applyBg();
    connect(bgBtn, &QPushButton::clicked, page, [page, applyBg]() {
        const QColor chosen = QColorDialog::getColor(bgColorToQ(), page,
            "Background color");
        if (chosen.isValid()) {
            qToBgColor(chosen);
            applyBg();
        }
    });
    form->addRow("Background color", bgBtn);

    auto* checker = new QCheckBox("Checkerboard background", page);
    checker->setChecked(sett.bgCheckerboard != 0);
    checker->setObjectName("preferences.general.checkerboard.check");
    checker->setAccessibleName("Checkerboard background");
    connect(checker, &QCheckBox::toggled, page, [](bool on){ sett.bgCheckerboard = on ? 1 : 0; });
    form->addRow(QString(), checker);

    auto* browsePath = new QLineEdit(QString::fromStdString(sett.defaultBrowsePath), page);
    browsePath->setObjectName("preferences.general.browsepath.edit");
    browsePath->setAccessibleName("Default browse path");
    connect(browsePath, &QLineEdit::editingFinished, page, [browsePath]() {
        sett.defaultBrowsePath = browsePath->text().toStdString();
    });
    form->addRow("Default browse path", browsePath);

    auto* fullscreen = new QCheckBox("Start in fullscreen", page);
    fullscreen->setChecked(sett.startFullscreen != 0);
    fullscreen->setObjectName("preferences.general.fullscreen.check");
    fullscreen->setAccessibleName("Start in fullscreen");
    connect(fullscreen, &QCheckBox::toggled, page,
            [](bool on) { sett.startFullscreen = on ? 1 : 0; });
    form->addRow(QString(), fullscreen);

    auto* showLoad = new QCheckBox("Open Load window at startup", page);
    showLoad->setChecked(sett.openLoadWindowAtStartup != 0);
    showLoad->setObjectName("preferences.general.openloadatstart.check");
    showLoad->setAccessibleName("Open Load window at startup");
    connect(showLoad, &QCheckBox::toggled, page,
            [](bool on) { sett.openLoadWindowAtStartup = on ? 1 : 0; });
    form->addRow(QString(), showLoad);

    auto* recovery = new QCheckBox("Enable crash recovery session", page);
    recovery->setChecked(sett.enableCrashRecoverySession != 0);
    recovery->setObjectName("preferences.general.recovery.check");
    recovery->setAccessibleName("Enable crash recovery session");
    connect(recovery, &QCheckBox::toggled, page,
            [](bool on) { sett.enableCrashRecoverySession = on ? 1 : 0; });
    form->addRow(QString(), recovery);

    // On launch: what to do with the previous session. Persisted under
    // Session/startupBehavior; the MainWindow constructor seeds sett from it.
    auto* startup = new QComboBox(page);
    startup->addItem("Start empty");          // 0
    startup->addItem("Reopen last session");  // 1
    startup->addItem("Ask");                  // 2
    startup->setObjectName("prefs.session.startup");
    startup->setAccessibleName("On launch session behavior");
    {
        QSettings s;
        startup->setCurrentIndex(s.value("Session/startupBehavior", 2).toInt());
    }
    connect(startup, QOverload<int>::of(&QComboBox::currentIndexChanged),
            page, [](int idx) {
        sett.startupSessionBehavior = idx;
        QSettings s;
        s.setValue("Session/startupBehavior", idx);
    });
    form->addRow("On launch", startup);

    auto* aspect = new QDoubleSpinBox(page);
    aspect->setRange(0.0, 1.0);
    aspect->setSingleStep(0.05);
    aspect->setValue(sett.aspectBarsOpacity);
    aspect->setObjectName("preferences.general.aspectopacity.spin");
    aspect->setAccessibleName("Aspect-bar opacity");
    connect(aspect, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            page, [](double v) { sett.aspectBarsOpacity = float(v); });
    form->addRow("Aspect-bar opacity", aspect);

    auto* procPri = new QSpinBox(page);
    procPri->setRange(0, 10);
    procPri->setValue(sett.processorPriority);
    procPri->setObjectName("preferences.general.priority.spin");
    procPri->setAccessibleName("Processor priority");
    connect(procPri, QOverload<int>::of(&QSpinBox::valueChanged),
            page, [](int v) { sett.processorPriority = v; });
    form->addRow("Processor priority", procPri);
    // NOTE: processorPriority row's removal is owned by Task 2 (JEF-16) —
    // intentionally left in place here to avoid overlap.

    auto* thumbs = new QCheckBox("Timeline thumbnails", page);
    thumbs->setChecked(sett.showThumbnails);
    thumbs->setObjectName("preferences.general.thumbnails.check");
    thumbs->setAccessibleName("Timeline thumbnails");
    connect(thumbs, &QCheckBox::toggled, page, [](bool on){ sett.showThumbnails = on; });
    form->addRow(QString(), thumbs);

    auto* fbSize = new QSpinBox(page);
    fbSize->setRange(6, 72);
    fbSize->setValue(sett.feedbackMessageSize);
    fbSize->setObjectName("preferences.general.feedbacksize.spin");
    fbSize->setAccessibleName("Feedback message size");
    connect(fbSize, QOverload<int>::of(&QSpinBox::valueChanged), page,
            [](int v){ sett.feedbackMessageSize = v; });
    form->addRow("Feedback message size", fbSize);

    auto* fbFade = new QDoubleSpinBox(page);
    fbFade->setRange(0.0, 30.0);
    fbFade->setSingleStep(0.5);
    fbFade->setValue(sett.feedbackMessageFadeDelay);
    fbFade->setObjectName("preferences.general.feedbackfade.spin");
    fbFade->setAccessibleName("Feedback message fade delay");
    connect(fbFade, QOverload<double>::of(&QDoubleSpinBox::valueChanged), page,
            [](double v){ sett.feedbackMessageFadeDelay = float(v); });
    form->addRow("Feedback fade delay (s)", fbFade);

    addPage("General", page);
}

void PreferencesWindow_Qt::buildEnginePage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    auto* engine = new QComboBox(page);
    engine->addItems({"3D", "2D"});
    engine->setCurrentIndex(sett.renderingEngine == 0 ? 0 : 1);
    engine->setObjectName("preferences.engine.engine.combo");
    engine->setAccessibleName("Rendering engine");
    connect(engine, QOverload<int>::of(&QComboBox::currentIndexChanged),
            page, [](int idx) { sett.renderingEngine = idx; });
    form->addRow("Rendering engine", engine);

    auto* vsync = new QCheckBox("VSync", page);
    vsync->setChecked(sett.vsync != 0);
    vsync->setObjectName("preferences.engine.vsync.check");
    vsync->setAccessibleName("VSync");
    connect(vsync, &QCheckBox::toggled, page,
            [](bool on) { sett.vsync = on ? 1 : 0; });
    form->addRow(QString(), vsync);

    auto* queue = new QSpinBox(page);
    queue->setRange(0, 32);
    queue->setValue(sett.maximumFramesInQueue);
    queue->setObjectName("preferences.engine.queue.spin");
    queue->setAccessibleName("Max frames in raw queue");
    connect(queue, QOverload<int>::of(&QSpinBox::valueChanged),
            page, [](int v) { sett.maximumFramesInQueue = v; });
    form->addRow("Max frames in raw queue", queue);

    auto* partitions = new QSpinBox(page);
    partitions->setRange(1, 16);
    partitions->setValue(sett.numOfPartitions);
    partitions->setObjectName("preferences.engine.partitions.spin");
    partitions->setAccessibleName("Loader partitions");
    connect(partitions, QOverload<int>::of(&QSpinBox::valueChanged),
            page, [](int v) { sett.numOfPartitions = v; });
    form->addRow("Loader partitions", partitions);

    auto* balance = new QCheckBox("Balance read mutex across tracks", page);
    balance->setChecked(sett.balanceReads != 0);
    balance->setObjectName("preferences.engine.balance.check");
    balance->setAccessibleName("Balance read mutex across tracks");
    connect(balance, &QCheckBox::toggled, page,
            [](bool on) { sett.balanceReads = on ? 1 : 0; });
    form->addRow(QString(), balance);

    auto* pbo = new QSpinBox(page);
    pbo->setRange(0, 4);
    pbo->setValue(sett.forcePBO);
    pbo->setObjectName("preferences.engine.pbo.spin");
    pbo->setAccessibleName("Force PBO mode");
    connect(pbo, QOverload<int>::of(&QSpinBox::valueChanged),
            page, [](int v) { sett.forcePBO = v; });
    form->addRow("Force PBO mode", pbo);

    // Default decode filter — shared by all tracks via OIIO loader's
    // Filter2D resize path (see gfcImageLoaderOIIO scale handling).
    // Persisted under Engine/defaultDecodeFilter; restored at startup by
    // MainWindow_Qt next to defaultTextureFormat.
    auto* filterLabel = new QLabel("Default decode filter:", page);
    auto* filterCombo = new QComboBox(page);
    filterCombo->setObjectName("prefs.engine.defaultDecodeFilter");
    filterCombo->setAccessibleName("Default decode filter");
    filterCombo->addItem("nearest",   FILTERBOX_ID);
    filterCombo->addItem("triangle",  FILTERTRIANGLE_ID);
    filterCombo->addItem("mitchell",  FILTERMITCHELL_ID);
    filterCombo->addItem("lanczos3",  FILTERLANCZOS_ID);
    int idx = filterCombo->findData(sett.defaultDecodeFilter);
    if (idx < 0) idx = filterCombo->findData(FILTERLANCZOS_ID);
    filterCombo->setCurrentIndex(idx);
    connect(filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [filterCombo](int i) {
        sett.defaultDecodeFilter = filterCombo->itemData(i).toInt();
        QSettings s;
        s.setValue("Engine/defaultDecodeFilter", sett.defaultDecodeFilter);
    });
    form->addRow(filterLabel, filterCombo);

    // Default bit depth for new loads (moved here from the status bar).
    // Sets the texture format used when a sequence is loaded via Quick Load /
    // drag-drop; existing plates keep their depth until reloaded. The Load
    // Sequence Manager has its own per-track depth controls. GFC_4BPC is a
    // historical misnomer for 32-bit float; GFC_S3TCDX1 is intentionally
    // omitted (storage optimization, not a quality choice). Persisted under
    // Engine/defaultTextureFormat; restored at startup by MainWindow_Qt.
    auto* depthLabel = new QLabel("Default bit depth:", page);
    auto* depthCombo = new QComboBox(page);
    depthCombo->setObjectName("prefs.engine.defaultTextureFormat");
    depthCombo->setAccessibleName("Default bit depth for new loads");
    depthCombo->addItem("8",        GFC_8BPC);
    depthCombo->addItem("16",       GFC_16BPC);
    depthCombo->addItem("16-half",  GFC_16HALF);
    depthCombo->addItem("32-float", GFC_4BPC);
    int depthIdx = depthCombo->findData(sett.defaultTextureFormat);
    if (depthIdx < 0) depthIdx = depthCombo->findData(GFC_16HALF);
    depthCombo->setCurrentIndex(depthIdx);
    connect(depthCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [depthCombo](int i) {
        sett.defaultTextureFormat = depthCombo->itemData(i).toInt();
        QSettings s;
        s.setValue("Engine/defaultTextureFormat", sett.defaultTextureFormat);
    });
    form->addRow(depthLabel, depthCombo);

    addPage("Engine", page);
}

void PreferencesWindow_Qt::buildFormatsPage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    auto* exrIgnoreDisplay = new QCheckBox("EXR: ignore display window", page);
    exrIgnoreDisplay->setChecked(sett.exrIgnoreDisplayWindow != 0);
    exrIgnoreDisplay->setObjectName("preferences.formats.exrignoredisplay.check");
    exrIgnoreDisplay->setAccessibleName("EXR: ignore display window");
    connect(exrIgnoreDisplay, &QCheckBox::toggled, page,
            [](bool on) { sett.exrIgnoreDisplayWindow = on ? 1 : 0; });
    form->addRow(QString(), exrIgnoreDisplay);

    auto* exrIgnoreAspect = new QCheckBox("EXR: ignore header aspect ratio", page);
    exrIgnoreAspect->setChecked(sett.exrIgnoreHeadersAspectRatio != 0);
    exrIgnoreAspect->setObjectName("preferences.formats.exrignoreaspect.check");
    exrIgnoreAspect->setAccessibleName("EXR: ignore header aspect ratio");
    connect(exrIgnoreAspect, &QCheckBox::toggled, page,
            [](bool on) { sett.exrIgnoreHeadersAspectRatio = on ? 1 : 0; });
    form->addRow(QString(), exrIgnoreAspect);

    auto makeEXRSpin = [page](float& field, double min_, double max_, double step,
                              const QString& objectName,
                              const QString& accessibleName) {
        auto* s = new QDoubleSpinBox(page);
        s->setRange(min_, max_);
        s->setSingleStep(step);
        s->setDecimals(3);
        s->setValue(field);
        s->setObjectName(objectName);
        s->setAccessibleName(accessibleName);
        QObject::connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                         page, [&field](double v) { field = float(v); });
        return s;
    };

    form->addRow("EXR exposure", makeEXRSpin(sett.exrExposure, -20.0, 20.0, 0.1,
                                             "preferences.formats.exrexposure.spin",
                                             "EXR exposure"));
    form->addRow("EXR defog",    makeEXRSpin(sett.exrDefog, 0.0, 1.0, 0.01,
                                             "preferences.formats.exrdefog.spin",
                                             "EXR defog"));
    form->addRow("EXR gamma",    makeEXRSpin(sett.exrGamma, 0.1, 5.0, 0.05,
                                             "preferences.formats.exrgamma.spin",
                                             "EXR gamma"));
    form->addRow("EXR knee low", makeEXRSpin(sett.exrKneeLow, -10.0, 10.0, 0.1,
                                             "preferences.formats.exrkneelow.spin",
                                             "EXR knee low"));
    form->addRow("EXR knee high",makeEXRSpin(sett.exrKneeHigh, -10.0, 10.0, 0.1,
                                             "preferences.formats.exrkneehigh.spin",
                                             "EXR knee high"));

    addPage("Formats", page);
}

void PreferencesWindow_Qt::buildPlaceholderPage(const QString& title,
                                                const QString& note) {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* lbl = new QLabel(note, page);
    lbl->setStyleSheet("color: #888; font-style: italic;");
    lbl->setAlignment(Qt::AlignCenter);
    layout->addStretch(1);
    layout->addWidget(lbl);
    layout->addStretch(1);
    addPage(title, page);
}

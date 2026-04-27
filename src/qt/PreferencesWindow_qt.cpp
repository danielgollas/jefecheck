#include "PreferencesWindow_qt.h"

#include "../gfcStructures.h"
#include "../UIConstants.h"

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
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

extern gfcSettings sett;

PreferencesWindow_Qt::PreferencesWindow_Qt(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Preferences");
    setModal(true);
    resize(640, 480);

    sidebar_ = new QListWidget(this);
    sidebar_->setFixedWidth(140);
    pages_ = new QStackedWidget(this);

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
    buttons->button(QDialogButtonBox::Save)->setText("Done");
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        // FLTK's prefs window does this on the Done callback. Persists
        // to the JefeCheck XML in getApplicationDataPath().
        saveSettings(&sett);
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* split = new QHBoxLayout();
    split->setContentsMargins(0, 0, 0, 0);
    split->addWidget(sidebar_);
    split->addWidget(pages_, /*stretch*/ 1);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->addLayout(split, /*stretch*/ 1);
    outer->addWidget(buttons);
}

void PreferencesWindow_Qt::addPage(const QString& title, QWidget* page) {
    sidebar_->addItem(title);
    pages_->addWidget(page);
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

    auto* browsePath = new QLineEdit(QString::fromStdString(sett.defaultBrowsePath), page);
    connect(browsePath, &QLineEdit::editingFinished, page, [browsePath]() {
        sett.defaultBrowsePath = browsePath->text().toStdString();
    });
    form->addRow("Default browse path", browsePath);

    auto* fullscreen = new QCheckBox("Start in fullscreen", page);
    fullscreen->setChecked(sett.startFullscreen != 0);
    connect(fullscreen, &QCheckBox::toggled, page,
            [](bool on) { sett.startFullscreen = on ? 1 : 0; });
    form->addRow(QString(), fullscreen);

    auto* showLoad = new QCheckBox("Open Load window at startup", page);
    showLoad->setChecked(sett.openLoadWindowAtStartup != 0);
    connect(showLoad, &QCheckBox::toggled, page,
            [](bool on) { sett.openLoadWindowAtStartup = on ? 1 : 0; });
    form->addRow(QString(), showLoad);

    auto* recovery = new QCheckBox("Enable crash recovery session", page);
    recovery->setChecked(sett.enableCrashRecoverySession != 0);
    connect(recovery, &QCheckBox::toggled, page,
            [](bool on) { sett.enableCrashRecoverySession = on ? 1 : 0; });
    form->addRow(QString(), recovery);

    auto* aspect = new QDoubleSpinBox(page);
    aspect->setRange(0.0, 1.0);
    aspect->setSingleStep(0.05);
    aspect->setValue(sett.aspectBarsOpacity);
    connect(aspect, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            page, [](double v) { sett.aspectBarsOpacity = float(v); });
    form->addRow("Aspect-bar opacity", aspect);

    auto* procPri = new QSpinBox(page);
    procPri->setRange(0, 10);
    procPri->setValue(sett.processorPriority);
    connect(procPri, QOverload<int>::of(&QSpinBox::valueChanged),
            page, [](int v) { sett.processorPriority = v; });
    form->addRow("Processor priority", procPri);

    addPage("General", page);
}

void PreferencesWindow_Qt::buildEnginePage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    auto* engine = new QComboBox(page);
    engine->addItems({"3D", "2D"});
    engine->setCurrentIndex(sett.renderingEngine == 0 ? 0 : 1);
    connect(engine, QOverload<int>::of(&QComboBox::currentIndexChanged),
            page, [](int idx) { sett.renderingEngine = idx; });
    form->addRow("Rendering engine", engine);

    auto* vsync = new QCheckBox("VSync", page);
    vsync->setChecked(sett.vsync != 0);
    connect(vsync, &QCheckBox::toggled, page,
            [](bool on) { sett.vsync = on ? 1 : 0; });
    form->addRow(QString(), vsync);

    auto* queue = new QSpinBox(page);
    queue->setRange(0, 32);
    queue->setValue(sett.maximumFramesInQueue);
    connect(queue, QOverload<int>::of(&QSpinBox::valueChanged),
            page, [](int v) { sett.maximumFramesInQueue = v; });
    form->addRow("Max frames in raw queue", queue);

    auto* partitions = new QSpinBox(page);
    partitions->setRange(1, 16);
    partitions->setValue(sett.numOfPartitions);
    connect(partitions, QOverload<int>::of(&QSpinBox::valueChanged),
            page, [](int v) { sett.numOfPartitions = v; });
    form->addRow("Loader partitions", partitions);

    auto* balance = new QCheckBox("Balance read mutex across tracks", page);
    balance->setChecked(sett.balanceReads != 0);
    connect(balance, &QCheckBox::toggled, page,
            [](bool on) { sett.balanceReads = on ? 1 : 0; });
    form->addRow(QString(), balance);

    auto* pbo = new QSpinBox(page);
    pbo->setRange(0, 4);
    pbo->setValue(sett.forcePBO);
    connect(pbo, QOverload<int>::of(&QSpinBox::valueChanged),
            page, [](int v) { sett.forcePBO = v; });
    form->addRow("Force PBO mode", pbo);

    addPage("Engine", page);
}

void PreferencesWindow_Qt::buildFormatsPage() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);

    auto* exrIgnoreDisplay = new QCheckBox("EXR: ignore display window", page);
    exrIgnoreDisplay->setChecked(sett.exrIgnoreDisplayWindow != 0);
    connect(exrIgnoreDisplay, &QCheckBox::toggled, page,
            [](bool on) { sett.exrIgnoreDisplayWindow = on ? 1 : 0; });
    form->addRow(QString(), exrIgnoreDisplay);

    auto* exrIgnoreAspect = new QCheckBox("EXR: ignore header aspect ratio", page);
    exrIgnoreAspect->setChecked(sett.exrIgnoreHeadersAspectRatio != 0);
    connect(exrIgnoreAspect, &QCheckBox::toggled, page,
            [](bool on) { sett.exrIgnoreHeadersAspectRatio = on ? 1 : 0; });
    form->addRow(QString(), exrIgnoreAspect);

    auto makeEXRSpin = [page](float& field, double min_, double max_, double step) {
        auto* s = new QDoubleSpinBox(page);
        s->setRange(min_, max_);
        s->setSingleStep(step);
        s->setDecimals(3);
        s->setValue(field);
        QObject::connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                         page, [&field](double v) { field = float(v); });
        return s;
    };

    form->addRow("EXR exposure", makeEXRSpin(sett.exrExposure, -20.0, 20.0, 0.1));
    form->addRow("EXR defog",    makeEXRSpin(sett.exrDefog, 0.0, 1.0, 0.01));
    form->addRow("EXR gamma",    makeEXRSpin(sett.exrGamma, 0.1, 5.0, 0.05));
    form->addRow("EXR knee low", makeEXRSpin(sett.exrKneeLow, -10.0, 10.0, 0.1));
    form->addRow("EXR knee high",makeEXRSpin(sett.exrKneeHigh, -10.0, 10.0, 0.1));

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

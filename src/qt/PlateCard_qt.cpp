#include "PlateCard_qt.h"
#include "gfcplategui_qt.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {
QDoubleSpinBox* makeSpin(QWidget* parent, double min, double max, double step,
                         double initial, int width) {
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setValue(initial);
    s->setDecimals(step < 1.0 ? 2 : 0);
    s->setFixedWidth(width);
    s->setAlignment(Qt::AlignRight);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    return s;
}

QLabel* makeInlineLabel(QWidget* parent, const QString& text) {
    auto* l = new QLabel(text, parent);
    l->setStyleSheet("color: #888;");
    return l;
}

QPushButton* makeToggle(QWidget* parent, const QString& text,
                        const QString& tip, int width = 32) {
    auto* b = new QPushButton(text, parent);
    b->setCheckable(true);
    b->setToolTip(tip);
    b->setFixedHeight(20);
    b->setMinimumWidth(width);
    return b;
}
}  // namespace

PlateCard_Qt::PlateCard_Qt(int id, gfcPlateGUI_Qt* external, QWidget* parent)
    : QFrame(parent),
      id_(id) {
    if (external) {
        gui_ = external;
    } else {
        ownedGui_ = std::make_unique<gfcPlateGUI_Qt>();
        gui_ = ownedGui_.get();
        gui_->setPlateIndex(id);
        gui_->setTrackChoice(id);
    }

    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Plain);
    setMinimumSize(280, 84);

    setStyleSheet(
        "QLabel, QPushButton, QSpinBox, QDoubleSpinBox, QComboBox, "
        "QComboBox QAbstractItemView, QAbstractSpinBox { font-size: 10pt; }"
    );

    auto* plateId = new QLabel(QString::number(id + 1), this);
    plateId->setStyleSheet("font-weight: bold; font-size: 12pt; color: #ccc;");
    plateId->setFixedWidth(14);
    plateId->setAlignment(Qt::AlignCenter);

    trackBox_ = new QComboBox(this);
    trackBox_->addItems({"A", "B", "C", "D"});
    trackBox_->setCurrentIndex(gui_->getSequenceID() >= 0 ? gui_->getSequenceID() : id);
    trackBox_->setFixedWidth(40);

    aspectBox_ = new QComboBox(this);
    aspectBox_->setEditable(true);
    aspectBox_->addItems({"original", "16:9", "4:3", "2.39:1", "2.35:1", "1.85:1", "1.37:1"});
    aspectBox_->setMinimumWidth(60);

    cropBtn_ = makeToggle(this, "Crop", "Toggle crop bars (aspect-ratio letterbox)", 36);
    flipBtn_ = makeToggle(this, "Flip", "Flip vertically", 32);
    flopBtn_ = makeToggle(this, "Flop", "Flop horizontally", 32);

    rgbaBtn_ = new QPushButton("RGB", this);
    rgbaBtn_->setCheckable(true);
    rgbaBtn_->setToolTip("Cycle RGBA channel display (shortcuts r/g/b/a)");
    rgbaBtn_->setFixedHeight(20);
    rgbaBtn_->setMinimumWidth(36);

    auto* row1 = new QHBoxLayout();
    row1->setSpacing(4);
    row1->addWidget(plateId);
    row1->addWidget(trackBox_);
    row1->addWidget(aspectBox_, 1);
    row1->addWidget(cropBtn_);
    row1->addWidget(flipBtn_);
    row1->addWidget(flopBtn_);
    row1->addWidget(rgbaBtn_);

    zoomSpin_ = makeSpin(this, 0.01,    99.99, 0.01, 1.0, 46);
    panXSpin_ = makeSpin(this, -9999.0, 9999.0, 1.0,  0.0, 50);
    panYSpin_ = makeSpin(this, -9999.0, 9999.0, 1.0,  0.0, 50);
    rotSpin_  = makeSpin(this, -360.0,  360.0,  0.01, 0.0, 56);

    lutBox_ = new QComboBox(this);
    lutBox_->addItem("No LUT");
    lutBox_->setMinimumWidth(70);

    auto* row2 = new QHBoxLayout();
    row2->setSpacing(2);
    row2->addWidget(makeInlineLabel(this, "Zoom"));
    row2->addWidget(zoomSpin_);
    row2->addSpacing(4);
    row2->addWidget(makeInlineLabel(this, "X"));
    row2->addWidget(panXSpin_);
    row2->addWidget(makeInlineLabel(this, "Y"));
    row2->addWidget(panYSpin_);
    row2->addSpacing(4);
    row2->addWidget(makeInlineLabel(this, "R"));
    row2->addWidget(rotSpin_);
    row2->addSpacing(4);
    row2->addWidget(makeInlineLabel(this, "LUT"));
    row2->addWidget(lutBox_, 1);

    gammaSpin_      = makeSpin(this, 0.01,   99.99, 0.01, 1.0, 44);
    exposureSpin_   = makeSpin(this, -99.99, 99.99, 0.01, 0.0, 48);
    contrastSpin_   = makeSpin(this, 0.01,   99.99, 0.01, 1.0, 44);
    brightnessSpin_ = makeSpin(this, -99.99, 99.99, 0.01, 0.0, 48);
    saturationSpin_ = makeSpin(this, 0.0,    99.99, 0.01, 1.0, 44);

    auto* row3 = new QHBoxLayout();
    row3->setSpacing(2);
    row3->addWidget(makeInlineLabel(this, "γ"));
    row3->addWidget(gammaSpin_);
    row3->addWidget(makeInlineLabel(this, "Ex"));
    row3->addWidget(exposureSpin_);
    row3->addWidget(makeInlineLabel(this, "Cn"));
    row3->addWidget(contrastSpin_);
    row3->addWidget(makeInlineLabel(this, "Br"));
    row3->addWidget(brightnessSpin_);
    row3->addWidget(makeInlineLabel(this, "St"));
    row3->addWidget(saturationSpin_);
    row3->addStretch(1);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 2, 4, 2);
    outer->setSpacing(2);
    outer->addLayout(row1);
    outer->addLayout(row2);
    outer->addLayout(row3);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    // Wire signals to the GUI state object. gfcPlate reads through the
    // same interface, so each connect() below is the actual control →
    // rendering pipeline link.
    auto* g = gui_;
    using QDS = QDoubleSpinBox;
    using QCB = QComboBox;

    connect(trackBox_,  QOverload<int>::of(&QCB::currentIndexChanged),
            this, [g](int idx) { g->setTrackChoice(idx); });
    connect(aspectBox_, &QCB::currentTextChanged,
            this, [g](const QString& s) { g->setAspectChoice(s.toStdString()); });

    connect(cropBtn_, &QPushButton::toggled, this, [g](bool on) { g->setCrop(on ? 1 : 0); });
    connect(flipBtn_, &QPushButton::toggled, this, [g](bool on) { g->setFlip(on ? 1 : 0); });
    connect(flopBtn_, &QPushButton::toggled, this, [g](bool on) { g->setFlop(on ? 1 : 0); });
    connect(rgbaBtn_, &QPushButton::clicked, this, [g]() {
        // Cycle RGBA mode 0..3 each click. The FLTK build calls a similar
        // toggle from a single button.
        const int next = (g->getRGBA() + 1) % 4;
        g->setRGBA(next);
    });

    connect(zoomSpin_, QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setScale((float)v); });
    connect(panXSpin_, QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setTX((float)v); });
    connect(panYSpin_, QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setTY((float)v); });
    connect(rotSpin_,  QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setRZ((float)v); });

    connect(lutBox_,  QOverload<int>::of(&QCB::currentIndexChanged),
            this, [g](int idx) { g->setLUT(idx); });

    connect(gammaSpin_,      QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setGamma((float)v); });
    connect(exposureSpin_,   QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setExposure((float)v); });
    connect(contrastSpin_,   QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setContrast((float)v); });
    connect(brightnessSpin_, QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setBrightness((float)v); });
    connect(saturationSpin_, QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setSaturation((float)v); });

    refreshFromState();
}

PlateCard_Qt::~PlateCard_Qt() = default;

void PlateCard_Qt::refreshFromState() {
    if (!gui_) return;

    // Block every widget's signals for the duration of the refresh.
    // Without this, programmatic setValue() loops back into the setters
    // we just wired up, fighting the user's edits and re-rounding floats.
    const QSignalBlocker bTrack(trackBox_);
    const QSignalBlocker bAspect(aspectBox_);
    const QSignalBlocker bCrop(cropBtn_);
    const QSignalBlocker bFlip(flipBtn_);
    const QSignalBlocker bFlop(flopBtn_);
    const QSignalBlocker bRgba(rgbaBtn_);
    const QSignalBlocker bZoom(zoomSpin_);
    const QSignalBlocker bPanX(panXSpin_);
    const QSignalBlocker bPanY(panYSpin_);
    const QSignalBlocker bRot(rotSpin_);
    const QSignalBlocker bLut(lutBox_);
    const QSignalBlocker bGamma(gammaSpin_);
    const QSignalBlocker bExposure(exposureSpin_);
    const QSignalBlocker bContrast(contrastSpin_);
    const QSignalBlocker bBrightness(brightnessSpin_);
    const QSignalBlocker bSaturation(saturationSpin_);

    const int track = gui_->getSequenceID();
    if (track >= 0 && track < trackBox_->count()) {
        trackBox_->setCurrentIndex(track);
    }

    const QString aspect = QString::fromStdString(gui_->getAspectString());
    if (!aspect.isEmpty() && aspectBox_->currentText() != aspect) {
        aspectBox_->setCurrentText(aspect);
    }

    cropBtn_->setChecked(gui_->getCrop() != 0);
    flipBtn_->setChecked(gui_->getFlip() != 0);
    flopBtn_->setChecked(gui_->getFlop() != 0);

    // RGB button label tracks the active channel mask. Matches the FLTK
    // single-button cycle: 0=RGB, 1=R, 2=G, 3=B (alpha lives elsewhere).
    static const char* kRgbaLabels[4] = {"RGB", "R", "G", "B"};
    const int rgba = gui_->getRGBA() & 3;
    rgbaBtn_->setText(kRgbaLabels[rgba]);
    rgbaBtn_->setChecked(rgba != 0);

    zoomSpin_->setValue(gui_->getScale());
    panXSpin_->setValue(gui_->getTX());
    panYSpin_->setValue(gui_->getTY());
    rotSpin_->setValue(gui_->getRZ());

    const int lut = gui_->getLUT();
    if (lut >= 0 && lut < lutBox_->count()) {
        lutBox_->setCurrentIndex(lut);
    }

    gammaSpin_->setValue(gui_->getGamma());
    exposureSpin_->setValue(gui_->getExposure());
    contrastSpin_->setValue(gui_->getContrast());
    brightnessSpin_->setValue(gui_->getBrightness());
    saturationSpin_->setValue(gui_->getSaturation());
}

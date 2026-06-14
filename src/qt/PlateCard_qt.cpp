#include "PlateCard_qt.h"
#include "SequenceLoadBridge_qt.h"
#include "gfcplategui_qt.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
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

    // Plate-card object names follow `plate.<idx>.<role>` so UI tests can
    // target a specific plate without depending on tab order. Setting
    // setAccessibleName as well so the AX label matches the on-screen text.
    const QString objPrefix = QStringLiteral("plate.%1").arg(id);
    setObjectName(objPrefix + ".card");
    setAccessibleName(QStringLiteral("Plate %1").arg(id + 1));

    auto* plateId = new QLabel(QString::number(id + 1), this);
    plateId->setStyleSheet("font-weight: bold; font-size: 12pt; color: #ccc;");
    plateId->setFixedWidth(14);
    plateId->setAlignment(Qt::AlignCenter);
    plateId->setObjectName(objPrefix + ".id.label");

    trackBox_ = new QComboBox(this);
    trackBox_->addItems({"A", "B", "C", "D"});
    trackBox_->setCurrentIndex(gui_->getSequenceID() >= 0 ? gui_->getSequenceID() : id);
    trackBox_->setFixedWidth(40);
    trackBox_->setObjectName(objPrefix + ".track.combo");
    trackBox_->setAccessibleName("Track");

    // EXR layer picker. Lives between Track and Aspect so it reads as
    // "this track, this layer, this aspect". Hidden until the plate's
    // current track has > 1 named layer (refreshFromState handles that
    // toggle); for non-EXR / single-layer files there's nothing to
    // choose so showing the combo would just be visual noise.
    layerBox_ = new QComboBox(this);
    layerBox_->setMinimumWidth(70);
    layerBox_->setObjectName(objPrefix + ".layer.combo");
    layerBox_->setAccessibleName("EXR layer");
    layerBox_->setToolTip("EXR layer driving this plate");
    layerBox_->setVisible(false);

    aspectBox_ = new QComboBox(this);
    aspectBox_->setEditable(true);
    aspectBox_->addItems({"original", "16:9", "4:3", "2.39:1", "2.35:1", "1.85:1", "1.37:1"});
    aspectBox_->setMinimumWidth(60);
    aspectBox_->setObjectName(objPrefix + ".aspect.combo");
    aspectBox_->setAccessibleName("Aspect ratio");

    cropBtn_ = makeToggle(this, "Crop", "Toggle crop bars (aspect-ratio letterbox)", 36);
    cropBtn_->setObjectName(objPrefix + ".crop.button");
    cropBtn_->setAccessibleName("Crop");
    flipBtn_ = makeToggle(this, "Flip", "Flip vertically", 32);
    flipBtn_->setObjectName(objPrefix + ".flip.button");
    flipBtn_->setAccessibleName("Flip");
    flopBtn_ = makeToggle(this, "Flop", "Flop horizontally", 32);
    flopBtn_->setObjectName(objPrefix + ".flop.button");
    flopBtn_->setAccessibleName("Flop");

    rgbaBtn_ = new QPushButton("RGB", this);
    rgbaBtn_->setCheckable(true);
    rgbaBtn_->setToolTip("Cycle RGBA channel display (shortcuts r/g/b/a)");
    rgbaBtn_->setFixedHeight(20);
    rgbaBtn_->setMinimumWidth(36);
    rgbaBtn_->setObjectName(objPrefix + ".rgba.button");
    rgbaBtn_->setAccessibleName("RGBA channel");

    auto* row1 = new QHBoxLayout();
    row1->setSpacing(4);
    row1->addWidget(plateId);
    row1->addWidget(trackBox_);
    row1->addWidget(layerBox_);
    row1->addWidget(aspectBox_, 1);
    row1->addWidget(cropBtn_);
    row1->addWidget(flipBtn_);
    row1->addWidget(flopBtn_);
    row1->addWidget(rgbaBtn_);

    zoomSpin_ = makeSpin(this, 0.01,    99.99, 0.01, 1.0, 46);
    zoomSpin_->setObjectName(objPrefix + ".zoom.spin");
    zoomSpin_->setAccessibleName("Zoom");
    panXSpin_ = makeSpin(this, -9999.0, 9999.0, 1.0,  0.0, 50);
    panXSpin_->setObjectName(objPrefix + ".panx.spin");
    panXSpin_->setAccessibleName("Pan X");
    panYSpin_ = makeSpin(this, -9999.0, 9999.0, 1.0,  0.0, 50);
    panYSpin_->setObjectName(objPrefix + ".pany.spin");
    panYSpin_->setAccessibleName("Pan Y");
    rotSpin_  = makeSpin(this, -360.0,  360.0,  0.01, 0.0, 56);
    rotSpin_->setObjectName(objPrefix + ".rotation.spin");
    rotSpin_->setAccessibleName("Rotation");

    lutBox_ = new QComboBox(this);
    lutBox_->addItem("No LUT");
    lutBox_->setMinimumWidth(70);
    lutBox_->setObjectName(objPrefix + ".lut.combo");
    lutBox_->setAccessibleName("LUT");

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
    gammaSpin_->setObjectName(objPrefix + ".gamma.spin");
    gammaSpin_->setAccessibleName("Gamma");
    exposureSpin_   = makeSpin(this, -99.99, 99.99, 0.01, 0.0, 48);
    exposureSpin_->setObjectName(objPrefix + ".exposure.spin");
    exposureSpin_->setAccessibleName("Exposure");
    contrastSpin_   = makeSpin(this, 0.01,   99.99, 0.01, 1.0, 44);
    contrastSpin_->setObjectName(objPrefix + ".contrast.spin");
    contrastSpin_->setAccessibleName("Contrast");
    brightnessSpin_ = makeSpin(this, -99.99, 99.99, 0.01, 0.0, 48);
    brightnessSpin_->setObjectName(objPrefix + ".brightness.spin");
    brightnessSpin_->setAccessibleName("Brightness");
    saturationSpin_ = makeSpin(this, 0.0,    99.99, 0.01, 1.0, 44);
    saturationSpin_->setObjectName(objPrefix + ".saturation.spin");
    saturationSpin_->setAccessibleName("Saturation");

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

    // Route through the bridge (rather than g->setTrackChoice) so the
    // change actually reaches gfcPlate::track. setTrackChoice on the GUI
    // alone only updates the parallel GUI value object — gfcPlate's
    // rendering path reads through plateManager.setTrackOnPlate, which
    // also clears the histogram cache and pushes a feedback message.
    const int boundPlateIdForTrack = id_;
    connect(trackBox_,  QOverload<int>::of(&QCB::currentIndexChanged),
            this, [boundPlateIdForTrack](int idx) {
                jefe::qt::setTrackOnPlate(boundPlateIdForTrack, idx);
            });
    connect(aspectBox_, &QCB::currentTextChanged,
            this, [g](const QString& s) { g->setAspectChoice(s.toStdString()); });

    // Layer change → bridge does the heavy lifting (rewrite the track's
    // gfcSequenceGUI channel name, re-decode the preview, and kick off
    // a full async sequence reload so every cached frame matches the
    // newly-selected layer). The combo's items are layer name strings
    // pulled from the OIIO loader's discovery, so the text round-trips
    // unchanged.
    {
        const int boundPlateId = id_;
        connect(layerBox_, &QCB::currentTextChanged,
                this, [boundPlateId](const QString& s) {
                    if (s.isEmpty()) return;
                    jefe::qt::setLayerOnPlate(boundPlateId, s.toStdString());
                });
    }

    // Every plate-card slot that writes to the Qt GUI must follow with a
    // jefe::qt::propagatePlateChanges() call. Without it, the plate's
    // actual fields stay stale and the super-shader never picks up the
    // new value — controls appear inert. The LUT and Layer combos below
    // route through bridge functions that already handle propagation.
    connect(cropBtn_, &QPushButton::toggled, this, [g](bool on) {
        g->setCrop(on ? 1 : 0); jefe::qt::propagatePlateChanges();
    });
    connect(flipBtn_, &QPushButton::toggled, this, [g](bool on) {
        g->setFlip(on ? 1 : 0); jefe::qt::propagatePlateChanges();
    });
    connect(flopBtn_, &QPushButton::toggled, this, [g](bool on) {
        g->setFlop(on ? 1 : 0); jefe::qt::propagatePlateChanges();
    });
    connect(rgbaBtn_, &QPushButton::clicked, this, [g]() {
        // Cycle RGBA mode 0..3 each click. The FLTK build calls a similar
        // toggle from a single button.
        const int next = (g->getRGBA() + 1) % 4;
        g->setRGBA(next);
        jefe::qt::propagatePlateChanges();
    });

    connect(zoomSpin_, QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setScale((float)v); jefe::qt::propagatePlateChanges(); });
    connect(panXSpin_, QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setTX((float)v); jefe::qt::propagatePlateChanges(); });
    connect(panYSpin_, QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setTY((float)v); jefe::qt::propagatePlateChanges(); });
    connect(rotSpin_,  QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setRZ((float)v); jefe::qt::propagatePlateChanges(); });

    // Route LUT change through the bridge so plates[id_].setLUT actually
    // binds the new GL texture and recompiles the super-shader. Calling
    // gui_->setLUT alone only writes the GUI value; the rendering path
    // wouldn't see it until a separate updateValuesFromGUI cycle.
    const int boundPlateId = id_;
    connect(lutBox_,  QOverload<int>::of(&QCB::currentIndexChanged),
            this, [boundPlateId](int idx) {
                jefe::qt::applyLUTToPlate(boundPlateId, idx);
            });

    connect(gammaSpin_,      QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setGamma((float)v); jefe::qt::propagatePlateChanges(); });
    connect(exposureSpin_,   QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setExposure((float)v); jefe::qt::propagatePlateChanges(); });
    connect(contrastSpin_,   QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setContrast((float)v); jefe::qt::propagatePlateChanges(); });
    connect(brightnessSpin_, QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setBrightness((float)v); jefe::qt::propagatePlateChanges(); });
    connect(saturationSpin_, QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setSaturation((float)v); jefe::qt::propagatePlateChanges(); });

    refreshFromState();
}

PlateCard_Qt::~PlateCard_Qt() = default;

void PlateCard_Qt::mousePressEvent(QMouseEvent* e) {
    // Child widgets (spinboxes, combos, buttons) handle their own
    // events first; this only fires when the user clicks on the card's
    // background or a label.
    if (e->button() == Qt::LeftButton) {
        emit clicked(id_);
    }
    QFrame::mousePressEvent(e);
}

void PlateCard_Qt::setActiveHighlight(bool on) {
    // Border-only cue. The orange matches the FLTK build's accent
    // color (and the dark-VFX theme's active-state color). 1px is
    // enough to read at the card's compact height without crowding
    // the spinboxes inside.
    if (on) {
        setStyleSheet(styleSheet() +
            " PlateCard_Qt { border: 1px solid #d4771e; }");
    } else {
        // Strip our injected border by rebuilding the base stylesheet.
        setStyleSheet(
            "QLabel, QPushButton, QSpinBox, QDoubleSpinBox, QComboBox, "
            "QComboBox QAbstractItemView, QAbstractSpinBox { font-size: 10pt; }"
        );
    }
}

void PlateCard_Qt::refreshFromState() {
    if (!gui_) return;

    // Read fresh state once up front. The reads themselves are cheap
    // (plain getters off the GUI state object); the cost being optimized
    // is the widget-side setValue/setText/setChecked/setCurrentIndex
    // cascade through QAccessible + AppKit + AttributeGraph that fires
    // even when the new value equals the old.
    const int track = gui_->getSequenceID();
    const QString aspect = QString::fromStdString(gui_->getAspectString());
    const bool crop = gui_->getCrop() != 0;
    const bool flip = gui_->getFlip() != 0;
    const bool flop = gui_->getFlop() != 0;
    const int rgba = gui_->getRGBA() & 3;
    const float scale = gui_->getScale();
    const float tx = gui_->getTX();
    const float ty = gui_->getTY();
    const float rz = gui_->getRZ();
    const int lut = gui_->getLUT();
    const auto& lutOpts = gui_->getLUTOptions();
    const float gamma = gui_->getGamma();
    const float exposure = gui_->getExposure();
    const float contrast = gui_->getContrast();
    const float brightness = gui_->getBrightness();
    const float saturation = gui_->getSaturation();
    const auto layers = jefe::qt::getLayersOnPlate(id_);
    const std::string activeLayer = jefe::qt::getActiveLayerOnPlate(id_);

    // Block every widget's signals for the duration of the refresh.
    // Without this, programmatic setValue() loops back into the setters
    // we just wired up, fighting the user's edits and re-rounding floats.
    const QSignalBlocker bTrack(trackBox_);
    const QSignalBlocker bLayer(layerBox_);
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

    const bool firstPass = !lastShown_.valid;

    if ((firstPass || track != lastShown_.track)
        && track >= 0 && track < trackBox_->count()) {
        trackBox_->setCurrentIndex(track);
    }

    // Layer combo: populate from the bridge's view of the plate's track.
    // Hide the combo for files with no named layers (single-layer EXR,
    // or any non-EXR format — the OIIO loader returns one entry with
    // an empty name). Showing a one-item combo with a blank label
    // would just be confusing.
    {
        bool hasNamedLayers = false;
        for (const auto& n : layers) {
            if (!n.empty()) { hasNamedLayers = true; break; }
        }
        if (hasNamedLayers) {
            // Only rebuild the item list if the cached list shows a
            // different shape. Touching items causes the combo to
            // flicker even with signals blocked.
            const bool listChanged = firstPass || (layers != lastShown_.layers);
            if (listChanged) {
                layerBox_->clear();
                for (const auto& name : layers) {
                    layerBox_->addItem(QString::fromStdString(name));
                }
            }
            const QString want = QString::fromStdString(activeLayer);
            if (!want.isEmpty()
                && (firstPass || activeLayer != lastShown_.activeLayer
                    || listChanged)
                && layerBox_->currentText() != want) {
                layerBox_->setCurrentText(want);
            }
            if (firstPass || !lastShown_.layerVisible) {
                layerBox_->setVisible(true);
            }
            lastShown_.layerVisible = true;
        } else {
            if (firstPass || lastShown_.layerVisible) {
                layerBox_->setVisible(false);
            }
            lastShown_.layerVisible = false;
        }
        lastShown_.layers = layers;
        lastShown_.activeLayer = activeLayer;
    }

    if ((firstPass || aspect != lastShown_.aspect)
        && !aspect.isEmpty()
        && aspectBox_->currentText() != aspect) {
        aspectBox_->setCurrentText(aspect);
    }

    if (firstPass || crop != lastShown_.crop) cropBtn_->setChecked(crop);
    if (firstPass || flip != lastShown_.flip) flipBtn_->setChecked(flip);
    if (firstPass || flop != lastShown_.flop) flopBtn_->setChecked(flop);

    // RGB button label tracks the active channel mask. Matches the FLTK
    // single-button cycle: 0=RGB, 1=R, 2=G, 3=B (alpha lives elsewhere).
    static const char* kRgbaLabels[4] = {"RGB", "R", "G", "B"};
    if (firstPass || rgba != lastShown_.rgba) {
        rgbaBtn_->setText(kRgbaLabels[rgba]);
        rgbaBtn_->setChecked(rgba != 0);
    }

    if (firstPass || scale != lastShown_.scale)         zoomSpin_->setValue(scale);
    if (firstPass || tx    != lastShown_.tx)            panXSpin_->setValue(tx);
    if (firstPass || ty    != lastShown_.ty)            panYSpin_->setValue(ty);
    if (firstPass || rz    != lastShown_.rz)            rotSpin_->setValue(rz);

    // Rebuild the LUT combo if the gui's LUT list changed (it grows after
    // initializeInstallLUTs runs the autoload). Cheap to compare; saves a
    // user-visible flicker when the names already match.
    const bool lutListChanged = firstPass || (lutOpts != lastShown_.lutOptions);
    if (lutListChanged) {
        lutBox_->clear();
        for (const auto& name : lutOpts) {
            lutBox_->addItem(QString::fromStdString(name));
        }
        lastShown_.lutOptions = lutOpts;
    }
    if ((firstPass || lut != lastShown_.lut || lutListChanged)
        && lut >= 0 && lut < lutBox_->count()) {
        lutBox_->setCurrentIndex(lut);
    }

    if (firstPass || gamma      != lastShown_.gamma)      gammaSpin_->setValue(gamma);
    if (firstPass || exposure   != lastShown_.exposure)   exposureSpin_->setValue(exposure);
    if (firstPass || contrast   != lastShown_.contrast)   contrastSpin_->setValue(contrast);
    if (firstPass || brightness != lastShown_.brightness) brightnessSpin_->setValue(brightness);
    if (firstPass || saturation != lastShown_.saturation) saturationSpin_->setValue(saturation);

    // Commit the freshly-read values to the cache. Future calls compare
    // against these and short-circuit when they match.
    lastShown_.track       = track;
    lastShown_.aspect      = aspect;
    lastShown_.crop        = crop;
    lastShown_.flip        = flip;
    lastShown_.flop        = flop;
    lastShown_.rgba        = rgba;
    lastShown_.scale       = scale;
    lastShown_.tx          = tx;
    lastShown_.ty          = ty;
    lastShown_.rz          = rz;
    lastShown_.lut         = lut;
    lastShown_.gamma       = gamma;
    lastShown_.exposure    = exposure;
    lastShown_.contrast    = contrast;
    lastShown_.brightness  = brightness;
    lastShown_.saturation  = saturation;
    lastShown_.valid       = true;
}

void PlateCard_Qt::refreshTransformOnly() {
    if (!gui_) return;
    // Read only the four fields that change during a viewport pan/zoom drag.
    // Same delta-vs-cache gating as refreshFromState — typical drag touch
    // is 2-4 setValue calls on this one card, with no other widget scope
    // churn and no FX panel refresh fired.
    const float scale = gui_->getScale();
    const float tx    = gui_->getTX();
    const float ty    = gui_->getTY();
    const float rz    = gui_->getRZ();

    if (lastShown_.valid
        && scale == lastShown_.scale
        && tx == lastShown_.tx
        && ty == lastShown_.ty
        && rz == lastShown_.rz) {
        return;  // nothing changed — skip widget access entirely
    }

    const QSignalBlocker bZoom(zoomSpin_);
    const QSignalBlocker bPanX(panXSpin_);
    const QSignalBlocker bPanY(panYSpin_);
    const QSignalBlocker bRot(rotSpin_);

    if (scale != lastShown_.scale) { zoomSpin_->setValue(scale); lastShown_.scale = scale; }
    if (tx    != lastShown_.tx)    { panXSpin_->setValue(tx);   lastShown_.tx    = tx; }
    if (ty    != lastShown_.ty)    { panYSpin_->setValue(ty);   lastShown_.ty    = ty; }
    if (rz    != lastShown_.rz)    { rotSpin_->setValue(rz);    lastShown_.rz    = rz; }
}

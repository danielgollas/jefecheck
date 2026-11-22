// SPDX-License-Identifier: GPL-2.0-or-later
#include "PlateCard_qt.h"
#include "AspectCropCombo_qt.h"
#include "SequenceLoadBridge_qt.h"
#include "gfcplategui_qt.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPushButton>
#include <QScreen>
#include <QGuiApplication>
#include <QToolButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>

#include "UiMetrics_qt.h"
#include "qticons.h"

namespace ui = jefe::qt::ui;

namespace {
// Dynamic property the theme keys the grey caption colour on (jefecheck_dark.qss).
constexpr const char* kCaptionProperty = "plateCaption";
// Layer combo widths: "Main" / "right" fit the wide-short one; longer names elide.
constexpr int kLayerWideShortW = 80;
constexpr int kLayerNarrowTallW = 92;

// Spin boxes keep the theme's 24 px height and their own sizeHint width.
QDoubleSpinBox* makeSpin(QWidget* parent, double min, double max, double step,
                         double initial) {
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setValue(initial);
    s->setDecimals(step < 1.0 ? 2 : 0);
    s->setAlignment(Qt::AlignRight);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    return s;
}

// A caption beside its field, kGapTight apart. `capObjectName` names the caption; a click
// on it propagates to PlateCard_Qt::mousePressEvent (the plate-activation target).
QHBoxLayout* captioned(QWidget* parent, const QString& caption, QWidget* w,
                       const QString& capObjectName = QString()) {
    auto* h = new QHBoxLayout();
    ui::applyRowMetrics(h, ui::kGapTight);
    auto* cap = new QLabel(caption, parent);
    cap->setProperty(kCaptionProperty, true);
    cap->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // Hug the text, so a field that takes the row's spare width (LUT) keeps its caption beside it.
    cap->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    if (!capObjectName.isEmpty()) cap->setObjectName(capObjectName);
    h->addWidget(cap);
    h->addWidget(w);
    return h;
}

// Double-headed mirror arrow on the 16 px icon box: vertical = up/down (Flip), else
// left/right (Flop). Drawn on a 14-unit grid scaled to the box.
void paintMirror(QPainter& p, qreal side, const QColor& color, bool vertical) {
    p.scale(side / 14.0, side / 14.0);
    QPen pen(color, 1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    if (vertical) {  // Flip: vertical double-headed arrow
        p.drawLine(QPointF(7, 3), QPointF(7, 11));     // shaft
        p.drawLine(QPointF(7, 3), QPointF(4.5, 5.5));  // up head
        p.drawLine(QPointF(7, 3), QPointF(9.5, 5.5));
        p.drawLine(QPointF(7, 11), QPointF(4.5, 8.5));  // down head
        p.drawLine(QPointF(7, 11), QPointF(9.5, 8.5));
    } else {  // Flop: horizontal double-headed arrow
        p.drawLine(QPointF(3, 7), QPointF(11, 7));     // shaft
        p.drawLine(QPointF(3, 7), QPointF(5.5, 4.5));  // left head
        p.drawLine(QPointF(3, 7), QPointF(5.5, 9.5));
        p.drawLine(QPointF(11, 7), QPointF(8.5, 4.5));  // right head
        p.drawLine(QPointF(11, 7), QPointF(8.5, 9.5));
    }
}

// Checkable 24x24 icon-only toggle (Flip/Flop) with the dual-state mirror glyph.
QPushButton* makeMirrorToggle(QWidget* parent, bool vertical, const QString& tip) {
    auto* b = new QPushButton(parent);
    b->setCheckable(true);
    b->setIcon(jefe::qticons::make([vertical](QPainter& p, qreal side, const QColor& c) {
        paintMirror(p, side, c, vertical);
    }, ui::kIconPx));
    b->setToolTip(tip);
    ui::makeIconButton(b);
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
    // No artificial minimum: the layout's minimumSizeHint governs, so a narrow dock
    // scrolls horizontally instead of squashing fixed-width children into overlap.
    // No stylesheet on the card: the theme sizes the controls (13 px text, 24 px tall)
    // and styles the active/offscreen states through dynamic properties.

    // Object names follow `plate.<idx>.<role>` so UI tests can target a plate; the
    // accessible name matches the on-screen text.
    objPrefix_ = QStringLiteral("plate.%1").arg(id);
    setObjectName(objPrefix_ + ".card");
    setAccessibleName(QStringLiteral("Plate %1").arg(id + 1));

    trackBox_ = new QComboBox(this);
    trackBox_->addItems({"A", "B", "C", "D"});
    trackBox_->setCurrentIndex(gui_->getSequenceID() >= 0 ? gui_->getSequenceID() : id);
    trackBox_->setObjectName(objPrefix_ + ".track.combo");
    trackBox_->setAccessibleName("Track");
    trackBox_->setToolTip("Track shown on this plate");

    // Layer picker between Track and Aspect. Always visible: "Main" plus the EXR
    // sub-layers (e.g. "right", "diffuse") of a multi-layer file.
    layerBox_ = new QComboBox(this);
    // Fixed width (set per form in rebuildContent) so the card doesn't grow with a long
    // EXR layer name; longer names elide (the popup shows them in full).
    layerBox_->setProperty(ui::kDensityElidedProperty, true);
    layerBox_->setObjectName(objPrefix_ + ".layer.combo");
    layerBox_->setAccessibleName("Layer");
    layerBox_->setToolTip("Image layer / channel group driving this plate");

    // Aspect ratio control with the letterbox toggle in its popup. The checkbox keeps the
    // `crop.button` leaf objectName for UI-test locators; its visible label is "Letterbox".
    aspectCrop_ = new AspectCropCombo_Qt(this);
    aspectCrop_->setPresets({"original", "16:9", "4:3", "2.39:1", "2.35:1", "1.85:1", "1.37:1"});
    ui::useWidgetRect(aspectCrop_);
    aspectCrop_->setObjectName(objPrefix_ + ".aspect.combo");
    aspectCrop_->setAccessibleName("Aspect ratio");
    aspectCrop_->setToolTip("Aspect ratio");
    aspectCrop_->setCropObjectName(objPrefix_ + ".crop.button");
    aspectCrop_->setCropAccessibleName("Letterbox");
    aspectCrop_->setCropToolTip("Letterbox to the selected aspect ratio (black bars)");

    // Icon-only toggles: Flip = vertical mirror, Flop = horizontal. accessibleName stays
    // "Flip"/"Flop" so AX titles and UI-test locators are unchanged.
    flipBtn_ = makeMirrorToggle(this, /*vertical=*/true,
                                "Flip vertically (mirror top–bottom)");
    flipBtn_->setObjectName(objPrefix_ + ".flip.button");
    flipBtn_->setAccessibleName("Flip");
    flopBtn_ = makeMirrorToggle(this, /*vertical=*/false,
                                "Flop horizontally (mirror left–right)");
    flopBtn_->setObjectName(objPrefix_ + ".flop.button");
    flopBtn_->setAccessibleName("Flop");

    rgbaBtn_ = new QPushButton("RGB", this);
    rgbaBtn_->setCheckable(true);
    rgbaBtn_->setToolTip("Cycle RGBA channel display (shortcuts r/g/b/a)");
    // Held at the "RGB" width so the button doesn't jitter as its label cycles RGB -> R -> G -> B.
    ui::useWidgetRect(rgbaBtn_);
    rgbaBtn_->ensurePolished();
    rgbaBtn_->setMinimumWidth(rgbaBtn_->sizeHint().width());
    rgbaBtn_->setObjectName(objPrefix_ + ".rgba.button");
    rgbaBtn_->setAccessibleName("RGBA channel");

    zoomSpin_ = makeSpin(this, 0.01,  100.0, 0.01, 1.0);
    zoomSpin_->setObjectName(objPrefix_ + ".zoom.spin");
    zoomSpin_->setAccessibleName("Zoom");
    panXSpin_ = makeSpin(this, -99999.0, 99999.0, 1.0, 0.0);
    panXSpin_->setObjectName(objPrefix_ + ".panx.spin");
    panXSpin_->setAccessibleName("Pan X");
    panYSpin_ = makeSpin(this, -99999.0, 99999.0, 1.0, 0.0);
    panYSpin_->setObjectName(objPrefix_ + ".pany.spin");
    panYSpin_->setAccessibleName("Pan Y");
    rotSpin_  = makeSpin(this, -360.0,  360.0,  0.01, 0.0);
    rotSpin_->setObjectName(objPrefix_ + ".rotation.spin");
    rotSpin_->setAccessibleName("Rotation");

    // Transform button + popup (wide-short form): the face shows the zoom, the popup
    // holds Zoom/X/Y/Rot. The popup is parented to the card (Qt::Popup makes it its own
    // window), so the spins stay card descendants for findChild and UI-test locators.
    // Sized for a "10.00×" face (the card's width budget); a zoom of 100× or more widens
    // the button by one digit.
    transformPopup_ = makePopupButton(transformBtn_, "transform", QStringLiteral("10.00\u00d7"),
                                      "Transform", "Zoom, pan and rotation", &transformClosed_);
    colorPopup_ = makePopupButton(colorBtn_, "color", QStringLiteral("Color"),
                                  "Color", "Contrast, brightness and saturation", &colorClosed_);

    lutBox_ = new QComboBox(this);
    lutBox_->addItem("No LUT");
    // Ignored width policy: a QComboBox sizes to its longest item, which would balloon the
    // card once LUTs autoload. The popup shows full names; long selections elide.
    lutBox_->setMinimumWidth(50);
    lutBox_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    lutBox_->setProperty(ui::kDensityElidedProperty, true);
    lutBox_->setObjectName(objPrefix_ + ".lut.combo");
    lutBox_->setAccessibleName("LUT");

    // Color controls cap at 9.99; gamma/contrast/saturation are non-negative, exposure/
    // brightness bipolar.
    gammaSpin_      = makeSpin(this, 0.0,   9.99, 0.01, 1.0);
    gammaSpin_->setObjectName(objPrefix_ + ".gamma.spin");
    gammaSpin_->setAccessibleName("Gamma");
    exposureSpin_   = makeSpin(this, -9.99, 9.99, 0.01, 0.0);
    exposureSpin_->setObjectName(objPrefix_ + ".exposure.spin");
    exposureSpin_->setAccessibleName("Exposure");
    contrastSpin_   = makeSpin(this, 0.0,   9.99, 0.01, 1.0);
    contrastSpin_->setObjectName(objPrefix_ + ".contrast.spin");
    contrastSpin_->setAccessibleName("Contrast");
    brightnessSpin_ = makeSpin(this, -9.99, 9.99, 0.01, 0.0);
    brightnessSpin_->setObjectName(objPrefix_ + ".brightness.spin");
    brightnessSpin_->setAccessibleName("Brightness");
    saturationSpin_ = makeSpin(this, 0.0,   9.99, 0.01, 1.0);
    saturationSpin_->setObjectName(objPrefix_ + ".saturation.spin");
    saturationSpin_->setAccessibleName("Saturation");

    // Top-level layout: one swappable content_ child built by rebuildContent(), so the
    // wide-short and narrow-tall forms share one code path (controls are reparented).
    auto* top = new QVBoxLayout(this);
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(0);
    rebuildContent();

    // Wire signals to the GUI state object that gfcPlate reads through.
    auto* g = gui_;
    using QDS = QDoubleSpinBox;
    using QCB = QComboBox;

    // Route through the bridge, not g->setTrackChoice: gfcPlate reads via
    // plateManager.setTrackOnPlate, which also clears the histogram cache.
    const int boundPlateIdForTrack = id_;
    connect(trackBox_,  QOverload<int>::of(&QCB::currentIndexChanged),
            this, [boundPlateIdForTrack](int idx) {
                jefe::qt::setTrackOnPlate(boundPlateIdForTrack, idx);
            });
    connect(aspectCrop_, &AspectCropCombo_Qt::aspectChanged,
            this, [g](const QString& s) {
        g->setAspectChoice(s.toStdString());
        jefe::qt::propagatePlateChanges();
    });

    // Layer change: the bridge rewrites the track's channel name, re-decodes the preview
    // and kicks off an async reload so every cached frame matches the new layer.
    {
        const int boundPlateId = id_;
        connect(layerBox_, &QCB::currentTextChanged,
                this, [this, boundPlateId](const QString& s) {
                    if (s.isEmpty()) return;
                    jefe::qt::setLayerOnPlate(boundPlateId, s.toStdString());
                    // The layer lives on the shared sequence, so any other
                    // card bound to this track must refresh its combo.
                    emit layerChanged();
                });
    }

    // Every plate-card slot that writes to the Qt GUI must follow with
    // jefe::qt::propagatePlateChanges(), or the controls appear inert (the LUT/Layer combos use bridge functions).
    connect(aspectCrop_, &AspectCropCombo_Qt::cropToggled, this, [g](bool on) {
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
            this, [this, g](double v) {
                g->setScale((float)v); jefe::qt::propagatePlateChanges();
                updateTransformButton(v);
            });
    connect(panXSpin_, QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setTX((float)v); jefe::qt::propagatePlateChanges(); });
    connect(panYSpin_, QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setTY((float)v); jefe::qt::propagatePlateChanges(); });
    connect(rotSpin_,  QOverload<double>::of(&QDS::valueChanged),
            this, [g](double v) { g->setRZ((float)v); jefe::qt::propagatePlateChanges(); });

    // Route LUT change through the bridge so plates[id_].setLUT binds the GL texture and
    // recompiles the super-shader; gui_->setLUT alone only writes the GUI value.
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

QFrame* PlateCard_Qt::makePopupButton(QToolButton*& btn, const char* leaf, const QString& text,
                                      const QString& accessible, const QString& tip, QElapsedTimer* closed) {
    btn = new QToolButton(this);
    btn->setObjectName(QString("%1.%2.button").arg(objPrefix_, leaf));
    btn->setAccessibleName(accessible);
    btn->setToolTip(tip);
    ui::useWidgetRect(btn);
    btn->setText(text);
    btn->ensurePolished();
    btn->setMinimumWidth(btn->sizeHint().width());
    auto* popup = new QFrame(this, Qt::Popup);
    popup->setObjectName(QString("%1.%2.popup").arg(objPrefix_, leaf));
    popup->setFrameShape(QFrame::StyledPanel);
    auto* popLay = new QVBoxLayout(popup);
    popLay->setContentsMargins(0, 0, 0, 0);
    popLay->setSpacing(0);
    popup->installEventFilter(this);
    QToolButton* b = btn;
    connect(btn, &QToolButton::clicked, this, [this, popup, b, closed]() {
        // With the popup open, the Qt::Popup grab turns the press into an outside click
        // (Hide stamps `closed`) before clicked() fires; don't reopen on it.
        constexpr qint64 kReopenGuardMs = 150;
        if (closed->isValid() && !closed->hasExpired(kReopenGuardMs)) return;
        openPopup(popup, b);
    });
    return popup;
}

void PlateCard_Qt::setVertical(bool vertical) {
    if (vertical == vertical_) return;
    vertical_ = vertical;
    rebuildContent();
}

void PlateCard_Qt::rebuildContent() {
    // Build the new content into a fresh child widget. Adding the shared controls to nc's
    // layouts reparents them (signal connections persist); only captions/layouts are nc's own.
    // A popup left open across the swap would show controls that just moved into the card.
    // Only an open one is hidden: a Hide stamps the 150 ms reopen guard.
    if (transformPopup_->isVisible()) transformPopup_->hide();
    if (colorPopup_->isVisible()) colorPopup_->hide();
    auto* nc = new QWidget(this);

    auto* mirrorBox = new QWidget(nc);
    auto* mirrorLay = new QHBoxLayout(mirrorBox);
    ui::applyRowMetrics(mirrorLay, ui::kGapTight);
    mirrorLay->addWidget(flipBtn_);
    mirrorLay->addWidget(flopBtn_);

    QWidget* newPopupContent = nullptr;  // the Transform popup's captions (wide-short only)
    QWidget* newColorContent = nullptr;  // the Color popup's captions (wide-short only)
    auto* outer = new QVBoxLayout(nc);
    outer->setContentsMargins(ui::kGap, ui::kGap, ui::kGap, ui::kGap);
    outer->setSpacing(ui::kGapTight);

    if (!vertical_) {
        // ---- Wide-short (horizontal dock): two 24 px rows, each caption beside its field. ----
        // Side margins kGapTight (the dense-list exception, as on playlist cards) so two cards
        // fit beside the Timeline at 1280.
        outer->setContentsMargins(ui::kGapTight, ui::kGap, ui::kGapTight, ui::kGap);
        // Row 1: source, channel, Transform button and Flip/Flop (on this row so the LUT
        // combo below keeps room for its name). The "Track" caption carries the
        // `track.label` objectName as the plate-activation click target.
        // Only Track keeps a caption here (it is the click target); Layer, Aspect and Chan
        // read from their values and tooltips, so two cards fit beside the Timeline at 1280.
        layerBox_->setFixedWidth(kLayerWideShortW);
        auto* row1 = new QHBoxLayout();
        ui::applyRowMetrics(row1);
        // Two tight groups (kGapTight, controls that belong together): what the plate
        // shows (layer, aspect, channel) and how it is placed (transform, flip, flop).
        auto* sourceGroup = new QHBoxLayout();
        ui::applyRowMetrics(sourceGroup, ui::kGapTight);
        sourceGroup->addWidget(layerBox_);
        sourceGroup->addWidget(aspectCrop_);
        sourceGroup->addWidget(rgbaBtn_);
        auto* placeGroup = new QHBoxLayout();
        ui::applyRowMetrics(placeGroup, ui::kGapTight);
        placeGroup->addWidget(transformBtn_);
        placeGroup->addWidget(mirrorBox);
        row1->addLayout(captioned(nc, "Track",  trackBox_, objPrefix_ + ".track.label"));
        row1->addLayout(sourceGroup);
        row1->addLayout(placeGroup);
        row1->addStretch(1);  // absorb the width difference vs the other row
        transformBtn_->show();

        // Zoom/X/Y/Rot go into the Transform popup, one row of caption+field pairs.
        auto* pc = new QWidget(transformPopup_);
        auto* popRow = new QHBoxLayout(pc);
        popRow->setContentsMargins(ui::kGap, ui::kGap, ui::kGap, ui::kGap);
        popRow->setSpacing(ui::kGap);
        popRow->addLayout(captioned(pc, "Zoom", zoomSpin_));
        popRow->addLayout(captioned(pc, "X",    panXSpin_));
        popRow->addLayout(captioned(pc, "Y",    panYSpin_));
        popRow->addLayout(captioned(pc, "Rot",  rotSpin_));
        newPopupContent = pc;

        // Con/Bri/Sat go into the Color popup, so two cards fit beside the Timeline at 1280.
        auto* cc = new QWidget(colorPopup_);
        auto* colRow = new QHBoxLayout(cc);
        colRow->setContentsMargins(ui::kGap, ui::kGap, ui::kGap, ui::kGap);
        colRow->setSpacing(ui::kGap);
        colRow->addLayout(captioned(cc, "Con", contrastSpin_));
        colRow->addLayout(captioned(cc, "Bri", brightnessSpin_));
        colRow->addLayout(captioned(cc, "Sat", saturationSpin_));
        newColorContent = cc;

        // Row 2: gamma, exposure, the Color button, then LUT, which fills the row's
        // leftover width via its Ignored size policy (no trailing stretch).
        auto* row2 = new QHBoxLayout();
        ui::applyRowMetrics(row2);
        row2->addLayout(captioned(nc, "Gamma", gammaSpin_));
        row2->addLayout(captioned(nc, "Exp",   exposureSpin_));
        row2->addWidget(colorBtn_);
        row2->addLayout(captioned(nc, "LUT",   lutBox_), 1);
        colorBtn_->show();

        outer->addLayout(row1);
        outer->addLayout(row2);
    } else {
        // The spins sit in the card itself here; no Transform/Color buttons or popups.
        // Park the buttons on the card: left in the old content they would be deleted with it,
        // and the next wide-short rebuild would add a dangling pointer.
        transformBtn_->setParent(this);
        transformBtn_->hide();
        colorBtn_->setParent(this);
        colorBtn_->hide();
        layerBox_->setFixedWidth(kLayerNarrowTallW);
        // ---- Narrow-tall (vertical dock): four stacked rows, labels on top; the colour row
        // sets the width. ----
        auto onTop = [nc](const QString& caption, QWidget* w, const QString& capObjectName = QString()) {
            auto* v = new QVBoxLayout();
            v->setContentsMargins(0, 0, 0, 0);
            v->setSpacing(0);
            auto* cap = new QLabel(caption, nc);
            cap->setProperty(kCaptionProperty, true);
            cap->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
            if (!capObjectName.isEmpty()) cap->setObjectName(capObjectName);
            v->addWidget(cap);
            v->addWidget(w);
            return v;
        };
        auto tightRow = []() {
            auto* r = new QHBoxLayout();
            ui::applyRowMetrics(r, ui::kGapTight);
            return r;
        };

        // Row A: Track + Layer + channel; the "Track" caption keeps the click-target id.
        auto* rowA = tightRow();
        rowA->addLayout(onTop("Track", trackBox_, objPrefix_ + ".track.label"));
        rowA->addLayout(onTop("Layer", layerBox_));
        rowA->addLayout(onTop("Chan",  rgbaBtn_));
        rowA->addStretch(1);

        // Row B: Aspect + Flip/Flop + LUT (fills leftover width via its Ignored policy).
        auto* rowB = tightRow();
        rowB->addLayout(onTop("Aspect", aspectCrop_));
        rowB->addLayout(onTop(QString(), mirrorBox));
        rowB->addLayout(onTop(QString(), lutBox_), 1);

        // Row C: transforms.
        auto* rowC = tightRow();
        rowC->addLayout(onTop("Zoom", zoomSpin_));
        rowC->addLayout(onTop("X",    panXSpin_));
        rowC->addLayout(onTop("Y",    panYSpin_));
        rowC->addLayout(onTop("Rot",  rotSpin_));
        rowC->addStretch(1);

        // Row D: colour correction.
        auto* rowD = tightRow();
        rowD->addLayout(onTop("Gamma", gammaSpin_));
        rowD->addLayout(onTop("Exp",   exposureSpin_));
        rowD->addLayout(onTop("Con",   contrastSpin_));
        rowD->addLayout(onTop("Bri",   brightnessSpin_));
        rowD->addLayout(onTop("Sat",   saturationSpin_));
        rowD->addStretch(1);

        outer->addLayout(rowA);
        outer->addLayout(rowB);
        outer->addLayout(rowC);
        outer->addLayout(rowD);
    }

    // Swap content: build nc first (reparenting the controls into it) BEFORE deleting
    // the old content_, so the deletion can't take the shared controls with it.
    auto* top = layout();
    if (content_) {
        top->removeWidget(content_);
        // Hidden now: until the deferred delete runs it would still paint over the new content.
        content_->hide();
        content_->deleteLater();
    }
    top->addWidget(nc);
    content_ = nc;
    // Same swap for the popup content: the spins were already reparented out of it.
    if (popupContent_) {
        transformPopup_->layout()->removeWidget(popupContent_);
        popupContent_->hide();
        popupContent_->deleteLater();
    }
    popupContent_ = newPopupContent;
    if (popupContent_) transformPopup_->layout()->addWidget(popupContent_);
    if (colorPopupContent_) {
        colorPopup_->layout()->removeWidget(colorPopupContent_);
        colorPopupContent_->hide();
        colorPopupContent_->deleteLater();
    }
    colorPopupContent_ = newColorContent;
    if (colorPopupContent_) colorPopup_->layout()->addWidget(colorPopupContent_);

    // The card stays at its packed size and neither grows nor shrinks; the
    // Plate Manager left/top-aligns cards and scrolls the long axis.
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    adjustSize();
}

bool PlateCard_Qt::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Hide) {
        if (watched == transformPopup_) transformClosed_.start();
        else if (watched == colorPopup_) colorClosed_.start();
    }
    return QFrame::eventFilter(watched, event);
}

void PlateCard_Qt::openPopup(QFrame* popup, QToolButton* btn) {
    popup->adjustSize();
    const QSize pop = popup->sizeHint().expandedTo(popup->size());
    // Under the button; flip above when it would spill off the bottom, clamp to the screen.
    const QPoint anchor = btn->mapToGlobal(QPoint(0, btn->height()));
    QScreen* scr = btn->screen();
    if (!scr) scr = QGuiApplication::screenAt(anchor);
    const QRect avail = scr ? scr->availableGeometry() : QRect(anchor, pop);
    int x = anchor.x();
    int y = anchor.y();
    if (y + pop.height() > avail.bottom()) {
        const int above = btn->mapToGlobal(QPoint(0, 0)).y() - pop.height();
        y = above >= avail.top() ? above : avail.bottom() - pop.height();
    }
    if (x + pop.width() > avail.right()) x = avail.right() - pop.width();
    if (x < avail.left()) x = avail.left();
    popup->move(QPoint(x, y));
    popup->show();
}

void PlateCard_Qt::updateTransformButton(double scale) {
    const QString text = QStringLiteral("%1\u00d7").arg(scale, 0, 'f', 2);
    if (transformBtn_->text() != text) transformBtn_->setText(text);
}

void PlateCard_Qt::mousePressEvent(QMouseEvent* e) {
    // Child widgets handle their own events first; this fires only for the card background or a label.
    if (e->button() == Qt::LeftButton) {
        emit clicked(id_);
    }
    QFrame::mousePressEvent(e);
}

namespace {
// Sets a dynamic property the theme keys on and re-polishes `w` (and, with `children`,
// its labels, whose rules key on the card's property), only when the value changes, so
// a refresh on every viewport event stays free.
void setStyleProperty(QWidget* w, const char* name, bool on, bool children) {
    if (w->property(name).toBool() == on && w->property(name).isValid()) return;
    w->setProperty(name, on);
    w->style()->unpolish(w);
    w->style()->polish(w);
    if (children) {
        for (auto* lb : w->findChildren<QLabel*>()) {
            lb->style()->unpolish(lb);
            lb->style()->polish(lb);
        }
    }
    w->update();
}
}  // namespace

void PlateCard_Qt::setActiveHighlight(bool on) {
    // Border-only cue in the theme's orange (PlateCard_Qt[active="true"]).
    setStyleProperty(this, "active", on, /*children=*/false);
}

void PlateCard_Qt::setOffscreen(bool on) {
    // Dashed border and dimmed captions (PlateCard_Qt[offscreen="true"]).
    setStyleProperty(this, "offscreen", on, /*children=*/true);
}

bool PlateCard_Qt::isOffscreen() const {
    return property("offscreen").toBool();
}

void PlateCard_Qt::refreshFromState() {
    if (!gui_) return;

    // Read fresh state once up front; the cost being avoided is the widget-side setValue/
    // setText cascade through QAccessible + AppKit, which fires even for an unchanged value.
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

    // Block every widget's signals during the refresh, or programmatic setValue() loops
    // back into the setters and fights the user's edits.
    const QSignalBlocker bTrack(trackBox_);
    const QSignalBlocker bLayer(layerBox_);
    // aspectCrop_ has inherently signal-free setters (setCurrentAspect /
    // setCropChecked block the inner widget internally), so no blocker here.
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

    // Layer combo: always visible, a single "Main" entry until a multi-layer EXR adds its
    // sub-layer names (the loaders already name the default layer "Main").
    {
        std::vector<std::string> items = layers;
        // Defensive: any blank/legacy entry shows as "Main".
        for (auto& n : items) {
            if (n.empty()) n = "Main";
        }
        if (items.empty()) items.push_back("Main");

        // Only rebuild the item list when its shape changed — touching items
        // flickers the combo even with signals blocked.
        const bool listChanged = firstPass || (items != lastShown_.layers);
        if (listChanged) {
            layerBox_->clear();
            for (const auto& name : items) {
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
        lastShown_.layers = items;
        lastShown_.activeLayer = activeLayer;
    }

    if ((firstPass || aspect != lastShown_.aspect) && !aspect.isEmpty()) {
        aspectCrop_->setCurrentAspect(aspect);
    }

    // Show the loaded frame's true ratio next to "original" in the drop-down.
    // Cheap string compare inside the widget gates the work; safe per-refresh.
    aspectCrop_->setNativeAspectLabel(
        QString::fromStdString(jefe::qt::getPlateNativeAspect(id_)));

    if (firstPass || crop != lastShown_.crop) aspectCrop_->setCropChecked(crop);
    if (firstPass || flip != lastShown_.flip) flipBtn_->setChecked(flip);
    if (firstPass || flop != lastShown_.flop) flopBtn_->setChecked(flop);

    // RGB button label tracks the active channel mask. Matches the FLTK
    // single-button cycle: 0=RGB, 1=R, 2=G, 3=B (alpha lives elsewhere).
    static const char* kRgbaLabels[4] = {"RGB", "R", "G", "B"};
    if (firstPass || rgba != lastShown_.rgba) {
        rgbaBtn_->setText(kRgbaLabels[rgba]);
        rgbaBtn_->setChecked(rgba != 0);
    }

    if (firstPass || scale != lastShown_.scale) {
        zoomSpin_->setValue(scale);
        updateTransformButton(scale);
    }
    if (firstPass || tx    != lastShown_.tx)            panXSpin_->setValue(tx);
    if (firstPass || ty    != lastShown_.ty)            panYSpin_->setValue(ty);
    if (firstPass || rz    != lastShown_.rz)            rotSpin_->setValue(rz);

    // Rebuild the LUT combo if the gui's LUT list changed (it grows after the autoload);
    // comparing first avoids a flicker when the names already match.
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
    // Read only the four fields that change during a pan/zoom drag, with the same
    // delta-vs-cache gating as refreshFromState.
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

    if (scale != lastShown_.scale) {
        zoomSpin_->setValue(scale);
        updateTransformButton(scale);
        lastShown_.scale = scale;
    }
    if (tx    != lastShown_.tx)    { panXSpin_->setValue(tx);   lastShown_.tx    = tx; }
    if (ty    != lastShown_.ty)    { panYSpin_->setValue(ty);   lastShown_.ty    = ty; }
    if (rz    != lastShown_.rz)    { rotSpin_->setValue(rz);    lastShown_.rz    = rz; }
}

void PlateCard_Qt::refreshColorOnly() {
    if (!gui_) return;
    // Mirror of refreshTransformOnly for the five color-correction spinboxes: writes only
    // the changed fields, signals blocked, during a W/E/Q/D/S drag.
    const float gamma      = gui_->getGamma();
    const float exposure   = gui_->getExposure();
    const float contrast   = gui_->getContrast();
    const float brightness = gui_->getBrightness();
    const float saturation = gui_->getSaturation();

    if (lastShown_.valid
        && gamma == lastShown_.gamma
        && exposure == lastShown_.exposure
        && contrast == lastShown_.contrast
        && brightness == lastShown_.brightness
        && saturation == lastShown_.saturation) {
        return;
    }

    const QSignalBlocker bGamma(gammaSpin_);
    const QSignalBlocker bExposure(exposureSpin_);
    const QSignalBlocker bContrast(contrastSpin_);
    const QSignalBlocker bBrightness(brightnessSpin_);
    const QSignalBlocker bSaturation(saturationSpin_);

    if (gamma      != lastShown_.gamma)      { gammaSpin_->setValue(gamma);           lastShown_.gamma      = gamma; }
    if (exposure   != lastShown_.exposure)   { exposureSpin_->setValue(exposure);     lastShown_.exposure   = exposure; }
    if (contrast   != lastShown_.contrast)   { contrastSpin_->setValue(contrast);     lastShown_.contrast   = contrast; }
    if (brightness != lastShown_.brightness) { brightnessSpin_->setValue(brightness); lastShown_.brightness = brightness; }
    if (saturation != lastShown_.saturation) { saturationSpin_->setValue(saturation); lastShown_.saturation = saturation; }
}

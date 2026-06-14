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
#include <QPixmap>
#include <QPointF>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSize>
#include <QVBoxLayout>

namespace {
// One height for every control on the card (combos, spinboxes, buttons) so
// rows line up exactly. Combos enforce a min height from their padding, so
// the card stylesheet trims that padding to let everything sit at this
// compact height under labels-on-top.
constexpr int kCtrlHeight = 22;

QDoubleSpinBox* makeSpin(QWidget* parent, double min, double max, double step,
                         double initial, int width) {
    auto* s = new QDoubleSpinBox(parent);
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setValue(initial);
    s->setDecimals(step < 1.0 ? 2 : 0);
    s->setFixedWidth(width);
    s->setFixedHeight(kCtrlHeight);
    s->setAlignment(Qt::AlignRight);
    s->setButtonSymbols(QAbstractSpinBox::NoButtons);
    return s;
}

// A "label on top" cell: a small gray caption above the control, returned
// as a QVBoxLayout the caller adds to the row. An empty caption still
// reserves the caption's height, so captionless controls (Flip/Flop, LUT)
// bottom-align with the labeled ones. `capObjectName`, when set, names the
// caption QLabel — used so the "Track" caption can serve as the plate-
// activation click target (a QLabel doesn't consume the press, so it
// propagates to PlateCard_Qt::mousePressEvent → clicked()).
QVBoxLayout* labeledCell(QWidget* parent, const QString& caption, QWidget* w,
                         const QString& capObjectName = QString()) {
    auto* v = new QVBoxLayout();
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(1);
    auto* cap = new QLabel(caption, parent);
    cap->setStyleSheet("color: #888; font-size: 8pt;");
    cap->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    if (!capObjectName.isEmpty()) cap->setObjectName(capObjectName);
    v->addWidget(cap);
    v->addWidget(w);
    return v;
}

// Draw a double-headed mirror arrow into a 2x-dpr pixmap, tinted `color`.
// vertical=true → up/down arrow (Flip, mirrors top-bottom); false → left/
// right arrow (Flop, mirrors left-right). Logical canvas is 14x14.
QPixmap makeMirrorPixmap(bool vertical, const QColor& color) {
    QPixmap pm(28, 28);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
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
    p.end();
    return pm;
}

// QIcon with both check states: light glyph when unchecked (dark bg), dark
// glyph when checked (the theme's orange QPushButton:checked background).
QIcon makeMirrorIcon(bool vertical) {
    QIcon icon;
    icon.addPixmap(makeMirrorPixmap(vertical, QColor(0xe0, 0xe0, 0xe0)),
                   QIcon::Normal, QIcon::Off);
    icon.addPixmap(makeMirrorPixmap(vertical, QColor(0x1a, 0x1a, 0x1a)),
                   QIcon::Normal, QIcon::On);
    return icon;
}

// Compact icon-only toggle button (Flip/Flop). Tight padding so the card row
// stays narrow; fixed size keeps the 14px glyph from being clipped by the
// theme's default button padding.
QPushButton* makeIconToggle(QWidget* parent, const QIcon& icon,
                            const QString& tip) {
    auto* b = new QPushButton(parent);
    b->setCheckable(true);
    b->setIcon(icon);
    b->setIconSize(QSize(14, 14));
    b->setToolTip(tip);
    b->setFixedSize(24, kCtrlHeight);
    b->setStyleSheet("QPushButton { padding: 1px; }");
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
    // No artificial minimum: let the layout's own minimumSizeHint govern so
    // the card never shrinks below its tightly-packed contents. The Plate
    // Manager's scroll area then shows a horizontal scrollbar when the dock
    // is narrower, instead of squashing fixed-width children into overlap.

    setStyleSheet(
        "QLabel, QPushButton, QSpinBox, QDoubleSpinBox, QComboBox, "
        "QComboBox QAbstractItemView, QAbstractSpinBox { font-size: 10pt; }"
        // Trim the theme's padding so combos/spinboxes fit the compact
        // kCtrlHeight row and pack tightly under labels-on-top.
        "QComboBox { padding: 1px 16px 1px 4px; }"
        "QAbstractSpinBox { padding: 1px 2px; }"
    );

    // Plate-card object names follow `plate.<idx>.<role>` so UI tests can
    // target a specific plate without depending on tab order. Setting
    // setAccessibleName as well so the AX label matches the on-screen text.
    objPrefix_ = QStringLiteral("plate.%1").arg(id);
    setObjectName(objPrefix_ + ".card");
    setAccessibleName(QStringLiteral("Plate %1").arg(id + 1));

    trackBox_ = new QComboBox(this);
    trackBox_->addItems({"A", "B", "C", "D"});
    trackBox_->setCurrentIndex(gui_->getSequenceID() >= 0 ? gui_->getSequenceID() : id);
    // Wide enough to fit the single glyph clear of the theme's 22px
    // dropdown-arrow padding (40px clipped it).
    trackBox_->setFixedWidth(56);  // room for the letter clear of the arrow
    trackBox_->setFixedHeight(kCtrlHeight);
    trackBox_->setObjectName(objPrefix_ + ".track.combo");
    trackBox_->setAccessibleName("Track");

    // Layer picker. Lives between Track and Aspect so it reads as "this
    // track, this layer, this aspect". Always visible: it shows "Main"
    // by default (the file's primary channels) and adds the EXR sub-layers
    // (e.g. "right", "diffuse") when a multi-layer file is loaded.
    layerBox_ = new QComboBox(this);
    // Fixed width — wide enough for "Main" and typical layer names; longer
    // names elide. (The card no longer grows, so nothing expands here.)
    layerBox_->setFixedWidth(92);
    layerBox_->setFixedHeight(kCtrlHeight);
    layerBox_->setObjectName(objPrefix_ + ".layer.combo");
    layerBox_->setAccessibleName("Layer");
    layerBox_->setToolTip("Image layer / channel group driving this plate");

    // Aspect ratio control with the letterbox toggle folded into its popup.
    // The checkbox keeps the old `crop.button` leaf objectName so existing
    // UI-test locators resolve it once the popup is open; its visible/AX
    // label is "Letterbox".
    aspectCrop_ = new AspectCropCombo_Qt(this);
    aspectCrop_->setPresets({"original", "16:9", "4:3", "2.39:1", "2.35:1", "1.85:1", "1.37:1"});
    // Fixed, just wide enough for "source" (the default label). Layer takes
    // the row's free space instead.
    aspectCrop_->setFixedWidth(54);
    aspectCrop_->setFixedHeight(kCtrlHeight);  // match the combos/buttons
    aspectCrop_->setObjectName(objPrefix_ + ".aspect.combo");
    aspectCrop_->setAccessibleName("Aspect ratio");
    aspectCrop_->setCropObjectName(objPrefix_ + ".crop.button");
    aspectCrop_->setCropAccessibleName("Letterbox");
    aspectCrop_->setCropToolTip("Letterbox to the selected aspect ratio (black bars)");

    // Icon-only toggles: Flip = vertical mirror (up/down arrow), Flop =
    // horizontal mirror (left/right arrow). accessibleName stays "Flip"/
    // "Flop" so AX titles and UI-test locators are unchanged.
    flipBtn_ = makeIconToggle(this, makeMirrorIcon(/*vertical=*/true),
                              "Flip vertically (mirror top–bottom)");
    flipBtn_->setObjectName(objPrefix_ + ".flip.button");
    flipBtn_->setAccessibleName("Flip");
    flopBtn_ = makeIconToggle(this, makeMirrorIcon(/*vertical=*/false),
                              "Flop horizontally (mirror left–right)");
    flopBtn_->setObjectName(objPrefix_ + ".flop.button");
    flopBtn_->setAccessibleName("Flop");

    rgbaBtn_ = new QPushButton("RGB", this);
    rgbaBtn_->setCheckable(true);
    rgbaBtn_->setToolTip("Cycle RGBA channel display (shortcuts r/g/b/a)");
    // Fixed size with tight padding so the button doesn't jitter as its
    // label cycles RGB → R → G → B (different glyph widths). 40px clears
    // the widest label ("RGB") with the reduced padding.
    rgbaBtn_->setFixedSize(36, kCtrlHeight);
    rgbaBtn_->setStyleSheet("QPushButton { padding: 1px 4px; }");
    rgbaBtn_->setObjectName(objPrefix_ + ".rgba.button");
    rgbaBtn_->setAccessibleName("RGBA channel");

    // Narrow fields — sized to fit "00.00" (theme adds 6px padding + 1px
    // border each side). Pan/rotation get a touch more for the sign / 3rd
    // digit ("-360.00").
    // Snug fields sized for "00.00" (kNarrowSpin); Pan X/Y get the wider
    // width (pixel offsets run 3–4 digits); rotation fits "360.00".
    constexpr int kNarrowSpin = 38;  // Zoom + color fields — fits "00.00"/"-9.99"
    constexpr int kWideSpin   = 40;  // Pan X/Y — fits "0000" / "-999"
    constexpr int kRotSpin    = 48;
    zoomSpin_ = makeSpin(this, 0.01,    1.0e6, 0.01, 1.0, kNarrowSpin);
    zoomSpin_->setObjectName(objPrefix_ + ".zoom.spin");
    zoomSpin_->setAccessibleName("Zoom");
    panXSpin_ = makeSpin(this, -1.0e6, 1.0e6, 1.0,  0.0, kWideSpin);
    panXSpin_->setObjectName(objPrefix_ + ".panx.spin");
    panXSpin_->setAccessibleName("Pan X");
    panYSpin_ = makeSpin(this, -1.0e6, 1.0e6, 1.0,  0.0, kWideSpin);
    panYSpin_->setObjectName(objPrefix_ + ".pany.spin");
    panYSpin_->setAccessibleName("Pan Y");
    rotSpin_  = makeSpin(this, -360.0,  360.0,  0.01, 0.0, kRotSpin);
    rotSpin_->setObjectName(objPrefix_ + ".rotation.spin");
    rotSpin_->setAccessibleName("Rotation");

    lutBox_ = new QComboBox(this);
    lutBox_->addItem("No LUT");
    // Ignored width policy: a QComboBox normally sizes to its longest item,
    // which would balloon the card once LUTs autoload. Ignored makes the
    // combo's sizeHint irrelevant — it simply fills the space row 2 has
    // left over (the card width is set by the wider transforms row), so the
    // LUT never extends past row 1's right edge. The popup still shows full
    // names; long current selections elide. Min keeps it usable.
    lutBox_->setMinimumWidth(50);
    lutBox_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    lutBox_->setFixedHeight(kCtrlHeight);
    lutBox_->setObjectName(objPrefix_ + ".lut.combo");
    lutBox_->setAccessibleName("LUT");

    // Color controls cap at 9.99 (the useful range for grade tweaks).
    // gamma/contrast/saturation are non-negative; exposure/brightness are
    // bipolar (±9.99). Two-decimal display.
    gammaSpin_      = makeSpin(this, 0.0,   9.99, 0.01, 1.0, kNarrowSpin);
    gammaSpin_->setObjectName(objPrefix_ + ".gamma.spin");
    gammaSpin_->setAccessibleName("Gamma");
    exposureSpin_   = makeSpin(this, -9.99, 9.99, 0.01, 0.0, kNarrowSpin);
    exposureSpin_->setObjectName(objPrefix_ + ".exposure.spin");
    exposureSpin_->setAccessibleName("Exposure");
    contrastSpin_   = makeSpin(this, 0.0,   9.99, 0.01, 1.0, kNarrowSpin);
    contrastSpin_->setObjectName(objPrefix_ + ".contrast.spin");
    contrastSpin_->setAccessibleName("Contrast");
    brightnessSpin_ = makeSpin(this, -9.99, 9.99, 0.01, 0.0, kNarrowSpin);
    brightnessSpin_->setObjectName(objPrefix_ + ".brightness.spin");
    brightnessSpin_->setAccessibleName("Brightness");
    saturationSpin_ = makeSpin(this, 0.0,   9.99, 0.01, 1.0, kNarrowSpin);
    saturationSpin_->setObjectName(objPrefix_ + ".saturation.spin");
    saturationSpin_->setAccessibleName("Saturation");

    // Top-level layout: one swappable content_ child holding the caption
    // labels and per-row layouts. Created once here; rebuildContent() builds
    // (and on later setVertical calls rebuilds) it. Keeping the row assembly
    // in rebuildContent lets the wide-short and narrow-tall forms share one
    // code path while the control widgets are simply reparented across swaps.
    auto* top = new QVBoxLayout(this);
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(0);
    rebuildContent();

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
    connect(aspectCrop_, &AspectCropCombo_Qt::aspectChanged,
            this, [g](const QString& s) {
        g->setAspectChoice(s.toStdString());
        jefe::qt::propagatePlateChanges();
    });

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

void PlateCard_Qt::setVertical(bool vertical) {
    if (vertical == vertical_) return;
    vertical_ = vertical;
    rebuildContent();
}

void PlateCard_Qt::rebuildContent() {
    // Build the new content into a fresh child widget. Adding the shared
    // control widgets to nc's layouts reparents them into nc — intended:
    // the members survive (and their signal connections persist across
    // reparenting); only the caption QLabels and layout containers are nc's
    // own children, so deleting the old content can't strand a caption.
    auto* nc = new QWidget(this);

    if (!vertical_) {
        // ---- Wide-short (horizontal dock): two rows. ----
        // Flip/Flop share one cell (a blank caption keeps them bottom-
        // aligned with the labeled fields). A fresh mirrorBox parented to
        // nc each rebuild so it dies with the old content.
        auto* mirrorBox = new QWidget(nc);
        auto* mirrorLay = new QHBoxLayout(mirrorBox);
        mirrorLay->setContentsMargins(0, 0, 0, 0);
        mirrorLay->setSpacing(1);
        mirrorLay->addWidget(flipBtn_);
        mirrorLay->addWidget(flopBtn_);

        // Transform group, packed tight (small spacing) so the four fields
        // read as one cluster and free width for the dropdowns.
        auto* xformGroup = new QHBoxLayout();
        xformGroup->setSpacing(1);
        xformGroup->addLayout(labeledCell(nc, "Zoom", zoomSpin_));
        xformGroup->addLayout(labeledCell(nc, "X",    panXSpin_));
        xformGroup->addLayout(labeledCell(nc, "Y",    panYSpin_));
        xformGroup->addLayout(labeledCell(nc, "Rot",  rotSpin_));

        // Row 1 — Track (fixed) + Layer/Aspect + packed transforms. The
        // "Track" caption carries the `track.label` objectName so it's the
        // plate-activation click target. Flip/Flop moved to row 2 so Layer/
        // Aspect don't clip.
        auto* row1 = new QHBoxLayout();
        row1->setSpacing(2);
        row1->addLayout(labeledCell(nc, "Track",  trackBox_, objPrefix_ + ".track.label"));
        row1->addLayout(labeledCell(nc, "Layer",  layerBox_));
        row1->addLayout(labeledCell(nc, "Aspect", aspectCrop_));
        row1->addLayout(xformGroup);
        row1->addStretch(1);  // absorb the width difference vs the (wider) row 2

        // Color-correction group, packed tight so the five fields read as
        // one cluster.
        auto* colorGroup = new QHBoxLayout();
        colorGroup->setSpacing(1);
        colorGroup->addLayout(labeledCell(nc, "Gamma", gammaSpin_));
        colorGroup->addLayout(labeledCell(nc, "Exp",   exposureSpin_));
        colorGroup->addLayout(labeledCell(nc, "Con",   contrastSpin_));
        colorGroup->addLayout(labeledCell(nc, "Bri",   brightnessSpin_));
        colorGroup->addLayout(labeledCell(nc, "Sat",   saturationSpin_));

        // Row 2 — channel, packed color group, Flip/Flop, then LUT.
        auto* row2 = new QHBoxLayout();
        row2->setSpacing(2);
        row2->addLayout(labeledCell(nc, "Chan", rgbaBtn_));
        row2->addLayout(colorGroup);
        row2->addLayout(labeledCell(nc, QString(), mirrorBox));  // Flip/Flop
        // LUT picker fills the row's leftover width via its Ignored size
        // policy; no trailing stretch so it reaches the card's right edge.
        row2->addLayout(labeledCell(nc, QString(), lutBox_), 1);

        auto* outer = new QVBoxLayout(nc);
        outer->setContentsMargins(4, 2, 4, 2);
        outer->setSpacing(2);
        outer->addLayout(row1);
        outer->addLayout(row2);
    } else {
        // ---- Narrow-tall (vertical dock): five stacked rows, ~200px. ----
        // The color row (row D, five fields) sets the card's width. Each row
        // is a QHBoxLayout of cells with a trailing stretch so cells pack
        // left; row E lets the LUT fill instead.
        auto* mirrorBox = new QWidget(nc);
        auto* mirrorLay = new QHBoxLayout(mirrorBox);
        mirrorLay->setContentsMargins(0, 0, 0, 0);
        mirrorLay->setSpacing(1);
        mirrorLay->addWidget(flipBtn_);
        mirrorLay->addWidget(flopBtn_);

        // Row A — Track + Layer + channel. "Track" caption keeps the
        // click-target id. Channel rides here (not row B) to leave row B's
        // width for the LUT.
        auto* rowA = new QHBoxLayout();
        rowA->setSpacing(2);
        rowA->addLayout(labeledCell(nc, "Track", trackBox_, objPrefix_ + ".track.label"));
        rowA->addLayout(labeledCell(nc, "Layer", layerBox_));
        rowA->addLayout(labeledCell(nc, "Chan",  rgbaBtn_));
        rowA->addStretch(1);

        // Row B — Aspect + Flip/Flop mirror + LUT (fills the leftover width
        // via its Ignored size policy). Folding the LUT in here keeps the
        // narrow card to four rows instead of five.
        auto* rowB = new QHBoxLayout();
        rowB->setSpacing(2);
        rowB->addLayout(labeledCell(nc, "Aspect", aspectCrop_));
        rowB->addLayout(labeledCell(nc, QString(), mirrorBox));
        rowB->addLayout(labeledCell(nc, QString(), lutBox_), 1);

        // Row C — transforms, packed tight.
        auto* rowC = new QHBoxLayout();
        rowC->setSpacing(1);
        rowC->addLayout(labeledCell(nc, "Zoom", zoomSpin_));
        rowC->addLayout(labeledCell(nc, "X",    panXSpin_));
        rowC->addLayout(labeledCell(nc, "Y",    panYSpin_));
        rowC->addLayout(labeledCell(nc, "Rot",  rotSpin_));
        rowC->addStretch(1);

        // Row D — color correction, packed tight. Five fields ≈ 200px set
        // the card's width.
        auto* rowD = new QHBoxLayout();
        rowD->setSpacing(1);
        rowD->addLayout(labeledCell(nc, "Gamma", gammaSpin_));
        rowD->addLayout(labeledCell(nc, "Exp",   exposureSpin_));
        rowD->addLayout(labeledCell(nc, "Con",   contrastSpin_));
        rowD->addLayout(labeledCell(nc, "Bri",   brightnessSpin_));
        rowD->addLayout(labeledCell(nc, "Sat",   saturationSpin_));
        rowD->addStretch(1);

        auto* outer = new QVBoxLayout(nc);
        outer->setContentsMargins(4, 2, 4, 2);
        outer->setSpacing(2);
        outer->addLayout(rowA);
        outer->addLayout(rowB);
        outer->addLayout(rowC);
        outer->addLayout(rowD);
    }

    // Swap content. Build nc first (reparenting the controls into it) BEFORE
    // deleting the old content_, so the deletion can't take the shared
    // control widgets with it.
    auto* top = layout();
    if (content_) {
        top->removeWidget(content_);
        content_->deleteLater();
    }
    top->addWidget(nc);
    content_ = nc;

    // The card stays at its packed size and neither grows nor shrinks; the
    // Plate Manager left/top-aligns cards and scrolls the long axis.
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    adjustSize();
}

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

    // Layer combo: always visible. Defaults to a single "Main" entry
    // when the plate has no loaded layers yet (or a non-EXR/single-layer
    // file); multi-layer EXRs add their discovered sub-layer names. The
    // OIIO/DPX loaders already name the default layer "Main", so a
    // loaded plate's first entry matches this default with no special-case.
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

void PlateCard_Qt::refreshColorOnly() {
    if (!gui_) return;
    // Mirror of refreshTransformOnly for the five color-correction
    // spinboxes. Reads the GUI's current values, short-circuits when
    // none changed against the cache, then writes only the changed
    // fields with signals blocked. Saves the full refreshFromState
    // walk (13+ widget writes across track/aspect/layer/RGBA/LUT/etc.)
    // during a W/E/Q/D/S drag.
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

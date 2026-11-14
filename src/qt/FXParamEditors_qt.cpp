// SPDX-License-Identifier: GPL-2.0-or-later
// FX float parameter editors. See FXParamEditors_qt.h.
#include "FXParamEditors_qt.h"
#include "UiDensityCheck_qt.h"
#include "UiMetrics_qt.h"
#include "qticons.h"

#include <QAction>
#include <QCoreApplication>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace ui = jefe::qt::ui;

namespace jefe::qt {

int decimalsForStep(float step) {
    if (step <= 0.0f) return 3;
    const float abs_step = std::fabs(step);
    if (abs_step >= 1.0f) return 0;
    int d = 1;
    float s = abs_step;
    while (s < 0.1f && d < 6) { s *= 10.0f; ++d; }
    return d;
}

}  // namespace jefe::qt

namespace {

// Continuous drags emit at most this often (developer_notes.md §5d); the final
// value is always emitted on release.
constexpr qint64 kEmitIntervalMs = 16;
// Knob dial side: one control plus a gap, so it reads larger than a field.
constexpr int kKnobPx = ui::kControlH + ui::kGap;
// Slider: horizontal room either side of the track for the handle.
constexpr qreal kHandleR = 5.0;

const QColor kTrackColor(0x3d, 0x3d, 0x3d);
const QColor kFillColor(0x9a, 0x9a, 0x9a);
const QColor kHandleColor(0xdc, 0xdc, 0xdc);
const QColor kAccent(jefe::qt::kFXModifiedColor);

float clampTo(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }

// Emits through `emitFn` when the value moved since the last emit, at most every
// kEmitIntervalMs unless forced.
struct EmitGate {
    QElapsedTimer timer;
    float last = 0.f;
    void start(float v) { last = v; timer.invalidate(); }
    template <class Fn>
    void offer(float v, bool force, Fn&& emitFn) {
        if (v == last) return;
        if (!force && timer.isValid() && timer.elapsed() < kEmitIntervalMs) return;
        last = v;
        timer.restart();
        emitFn(v);
    }
};

// Drawn slider over the soft range. A value outside it pins the handle at that
// end; the value itself is untouched until the user drags.
class FXSoftSlider : public QWidget {
public:
    FXSoftSlider(const FXEditorSpec& spec, QWidget* parent) : QWidget(parent), spec_(spec) {
        setProperty(ui::kDensityExemptProperty, true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(ui::kControlH);
        setMinimumWidth(2 * ui::kControlH);
        setCursor(Qt::PointingHandCursor);
        setContextMenuPolicy(Qt::NoContextMenu);
    }
    std::function<void(float)> onEdited;

    void setValue(float v, bool modified) {
        if (dragging_) return;
        if (v == value_ && modified == modified_) return;
        value_ = v;
        modified_ = modified;
        update();
    }
    bool isDragging() const { return dragging_; }
    float handlePos() const { return fxui::toNormalized(value_, {spec_.softMin, spec_.softMax}); }
    QSize sizeHint() const override { return {4 * ui::kControlH, ui::kControlH}; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal y = height() / 2.0;
        const qreal x0 = kHandleR + 1, x1 = width() - kHandleR - 1;
        const qreal hx = x0 + handlePos() * (x1 - x0);
        p.setPen(QPen(kTrackColor, 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(x0, y), QPointF(x1, y));
        p.setPen(QPen(modified_ ? kAccent : kFillColor, 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(x0, y), QPointF(hx, y));
        p.setPen(Qt::NoPen);
        p.setBrush(dragging_ ? kAccent : kHandleColor);
        p.drawEllipse(QPointF(hx, y), kHandleR, kHandleR);
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return QWidget::mousePressEvent(e);
        dragging_ = true;
        gate_.start(value_);
        moveTo(e->position().x(), false);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (dragging_) moveTo(e->position().x(), false);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (!dragging_) return QWidget::mouseReleaseEvent(e);
        moveTo(e->position().x(), true);
        dragging_ = false;
        update();
    }

private:
    void moveTo(qreal x, bool force) {
        const qreal x0 = kHandleR + 1, x1 = width() - kHandleR - 1;
        const float t = x1 > x0 ? float((x - x0) / (x1 - x0)) : 0.f;
        value_ = fxui::fromNormalized(t, {spec_.softMin, spec_.softMax}, spec_.hardMin, spec_.hardMax, spec_.step);
        update();
        gate_.offer(value_, force, [this](float v) { if (onEdited) onEdited(v); });
    }

    FXEditorSpec spec_;
    float value_ = 0.f;
    bool modified_ = false;
    bool dragging_ = false;
    EmitGate gate_;
};

// Drawn rotary knob: a 270-degree arc over the soft range. Vertical drag turns
// it through fxui::scrub (up = positive, Shift = fine).
class FXKnobDial : public QWidget {
public:
    FXKnobDial(const FXEditorSpec& spec, QWidget* parent) : QWidget(parent), spec_(spec) {
        setProperty(ui::kDensityExemptProperty, true);
        setFixedSize(kKnobPx, kKnobPx);
        setCursor(Qt::SizeVerCursor);
        setContextMenuPolicy(Qt::NoContextMenu);
    }
    std::function<void(float)> onEdited;
    std::function<void()> onDoubleClick;

    void setValue(float v, bool modified) {
        if (dragging_) return;
        if (v == value_ && modified == modified_) return;
        value_ = v;
        modified_ = modified;
        update();
    }
    bool isDragging() const { return dragging_; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal m = 3.5;
        const QRectF r(m, m, width() - 2 * m, height() - 2 * m);
        const float t = fxui::toNormalized(value_, {spec_.softMin, spec_.softMax});
        constexpr int kStart = 225 * 16, kSpan = -270 * 16;  // Qt angles: CCW from 3 o'clock
        p.setPen(QPen(kTrackColor, 3, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(r, kStart, kSpan);
        p.setPen(QPen(modified_ || dragging_ ? kAccent : kFillColor, 3, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(r, kStart, int(kSpan * t));
        const qreal a = (225.0 - 270.0 * t) * M_PI / 180.0;
        const QPointF c = r.center();
        const qreal len = r.width() / 2.0 - 4;
        p.setPen(QPen(kHandleColor, 2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(c, c + QPointF(len * std::cos(a), -len * std::sin(a)));
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return QWidget::mousePressEvent(e);
        dragging_ = true;
        fine_ = e->modifiers() & Qt::ShiftModifier;
        startValue_ = value_;
        startY_ = lastY_ = e->globalPosition().y();
        gate_.start(value_);
        update();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!dragging_) return;
        turnTo(e->globalPosition().y(), e->modifiers() & Qt::ShiftModifier, false);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (!dragging_) return QWidget::mouseReleaseEvent(e);
        turnTo(e->globalPosition().y(), e->modifiers() & Qt::ShiftModifier, true);
        dragging_ = false;
        update();
    }
    void mouseDoubleClickEvent(QMouseEvent*) override {
        dragging_ = false;
        if (onDoubleClick) onDoubleClick();
    }

private:
    void turnTo(qreal y, bool fine, bool force) {
        if (fine != fine_) {  // re-base so switching speed does not jump the value
            startValue_ = value_;
            startY_ = lastY_;
            fine_ = fine;
        }
        lastY_ = y;
        value_ = fxui::scrub(startValue_, float(startY_ - y), {spec_.softMin, spec_.softMax}, spec_.hardMin,
                             spec_.hardMax, spec_.step, fine_);
        update();
        gate_.offer(value_, force, [this](float v) { if (onEdited) onEdited(v); });
    }

    FXEditorSpec spec_;
    float value_ = 0.f;
    float startValue_ = 0.f;
    qreal startY_ = 0., lastY_ = 0.;
    bool modified_ = false;
    bool dragging_ = false;
    bool fine_ = false;
    EmitGate gate_;
};

// Test access to the slider handle (normalised 0..1); -1 when w is not a slider.
float sliderHandlePosition(QWidget* w) {
    auto* s = dynamic_cast<FXSoftSlider*>(w);
    return s ? s->handlePos() : -1.f;
}

const QString kResetUnchanged = QString(QChar(0x25CB));  // ○
const QString kResetModified = QString(QChar(0x25CF));   // ●

}  // namespace

// ---------------------------------------------------------------------------
// FXScrubField_Qt
// ---------------------------------------------------------------------------

FXScrubField_Qt::FXScrubField_Qt(QWidget* parent) : QLineEdit(parent) {
    setReadOnly(true);
    setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setCursor(Qt::SizeHorCursor);
    setContextMenuPolicy(Qt::NoContextMenu);  // the editor's Reset menu handles it
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void FXScrubField_Qt::setRange(fxui::Range soft, float hardMin, float hardMax, float step) {
    soft_ = soft;
    hardMin_ = hardMin;
    hardMax_ = hardMax;
    step_ = step;
    updateGeometry();
    showValue();
}

QString FXScrubField_Qt::format(float v) const {
    return QString::number(double(v), 'f', jefe::qt::decimalsForStep(step_));
}

QSize FXScrubField_Qt::sizeHint() const {
    const QFontMetrics fm(font());
    int w = 0;
    for (float v : {soft_.lo, soft_.hi, value_}) w = std::max(w, fm.horizontalAdvance(format(v)));
    w = std::max(w, fm.horizontalAdvance(QStringLiteral("-0.000")));
    // QSS: 7 px padding plus a 1 px border each side, and a little air.
    return {w + 2 * (7 + 1) + ui::kGapTight, QLineEdit::sizeHint().height()};
}

QSize FXScrubField_Qt::minimumSizeHint() const { return sizeHint(); }

void FXScrubField_Qt::setDisplayValue(float v) {
    if (isInteracting()) return;
    value_ = v;
    showValue();
}

void FXScrubField_Qt::showValue() {
    const QString t = format(value_);
    if (t == text()) return;  // cache-gated write (developer_notes.md §5c)
    setText(t);
    setCursorPosition(0);
}

void FXScrubField_Qt::beginEdit() {
    dragging_ = false;
    editing_ = true;
    setReadOnly(false);
    setCursor(Qt::IBeamCursor);
    setFocus(Qt::MouseFocusReason);
    selectAll();
}

void FXScrubField_Qt::endEdit() {
    editing_ = false;
    setReadOnly(true);
    deselect();
    setCursor(Qt::SizeHorCursor);
}

void FXScrubField_Qt::commitEdit() {
    // QLocale::c(): '.' is the decimal point whatever the system locale says.
    bool ok = false;
    float v = QLocale::c().toFloat(text().trimmed(), &ok);
    endEdit();
    if (!ok || !std::isfinite(v)) {
        showValue();
        return;
    }
    v = clampTo(v, hardMin_, hardMax_);
    v = clampTo(fxui::quantise(v, step_), hardMin_, hardMax_);
    value_ = v;
    lastEmitted_ = v;
    showValue();
    emit valueEdited(v);
}

void FXScrubField_Qt::cancelEdit() {
    endEdit();
    showValue();
}

void FXScrubField_Qt::emitIfChanged(bool force) {
    if (value_ == lastEmitted_) return;
    if (!force && emitTimer_.isValid() && emitTimer_.elapsed() < kEmitIntervalMs) return;
    lastEmitted_ = value_;
    emitTimer_.restart();
    emit valueEdited(value_);
}

void FXScrubField_Qt::mousePressEvent(QMouseEvent* e) {
    if (editing_ || e->button() != Qt::LeftButton) return QLineEdit::mousePressEvent(e);
    dragging_ = true;
    fine_ = e->modifiers() & Qt::ShiftModifier;
    dragStartValue_ = value_;
    dragStartX_ = lastX_ = e->globalPosition().x();
    lastEmitted_ = value_;
    emitTimer_.invalidate();
    e->accept();
}

void FXScrubField_Qt::mouseMoveEvent(QMouseEvent* e) {
    if (editing_) return QLineEdit::mouseMoveEvent(e);
    if (!dragging_) return;
    const bool fine = e->modifiers() & Qt::ShiftModifier;
    if (fine != fine_) {  // re-base so switching speed does not jump the value
        dragStartValue_ = value_;
        dragStartX_ = lastX_;
        fine_ = fine;
    }
    const qreal x = e->globalPosition().x();
    lastX_ = x;
    value_ = fxui::scrub(dragStartValue_, float(x - dragStartX_), soft_, hardMin_, hardMax_, step_, fine_);
    showValue();
    emitIfChanged(false);
}

void FXScrubField_Qt::mouseReleaseEvent(QMouseEvent* e) {
    if (!dragging_) return QLineEdit::mouseReleaseEvent(e);
    dragging_ = false;
    emitIfChanged(true);
}

void FXScrubField_Qt::mouseDoubleClickEvent(QMouseEvent* e) {
    if (editing_) return QLineEdit::mouseDoubleClickEvent(e);
    beginEdit();
}

void FXScrubField_Qt::keyPressEvent(QKeyEvent* e) {
    if (editing_) {
        switch (e->key()) {
            case Qt::Key_Return:
            case Qt::Key_Enter: commitEdit(); return;
            case Qt::Key_Escape: cancelEdit(); return;
            default: break;
        }
    }
    QLineEdit::keyPressEvent(e);
}

void FXScrubField_Qt::focusOutEvent(QFocusEvent* e) {
    QLineEdit::focusOutEvent(e);
    if (editing_) commitEdit();
}

// ---------------------------------------------------------------------------
// FXParamEditor_Qt
// ---------------------------------------------------------------------------

FXParamEditor_Qt::FXParamEditor_Qt(const FXEditorSpec& spec, QWidget* parent)
    : QWidget(parent), spec_(spec), value_(spec.defaultValue) {
    setObjectName(spec_.objectBase);
    setToolTip(spec_.tooltip);
    label_ = new QLabel(spec_.label, this);
    label_->setObjectName(spec_.objectBase + ".label");
    field_ = new FXScrubField_Qt(this);
    field_->setObjectName(spec_.objectBase + ".field");
    field_->setToolTip(spec_.tooltip);
    field_->setAccessibleName(spec_.label);
    field_->setRange(soft(), spec_.hardMin, spec_.hardMax, spec_.step);
    connect(field_, &FXScrubField_Qt::valueEdited, this, &FXParamEditor_Qt::edited);
    attachResetMenu(this, [this] { edited(spec_.defaultValue); });
}

void FXParamEditor_Qt::setValue(float v) {
    if (isInteracting()) return;
    value_ = v;
    refresh();
}

bool FXParamEditor_Qt::isInteracting() const { return field_->isInteracting(); }

bool FXParamEditor_Qt::isModified() const { return fxui::isModified(value_, spec_.defaultValue, spec_.step); }

void FXParamEditor_Qt::edited(float v) {
    value_ = v;
    refresh();
    emit valueEdited(v);
}

void FXParamEditor_Qt::refresh() {
    field_->setDisplayValue(value_);
    applyLabelColour();
    refreshView();
}

void FXParamEditor_Qt::applyLabelColour() {
    const int state = isModified() ? 1 : 0;
    if (state == labelState_) return;
    labelState_ = state;
    if (state == 1)
        label_->setStyleSheet(QString("color: %1;").arg(jefe::qt::kFXModifiedColor));
    else if (spec_.labelColor.isValid())
        label_->setStyleSheet(QString("color: %1;").arg(spec_.labelColor.name()));
    else
        label_->setStyleSheet(QString());
}

// ---------------------------------------------------------------------------
// FXSliderRow_Qt
// ---------------------------------------------------------------------------

FXSliderRow_Qt::FXSliderRow_Qt(const FXEditorSpec& spec, QWidget* parent) : FXParamEditor_Qt(spec, parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* slider = new FXSoftSlider(spec_, this);
    slider->setObjectName(spec_.objectBase + ".slider");
    slider->setToolTip(spec_.tooltip);
    slider->onEdited = [this](float v) { edited(v); };
    slider_ = slider;

    reset_ = new QToolButton(this);
    reset_->setObjectName(spec_.objectBase + ".reset");
    reset_->setToolTip(tr("Reset to default"));
    reset_->setAccessibleName(tr("Reset %1 to default").arg(spec_.label));
    reset_->setText(kResetUnchanged);
    ui::makeIconButton(reset_);
    connect(reset_, &QToolButton::clicked, this, [this] { edited(spec_.defaultValue); });

    auto* row = new QHBoxLayout(this);
    ui::applyRowMetrics(row);
    row->addWidget(label_);
    row->addWidget(field_);
    row->addWidget(slider_, 1);
    row->addWidget(reset_);
    refresh();
}

bool FXSliderRow_Qt::isInteracting() const {
    return FXParamEditor_Qt::isInteracting() || static_cast<FXSoftSlider*>(slider_)->isDragging();
}

void FXSliderRow_Qt::setLabelColumnWidth(int w) { label_->setFixedWidth(w); }

void FXSliderRow_Qt::refreshView() {
    const bool modified = isModified();
    static_cast<FXSoftSlider*>(slider_)->setValue(value_, modified);
    const int state = modified ? 1 : 0;
    if (state == resetState_) return;
    resetState_ = state;
    reset_->setText(modified ? kResetModified : kResetUnchanged);
    reset_->setStyleSheet(modified ? QString("QToolButton { padding: 0; color: %1; }").arg(jefe::qt::kFXModifiedColor)
                                   : QString("QToolButton { padding: 0; }"));
}

// ---------------------------------------------------------------------------
// FXKnob_Qt
// ---------------------------------------------------------------------------

FXKnob_Qt::FXKnob_Qt(const FXEditorSpec& spec, QWidget* parent) : FXParamEditor_Qt(spec, parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    label_->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);

    auto* dial = new FXKnobDial(spec_, this);
    dial->setObjectName(spec_.objectBase + ".knob");
    dial->setToolTip(spec_.tooltip);
    dial->onEdited = [this](float v) { edited(v); };
    dial->onDoubleClick = [this] { field_->beginEdit(); };
    knob_ = dial;

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(ui::kGapTight);
    col->addWidget(label_, 0, Qt::AlignHCenter);
    col->addWidget(knob_, 0, Qt::AlignHCenter);
    col->addWidget(field_, 0, Qt::AlignHCenter);
    refresh();
}

bool FXKnob_Qt::isInteracting() const {
    return FXParamEditor_Qt::isInteracting() || static_cast<FXKnobDial*>(knob_)->isDragging();
}

void FXKnob_Qt::refreshView() { static_cast<FXKnobDial*>(knob_)->setValue(value_, isModified()); }

// ---------------------------------------------------------------------------
// FXFieldEditor_Qt
// ---------------------------------------------------------------------------

FXFieldEditor_Qt::FXFieldEditor_Qt(const FXEditorSpec& spec, QWidget* parent) : FXParamEditor_Qt(spec, parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* row = new QHBoxLayout(this);
    ui::applyRowMetrics(row, ui::kGapTight);
    row->addWidget(label_);
    row->addWidget(field_);
    refresh();
}

// ---------------------------------------------------------------------------
// Free functions
// ---------------------------------------------------------------------------

FXParamEditor_Qt* makeFloatEditor(fxui::Style style, const FXEditorSpec& spec, QWidget* parent) {
    switch (style) {
        case fxui::Style::Knob: return new FXKnob_Qt(spec, parent);
        case fxui::Style::Field: return new FXFieldEditor_Qt(spec, parent);
        case fxui::Style::Slider:
        case fxui::Style::Default: break;
    }
    return new FXSliderRow_Qt(spec, parent);
}

void attachResetMenu(QWidget* w, std::function<void()> reset) {
    auto* action = new QAction(QObject::tr("Reset to default"), w);
    action->setObjectName(w->objectName() + ".resetDefault");
    QObject::connect(action, &QAction::triggered, w, [reset = std::move(reset)] { reset(); });
    w->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(w, &QWidget::customContextMenuRequested, w, [w, action](const QPoint& pos) {
        QMenu menu(w);
        menu.addAction(action);
        menu.exec(w->mapToGlobal(pos));
    });
}

QToolButton* makePowerToggle(QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setCheckable(true);
    b->setIcon(jefe::qticons::power());
    b->setToolTip(QObject::tr("Effect on/off"));
    b->setAccessibleName(QObject::tr("Effect on/off"));
    ui::makeIconButton(b);
    b->setStyleSheet("QToolButton { padding: 0; }");
    return b;
}

// ---------------------------------------------------------------------------
// Self-test (--fx-editors-test)
// ---------------------------------------------------------------------------

namespace {

int gFails = 0;

void report(bool ok, const char* name, const QString& detail = QString()) {
    if (ok) {
        std::printf("FX-EDIT PASS %s\n", name);
    } else {
        ++gFails;
        std::printf("FX-EDIT FAIL %s %s\n", name, qPrintable(detail));
    }
}

bool near(float a, float b, float tol = 1e-3f) { return std::fabs(a - b) <= tol; }

void mouse(QWidget* w, QEvent::Type type, QPointF pos, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    const Qt::MouseButtons buttons =
        (type == QEvent::MouseButtonRelease) ? Qt::NoButton : Qt::MouseButtons(Qt::LeftButton);
    const Qt::MouseButton button = (type == QEvent::MouseMove) ? Qt::NoButton : Qt::LeftButton;
    QMouseEvent e(type, pos, w->mapToGlobal(pos), button, buttons, mods);
    QCoreApplication::sendEvent(w, &e);
}

void drag(QWidget* w, QPointF from, QPointF to, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    mouse(w, QEvent::MouseButtonPress, from, mods);
    mouse(w, QEvent::MouseMove, to, mods);
    mouse(w, QEvent::MouseButtonRelease, to, mods);
}

void key(QWidget* w, int k, const QString& text = QString()) {
    QKeyEvent press(QEvent::KeyPress, k, Qt::NoModifier, text);
    QCoreApplication::sendEvent(w, &press);
    QKeyEvent release(QEvent::KeyRelease, k, Qt::NoModifier, text);
    QCoreApplication::sendEvent(w, &release);
}

void typeInto(QWidget* field, const QString& text) {
    mouse(field, QEvent::MouseButtonDblClick, QPointF(5, 10));
    mouse(field, QEvent::MouseButtonRelease, QPointF(5, 10));
    for (const QChar c : text) key(field, c.unicode(), QString(c));
    key(field, Qt::Key_Return);
}

struct Recorder {
    std::vector<float> values;
    explicit Recorder(FXParamEditor_Qt* e) {
        QObject::connect(e, &FXParamEditor_Qt::valueEdited, [this](float v) { values.push_back(v); });
    }
    float last() const { return values.empty() ? -12345.f : values.back(); }
};

FXEditorSpec testSpec(const QString& base) {
    FXEditorSpec s;
    s.label = "Gain";
    s.tooltip = "Test parameter";
    s.hardMin = 0.f;
    s.hardMax = 9999.f;
    s.softMin = 0.f;
    s.softMax = 2.f;
    s.step = 0.001f;
    s.defaultValue = 1.f;
    s.objectBase = base;
    return s;
}

QString emitted(const Recorder& r) {
    QStringList parts;
    for (float v : r.values) parts << QString::number(v, 'f', 4);
    return "emitted [" + parts.join(", ") + "]";
}

bool labelIsAccent(const FXParamEditor_Qt& e) {
    return e.label()->styleSheet().contains(QLatin1String(jefe::qt::kFXModifiedColor));
}

}  // namespace

int fxEditorsSelfTest() {
    gFails = 0;
    using namespace jefe::qt;

    // slider row default
    {
        FXSliderRow_Qt row(testSpec("t.slider"), nullptr);
        row.show();
        row.setValue(1.f);
        const bool ok = row.field()->text() == "1.000" && row.resetButton()->text() == QString(QChar(0x25CB)) &&
                        !labelIsAccent(row) && !row.isModified();
        report(ok, "slider row default",
               QString("field '%1' reset '%2' accent %3").arg(row.field()->text(), row.resetButton()->text())
                   .arg(labelIsAccent(row)));
    }

    // typed past soft range
    {
        FXSliderRow_Qt row(testSpec("t.slider"), nullptr);
        row.show();
        row.setValue(1.f);
        Recorder rec(&row);
        typeInto(row.field(), "5");
        const bool emittedOk = rec.values.size() == 1 && near(rec.values[0], 5.f);
        row.setValue(5.f);
        const float handle = sliderHandlePosition(row.slider());
        const bool ok = emittedOk && row.field()->text() == "5.000" && near(handle, 1.f) && near(row.value(), 5.f);
        report(ok, "typed past soft range",
               QString("%1 field '%2' handle %3 value %4").arg(emitted(rec), row.field()->text())
                   .arg(handle).arg(row.value()));
    }

    // typed past hard range
    {
        FXSliderRow_Qt row(testSpec("t.slider"), nullptr);
        row.show();
        row.setValue(1.f);
        Recorder rec(&row);
        typeInto(row.field(), "12000");
        const bool ok = rec.values.size() == 1 && near(rec.values[0], 9999.f, 0.01f);
        report(ok, "typed past hard range", emitted(rec));
    }

    // field scrub, coarse and fine
    {
        FXSliderRow_Qt row(testSpec("t.slider"), nullptr);
        row.show();
        row.setValue(1.f);
        Recorder rec(&row);
        drag(row.field(), QPointF(5, 10), QPointF(105, 10));
        const float coarse = rec.last();
        row.setValue(1.f);
        drag(row.field(), QPointF(5, 10), QPointF(105, 10), Qt::ShiftModifier);
        const float fine = rec.last();
        report(near(coarse, 2.f) && near(fine, 1.1f), "field scrub",
               QString("coarse %1 fine %2").arg(coarse).arg(fine));
    }

    // knob turn
    {
        FXKnob_Qt knob(testSpec("t.knob"), nullptr);
        knob.show();
        knob.setValue(1.f);
        Recorder rec(&knob);
        QWidget* dial = knob.knob();
        const QPointF c(dial->width() / 2.0, dial->height() / 2.0);
        drag(dial, c, c - QPointF(0, 100));
        report(near(rec.last(), 2.f), "knob turn", emitted(rec));
    }

    // reset (dot and context-menu action)
    {
        FXSliderRow_Qt row(testSpec("t.slider"), nullptr);
        row.show();
        row.setValue(3.f);
        const bool modifiedLook = row.resetButton()->text() == QString(QChar(0x25CF)) && labelIsAccent(row);
        Recorder rec(&row);
        row.resetButton()->click();
        const bool dotOk = rec.values.size() == 1 && near(rec.values[0], 1.f);
        row.setValue(3.f);
        auto* action = row.findChild<QAction*>("t.slider.resetDefault");
        if (action) action->trigger();
        const bool menuOk = action && action->text() == "Reset to default" && rec.values.size() == 2 &&
                            near(rec.values[1], 1.f);
        report(modifiedLook && dotOk && menuOk, "reset",
               QString("modifiedLook %1 dot %2 menu %3 %4").arg(modifiedLook).arg(dotOk).arg(menuOk)
                   .arg(emitted(rec)));
    }

    // setValue ignored while interacting
    {
        FXSliderRow_Qt row(testSpec("t.slider"), nullptr);
        row.show();
        row.setValue(1.f);
        mouse(row.field(), QEvent::MouseButtonPress, QPointF(5, 10));
        mouse(row.field(), QEvent::MouseMove, QPointF(15, 10));
        const bool interacting = row.isInteracting();
        row.setValue(0.3f);
        const QString during = row.field()->text();
        mouse(row.field(), QEvent::MouseButtonRelease, QPointF(15, 10));
        row.setValue(0.3f);
        const QString after = row.field()->text();
        report(interacting && during == "1.100" && after == "0.300" && !row.isInteracting(),
               "setValue ignored while interacting",
               QString("interacting %1 during '%2' after '%3'").arg(interacting).arg(during, after));
    }

    // height and density
    {
        FXSliderRow_Qt row(testSpec("t.slider"), nullptr);
        FXFieldEditor_Qt field(testSpec("t.field"), nullptr);
        FXKnob_Qt knob(testSpec("t.knob"), nullptr);
        QStringList bad;
        for (FXParamEditor_Qt* e : std::initializer_list<FXParamEditor_Qt*>{&row, &field, &knob}) {
            e->setValue(1.f);
            e->show();
            e->adjustSize();
            if (e->field()->height() != ui::kControlH)
                bad << QString("%1.field h=%2").arg(e->objectName()).arg(e->field()->height());
            for (const auto& v : ui::checkDensity(e))
                bad << QString("%1 %2 %3").arg(v.widgetPath, v.rule, v.detail);
        }
        if (row.height() != ui::kControlH) bad << QString("row h=%1").arg(row.height());
        if (row.resetButton()->height() != ui::kControlH)
            bad << QString("reset h=%1").arg(row.resetButton()->height());
        if (field.height() != ui::kControlH) bad << QString("field editor h=%1").arg(field.height());
        report(bad.isEmpty(), "height", bad.join("; "));
    }

    // factory
    {
        std::unique_ptr<FXParamEditor_Qt> k(makeFloatEditor(fxui::Style::Knob, testSpec("t.f1"), nullptr));
        std::unique_ptr<FXParamEditor_Qt> f(makeFloatEditor(fxui::Style::Field, testSpec("t.f2"), nullptr));
        std::unique_ptr<FXParamEditor_Qt> s(makeFloatEditor(fxui::Style::Slider, testSpec("t.f3"), nullptr));
        const bool ok = dynamic_cast<FXKnob_Qt*>(k.get()) && dynamic_cast<FXFieldEditor_Qt*>(f.get()) &&
                        dynamic_cast<FXSliderRow_Qt*>(s.get()) && s->objectName() == "t.f3" &&
                        s->findChild<QWidget*>("t.f3.field") && s->findChild<QWidget*>("t.f3.slider") &&
                        s->findChild<QWidget*>("t.f3.reset") && s->findChild<QWidget*>("t.f3.label") &&
                        k->findChild<QWidget*>("t.f1.knob");
        report(ok, "factory");
    }

    // power toggle
    {
        QWidget host;
        QToolButton* b = makePowerToggle(&host);
        host.show();
        const bool before = b && b->isChecked();
        if (b) b->click();
        const bool ok = b && b->width() == ui::kControlH && b->height() == ui::kControlH && b->isCheckable() &&
                        !before && b->isChecked() && !b->toolTip().isEmpty() && !b->icon().isNull();
        report(ok, "power toggle",
               b ? QString("%1x%2 checkable %3 checked %4").arg(b->width()).arg(b->height())
                       .arg(b->isCheckable()).arg(b->isChecked())
                 : QString("null"));
    }

    std::printf("FX-EDIT RESULT %s %d\n", gFails == 0 ? "PASS" : "FAIL", gFails);
    return gFails;
}

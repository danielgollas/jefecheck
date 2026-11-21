// SPDX-License-Identifier: GPL-2.0-or-later
// Editor widgets for FX float parameters: a slider row (label, scrub field,
// soft-range slider, reset dot), a rotary knob and a plain field. Qt only: the
// widgets never call the bridge; the FX panel wires valueEdited to it and calls
// setValue with what the plate holds. Value maths comes from gfcFXUi.h.
#ifndef JEFECHECK_QT_FX_PARAM_EDITORS_H
#define JEFECHECK_QT_FX_PARAM_EDITORS_H

#include "gfcFXUi.h"

#include <QColor>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QPointF>
#include <QString>
#include <QWidget>

#include <functional>

class QLabel;
class QToolButton;

namespace jefe::qt {

// Decimals shown for a float with this step (0.001 -> 3, 1.0 -> 0), clamped so
// a tiny step does not blow up the field width.
int decimalsForStep(float step);

// Accent colour for a parameter that differs from its default.
inline constexpr const char* kFXModifiedColor = "#d4771e";

}  // namespace jefe::qt

struct FXEditorSpec {
    QString label;
    QString tooltip;
    float hardMin = 0.f, hardMax = 1.f;
    float softMin = 0.f, softMax = 1.f;
    float step = 0.001f;
    float defaultValue = 0.f;
    QColor labelColor;   // invalid = theme colour
    QString objectBase;  // e.g. "fxparams.fx0.param.Brightness"
};

// Read-only looking line edit that scrubs on horizontal drag (Shift = fine),
// edits on double-click (Enter commits, Esc cancels, focus loss commits).
class FXScrubField_Qt : public QLineEdit {
    Q_OBJECT
public:
    explicit FXScrubField_Qt(QWidget* parent = nullptr);

    void setRange(fxui::Range soft, float hardMin, float hardMax, float step);
    // Shows v; ignored while a drag or an edit is in progress.
    void setDisplayValue(float v);
    float displayValue() const { return value_; }

    void beginEdit();
    bool isEditing() const { return editing_; }
    bool isDragging() const { return dragging_; }
    bool isInteracting() const { return editing_ || dragging_; }

    // Wide enough for the soft range ends at the step's decimals.
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void valueEdited(float v);

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;

private:
    void commitEdit();
    void cancelEdit();
    void endEdit();
    void showValue();
    void emitIfChanged(bool force);
    QString format(float v) const;

    fxui::Range soft_{0.f, 1.f};
    float hardMin_ = 0.f, hardMax_ = 1.f, step_ = 0.001f;
    float value_ = 0.f;
    float lastEmitted_ = 0.f;
    bool editing_ = false;
    bool dragging_ = false;
    bool fine_ = false;
    float dragStartValue_ = 0.f;
    qreal dragStartX_ = 0.;
    qreal lastX_ = 0.;
    QElapsedTimer emitTimer_;
};

// Base of the float editors. Owns the value, the Reset to default context menu
// and the accent label colour while the value differs from the default.
class FXParamEditor_Qt : public QWidget {
    Q_OBJECT
public:
    FXParamEditor_Qt(const FXEditorSpec& spec, QWidget* parent);

    // Ignored while isInteracting().
    virtual void setValue(float v);
    float value() const { return value_; }
    virtual bool isInteracting() const;
    const FXEditorSpec& spec() const { return spec_; }
    bool isModified() const;

    QLabel* label() const { return label_; }
    FXScrubField_Qt* field() const { return field_; }

signals:
    void valueEdited(float v);

protected:
    // Called by subclasses when the user changes the value.
    void edited(float v);
    // Updates the subclass widgets to value(); the base refreshes field and label.
    virtual void refreshView() {}
    void refresh();
    fxui::Range soft() const { return {spec_.softMin, spec_.softMax}; }

    FXEditorSpec spec_;
    float value_ = 0.f;
    QLabel* label_ = nullptr;
    FXScrubField_Qt* field_ = nullptr;

private:
    void applyLabelColour();
    int labelState_ = -1;  // cache: 0 theme/spec colour, 1 accent
};

// Label column, scrub field, soft-range slider and a reset dot on one 24 px row.
class FXSliderRow_Qt : public FXParamEditor_Qt {
    Q_OBJECT
public:
    FXSliderRow_Qt(const FXEditorSpec& spec, QWidget* parent);
    bool isInteracting() const override;
    // Fixed width of the right-aligned label column, so rows line up.
    void setLabelColumnWidth(int w);
    QWidget* slider() const { return slider_; }
    QToolButton* resetButton() const { return reset_; }

protected:
    void refreshView() override;

private:
    QWidget* slider_ = nullptr;
    QToolButton* reset_ = nullptr;
    int resetState_ = -1;
};

// Label above a drawn rotary knob, value field below. Vertical drag turns it.
class FXKnob_Qt : public FXParamEditor_Qt {
    Q_OBJECT
public:
    FXKnob_Qt(const FXEditorSpec& spec, QWidget* parent);
    bool isInteracting() const override;
    QWidget* knob() const { return knob_; }

protected:
    void refreshView() override;

private:
    QWidget* knob_ = nullptr;
};

// Label then scrub field.
class FXFieldEditor_Qt : public FXParamEditor_Qt {
    Q_OBJECT
public:
    FXFieldEditor_Qt(const FXEditorSpec& spec, QWidget* parent);
};

FXParamEditor_Qt* makeFloatEditor(fxui::Style style, const FXEditorSpec& spec, QWidget* parent);

// Gives w a context menu with one action, Reset to default, which calls reset.
// The action is a child of w named "<w objectName>.resetDefault".
void attachResetMenu(QWidget* w, std::function<void()> reset);

// A 24x24 checkable icon button with a power glyph (accent when checked).
QToolButton* makePowerToggle(QWidget* parent);

// Headless self-test (--fx-editors-test). Prints FX-EDIT PASS|FAIL <name>,
// returns the number of failures.
int fxEditorsSelfTest();

#endif  // JEFECHECK_QT_FX_PARAM_EDITORS_H

// SPDX-License-Identifier: GPL-2.0-or-later
// Preferences -> Shortcuts keyboard map: a painted US ANSI keyboard (F-row, number row,
// QWERTY rows, space, arrows, a Delete/Home/End/PgUp/PgDn column). Each key is tinted by the category of
// the command it runs on the current modifier layer and shows a short name; hold keys carry a
// small ring. The layer is the toggled one (setLayer), or the modifiers physically held while
// a widget of the page (setModifierScope) has focus. Clicking a key emits keyClicked; clicking a modifier
// key toggles that modifier. The map takes no key focus, so it never consumes keys.
//
// Non-US layouts: keys are labelled by Qt key names on the ANSI shape; the list is authoritative.
#pragma once

#include "Shortcuts_qt.h"

#include <QColor>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QRectF>
#include <QString>
#include <QWidget>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;

namespace jefe::shortcuts {

struct KeyCap {
    Qt::Key key;
    QString label;
    float x, y, w;  // units of one key width; y is the row's top (the F-row is 0)
};
// The map's keys. Modifier keys (Shift, Control, Alt, Meta) and Tab / Caps Lock are on it for
// shape; the modifier keys toggle layers.
QList<KeyCap> ansiLayout();

// The cap a bound key is drawn on: Enter on Return, Backtab on Shift+Tab, and US shifted
// symbols ("<", "?", "+") on their base key with Shift. False for a key that has no cap.
bool capForKey(const QKeyCombination& key, Qt::Key* cap, Qt::KeyboardModifiers* mods);

// The short name the map shows for a command or hold key ("Set In", "Gamma").
QString shortCommandName(const QString& id, const QString& name);

// One tint per category, derived from the theme's accent; readable under #e0e0e0 text.
QColor categoryTint(Category c);         // the key fill
QColor categoryColor(Category c);        // the saturated swatch (legend, border)
QString categoryDisplayName(Category c);
QList<Category> allCategories();

// The modifiers the map offers, in display order: Shift, Control (Ctrl), Alt, plus on macOS
// Meta (the Control key, shown first as the Mac does) and Control (Cmd).
QList<Qt::KeyboardModifier> layerModifiers();
QString modifierGlyph(Qt::KeyboardModifier m);  // "⇧" "⌃" "⌥" "⌘" on macOS; "Shift" "Ctrl" "Alt"

}  // namespace jefe::shortcuts

class ShortcutKeyboard_Qt : public QWidget {
    Q_OBJECT
public:
    struct Binding {
        Qt::KeyboardModifiers mods;
        QKeySequence key;      // as bound ("<", not Shift+,)
        QString id;
        QString name;          // full command name
        QString caption;       // short name
        jefe::shortcuts::Category category;
        bool hold = false;
    };

    explicit ShortcutKeyboard_Qt(QWidget* parent = nullptr);
    ~ShortcutKeyboard_Qt() override;

    void setLayer(Qt::KeyboardModifiers mods);   // the toggled layer
    Qt::KeyboardModifiers layer() const;         // shown: held modifiers, else the toggled layer
    Qt::KeyboardModifiers toggledLayer() const { return toggled_; }
    QRectF keyRect(Qt::Key key) const;           // widget coordinates; empty when not on the map
    QString keyCaption(Qt::Key key) const;       // short name on the shown layer, or empty
    bool showsHoldMark(Qt::Key key) const;       // a hold key on the shown layer
    // The caption as painted: keyCaption when it fits the key whole, else empty (never elided).
    QString drawnCaption(Qt::Key key) const;
    // Commands on the key on the shown layer; more than one draws a "+" mark.
    int bindingCount(Qt::Key key) const;
    QString keyToolTip(Qt::Key key) const;       // every command on the key on the shown layer
    QList<Binding> bindingsFor(Qt::Key key) const;  // every layer, in layer order
    Qt::Key selectedKey() const { return selected_; }
    void setSelectedKey(Qt::Key key);
    // The key under a command being dragged over the map (unknown when none).
    Qt::Key dropHoverKey() const { return dropHover_; }
    // The mime type a dragged command carries: its id, UTF-8.
    static QString commandMimeType();
    // Held modifiers count while a widget inside `scope` has focus (default: the parent).
    void setModifierScope(QWidget* scope) { scope_ = scope; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void keyClicked(Qt::Key key);
    void layerChanged(Qt::KeyboardModifiers shown);
    // A command was dropped on a key; `layer` is the layer shown at the drop.
    void commandDropped(const QString& id, Qt::Key key, Qt::KeyboardModifiers layer);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    bool event(QEvent* e) override;                       // tooltips
    bool eventFilter(QObject* o, QEvent* e) override;     // held modifiers inside the scope
    void changeEvent(QEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void rebuild();                                       // bindings cache from the registry
    void setHeld(Qt::KeyboardModifiers held);
    const Binding* bindingOn(Qt::Key key, Qt::KeyboardModifiers mods) const;  // the first
    QList<const Binding*> bindingsOn(Qt::Key key, Qt::KeyboardModifiers mods) const;
    int capAt(const QPointF& pos) const;
    QRectF capRect(const jefe::shortcuts::KeyCap& c) const;
    qreal unit() const;
    QWidget* modifierScope() const;
    Qt::Key dropKeyAt(const QPointF& pos, const QMimeData* mime) const;  // unknown: not a target
    void setDropHover(Qt::Key key);

    QList<jefe::shortcuts::KeyCap> caps_;
    QHash<int, QList<Binding>> bindings_;                 // by cap key
    Qt::KeyboardModifiers toggled_ = Qt::NoModifier;
    Qt::KeyboardModifiers held_ = Qt::NoModifier;
    Qt::Key selected_ = Qt::Key_unknown;
    Qt::Key dropHover_ = Qt::Key_unknown;
    QWidget* scope_ = nullptr;
};

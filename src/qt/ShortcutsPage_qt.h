// SPDX-License-Identifier: GPL-2.0-or-later
// Preferences -> Shortcuts: the preset, import / export, and the list of every command and
// hold key with its keys, where a key is changed in place. Edits go to the shortcut registry
// at once (so a key can be tried); PreferencesWindow_Qt's Done saves, Cancel reverts.
//
// Each command row has two key columns (Key, Second key): a command holds up to two keys and
// each is captured on its own, so binding one never drops the other. Hold keys have one.
// The "Inside panels" group lists keys that only work inside one widget; it is read-only.
#pragma once

#include "Shortcuts_qt.h"

#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QPersistentModelIndex>
#include <QString>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStandardItem;
class QStandardItemModel;
class QTreeView;
class QTreeWidget;
class ShortcutKeyboard_Qt;

// The in-place key capture: the first key with its modifiers is the key. Escape cancels;
// Backspace or Delete with nothing captured unbinds; modifier-only and unknown keys are
// ignored (the capture keeps waiting). Shift+a US digit symbol (!, @, ...) records Shift+digit,
// as the presets store it.
class ShortcutCaptureEdit_Qt : public QKeySequenceEdit {
    Q_OBJECT
public:
    explicit ShortcutCaptureEdit_Qt(QWidget* parent = nullptr);
    // The key a press stands for; empty when it is not a key the registry can hold.
    static QKeySequence keyFor(int key, Qt::KeyboardModifiers mods);

signals:
    void captured(const QKeySequence& key);
    void cancelled();
    void unbound();

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;

private:
    bool done_ = false;  // one signal per capture
};

class ShortcutsPage_Qt : public QWidget {
    Q_OBJECT
public:
    // The list's rows: kIdRole holds the command / hold-key id, kKindRole the Kind.
    enum Kind { Group = 0, CommandRow, HoldRow, PanelRow };
    static constexpr int kIdRole = Qt::UserRole + 1;
    static constexpr int kKindRole = Qt::UserRole + 2;

    explicit ShortcutsPage_Qt(QWidget* parent = nullptr);

    // Selects (and scrolls to) a command or hold-key row, clearing a search that hides it.
    void selectCommand(const QString& id);
    // The keyboard map above the list: modifier layer toggles, category legend, the map.
    QWidget* keymapSlot() const { return keymapSlot_; }
    ShortcutKeyboard_Qt* keymap() const { return keymap_; }

    // Import / export without a file dialog (the buttons call these after asking for a path).
    bool importFile(const QString& path);
    bool exportFile(const QString& path);

    // Opens the in-place capture on a row's key (`slot` 0 = Key, 1 = Second key). False for a
    // read-only or unknown row.
    bool beginCapture(const QString& id, int slot);
    // Binds a command (or hold key) to `key` + `mods`, as a drop on the map does: the same
    // checks as a capture (reserved keys, conflict bar). A command's first key is replaced (or
    // filled when it has none); a hold key takes the bare key and refuses modifiers. False for an
    // unknown or read-only row.
    bool assignKey(const QString& id, Qt::Key key, Qt::KeyboardModifiers mods);
    ShortcutCaptureEdit_Qt* captureEditor() const { return captureEditor_; }

    // Test access.
    QTreeView* list() const { return list_; }
    QLineEdit* search() const { return search_; }
    QStringList visibleIds() const;                      // command / hold / panel rows shown
    QString keyText(const QString& id, int slot) const;  // Key column text as shown
    bool rowShowsChanged(const QString& id) const;       // orange label and reset dot
    bool resetRowByClick(const QString& id);             // clicks the row's reset dot
    QStringList panelRowIds() const;

signals:
    void commandSelected(const QString& id);
    // The conflict bar opened for `id` wanting `key` (from a capture or a drop on the map).
    void conflictRaised(const QString& id, const QKeySequence& key);

protected:
    // Enter on a list row starts a capture; Escape in the search box or the list cancels the
    // open bar or clears the search first; Return in the search box goes to the first match.
    bool eventFilter(QObject* o, QEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;  // Escape from the page's other widgets

private:
    struct Pending {
        QString id;
        bool hold = false;
        int slot = 0;
        QKeySequence key;
        QStringList others;
    };

    void buildRows();
    void refresh();
    void applyFilter();
    QStandardItem* rowItem(const QString& id) const;
    void finishCapture();
    void onCaptured(const QKeySequence& key);
    void requestBind(const Pending& p);  // reserved check, conflict bar, or apply
    bool refuseReserved(const QKeySequence& key);  // a notice and true for a key the system takes
    void onUnbound();
    void apply(const Pending& p);
    void unbindFrom(const QString& id, const QKeySequence& key);
    void resetRow(const QString& id);
    void dismissConflict();  // hides the conflict bar and forgets its pending key
    bool handleEscape();     // true when Escape cancelled something on the page
    void focusFirstMatch();
    void onPresetActivated(int index);
    void buildKeymap();
    void showKeyInfo(Qt::Key key);  // the side panel: what `key` does on every modifier layer
    void showNotice(const QString& text, bool error);
    QString nameOf(const QString& id) const;
    QString testPath() const;  // JEFECHECK_SHORTCUTS_FILE, for UI tests: skips the file dialog

    jefe::shortcuts::Registry& reg_;
    QComboBox* preset_ = nullptr;
    QLabel* custom_ = nullptr;
    QPushButton* import_ = nullptr;
    QPushButton* export_ = nullptr;
    QPushButton* reset_ = nullptr;
    QWidget* presetBar_ = nullptr;
    QLabel* presetBarText_ = nullptr;
    QString pendingPreset_;
    QWidget* keymapSlot_ = nullptr;
    ShortcutKeyboard_Qt* keymap_ = nullptr;
    QLabel* keyInfoTitle_ = nullptr;
    QTreeWidget* keyInfo_ = nullptr;
    QLineEdit* search_ = nullptr;
    QTreeView* list_ = nullptr;
    QStandardItemModel* model_ = nullptr;
    QWidget* conflictBar_ = nullptr;
    QLabel* conflictText_ = nullptr;
    QLabel* notice_ = nullptr;
    Pending pending_;
    QPersistentModelIndex captureIndex_;
    ShortcutCaptureEdit_Qt* captureEditor_ = nullptr;
    friend class ShortcutCaptureDelegate_Qt;
};

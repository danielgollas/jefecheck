// qticons.h — shared programmatic icon factory (JEF-19).
//
// JefeCheck ships no icon asset files and uses no Qt resource (.qrc) or
// QStyle::standardIcon glyphs. Every icon is painted with QPainter. This module
// promotes the pattern that previously lived privately in PlateCard_qt.cpp
// (makeMirrorPixmap/makeMirrorIcon/makeIconToggle) into one reusable place so
// the Playlist, Timeline, FX/LUT panels and the dialogs can share it.
//
// Each icon is a DUAL-STATE QIcon: a light glyph (0xE0E0E0) for the normal
// dark-theme button (QIcon::Off) and a dark glyph (0x1A1A1A) for the orange
// ":checked" background (QIcon::On). Non-checkable buttons simply use the Off
// pixmap. Rendered at 2x device-pixel-ratio for retina crispness.
#pragma once

#include <QIcon>
#include <functional>

class QPushButton;
class QWidget;
class QPainter;
class QColor;

namespace jefe {
namespace qticons {

// A glyph painter draws into a logical `side` x `side` box using `color` as the
// stroke/fill tint. Coordinates are in logical pixels (0..side).
using PaintFn = std::function<void(QPainter& p, qreal side, const QColor& color)>;

// Build a dual-state icon (Off = light glyph, On = dark glyph) from a painter.
QIcon make(const PaintFn& paint, int side = 16);

// Compact icon button (QPushButton). A tooltip is REQUIRED — icon-only buttons
// are unusable without one. `withText` keeps the label beside the icon (for
// wordy primary actions); otherwise the button is icon-only and fixed-width.
QPushButton* makeIconButton(QWidget* parent, const QIcon& icon,
                            const QString& tooltip, const QString& accessibleName,
                            bool checkable = false, const QString& text = QString());

// --- Named glyphs (each returns a fresh dual-state QIcon) -------------------
QIcon add();                 // +
QIcon addFiles();            // document with a +
QIcon remove();              // ✕  (remove one)
QIcon trash();               // trash can (clear all)
QIcon up();                  // chevron up
QIcon down();                // chevron down
QIcon chevron(bool expanded);// ▾ (expanded) / ▸ (collapsed)
QIcon dragHandle();          // ☰
QIcon rewind();              // ◀◀ to start
QIcon stepBack();            // |◀
QIcon play();                // ▶
QIcon pause();               // ⏸
QIcon stepForward();         // ▶|
QIcon fastForward();         // ▶▶ to end
QIcon filmstrip();           // 🎞
QIcon refresh();             // circular arrow
QIcon folder();              // open folder (browse / load)
QIcon save();                // floppy disk
QIcon check();               // ✓ (apply)
QIcon send();                // paper plane
QIcon recent();              // clock

} // namespace qticons
} // namespace jefe

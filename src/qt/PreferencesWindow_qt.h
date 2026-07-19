// Preferences dialog for the Qt build. A sidebar list selects a page; each
// page binds widgets to the global `sett` (gfcSettings). Persistence is Qt
// QSettings via jefe::qt::writePreferences() (Done); Cancel reverts from a
// `sett` snapshot.
//
// Pages cover: General (incl. the folded Playback & Engine controls), Text,
// Formats, Search Paths, Remote. Text binds to the GfcTextRenderer singleton
// (not `sett`) with deferred QSettings persistence — see writeTextPrefs() and
// qt_prefs_persist's applyTextPrefs().
//
// While the (modal) dialog is open a repaint timer emits
// viewportRepaintRequested() so viewport-affecting settings (text style,
// background color/checkerboard, aspect bars) preview in real time; the owner
// connects it to the viewport's update().
#ifndef JEFECHECK_QT_PREFERENCES_WINDOW_H
#define JEFECHECK_QT_PREFERENCES_WINDOW_H

#include <QColor>
#include <QDialog>

#include <memory>

// Forward-declared only — gfcStructures.h pulls glad/glad.h, which cannot
// share a translation unit with QOpenGLWidget on macOS (see
// developer_notes.md §1). This header is included by MainWindow_qt.cpp,
// which also includes GlViewport_qt.h (QOpenGLWidget), so the full
// gfcSettings definition must stay confined to the .cpp.
class gfcSettings;

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTimer;
class QWidget;

class PreferencesWindow_Qt : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesWindow_Qt(QWidget* parent = nullptr);
    ~PreferencesWindow_Qt() override;

signals:
    // Emitted continuously while the dialog is open so the owner can repaint
    // the viewport for live preview of viewport-affecting settings.
    void viewportRepaintRequested();

private:
    void addPage(const QString& title, QWidget* page);
    void buildGeneralPage();
    void buildTextPage();
    void buildFormatsPage();
    void buildSearchPathsPage();
    void buildRemotePage();

    // Wraps `content` in a CollapsibleSection and returns it for adding to a
    // page layout. Scaffolding for later tasks — the three current pages
    // don't use it yet.
    QWidget* section(const QString& title, QWidget* content);

    // Text prefs use deferred (Done-writes) persistence rather than the
    // per-change QSettings writes other pages use, matching the Engine
    // combos' pattern (see developer_notes / task-6 brief). Reads the
    // current Text-page widget values and writes them to `Text/*`
    // QSettings; called from the Done handler.
    void writeTextPrefs();

    QListWidget* sidebar_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QTimer* liveTimer_ = nullptr;  // drives viewportRepaintRequested() while open

    // Text page widgets — kept as members so writeTextPrefs() (called on
    // Done) can read their current values. Live edits only call the
    // textRenderer() setters for immediate preview; QSettings is untouched
    // until Done, so Cancel (which calls jefe::qt::applyTextPrefs() to
    // reapply the unchanged QSettings) reverts them like every other page.
    QSpinBox* textSizeSpin_ = nullptr;
    QComboBox* textHintCombo_ = nullptr;
    QComboBox* textFilterCombo_ = nullptr;
    QDoubleSpinBox* textGammaSpin_ = nullptr;
    QCheckBox* textShadowEnabledCheck_ = nullptr;
    QDoubleSpinBox* textShadowOffXSpin_ = nullptr;
    QDoubleSpinBox* textShadowOffYSpin_ = nullptr;
    QDoubleSpinBox* textShadowBlurSpin_ = nullptr;
    QPushButton* textColorBtn_ = nullptr;
    QPushButton* textShadowColorBtn_ = nullptr;
    QColor textColor_;
    QColor textShadowColor_;

    // Snapshot of `sett` taken on open; restored verbatim on Cancel so
    // in-progress edits don't leak into the live global. Held behind a
    // unique_ptr so this header doesn't need the complete gfcSettings type
    // (see the forward-declaration comment above); allocated/populated in
    // the .cpp where gfcStructures.h is available.
    std::unique_ptr<gfcSettings> sett_backup_;
};

#endif

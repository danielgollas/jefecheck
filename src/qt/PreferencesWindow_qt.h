// Preferences dialog for the Qt build. Mirrors FLTK's preferencesWindow:
// a sidebar list selects a page, each page binds widgets to the global
// `sett` (gfcSettings). Done writes the settings via saveSettings().
//
// Pages cover: General, Engine, Formats, Search Paths. Remote, Text are
// placeholder pages for now — they need extra plumbing (per-track UI,
// font enumeration) that's better as follow-up PRs. The shell handles
// them so adding a page later is just an addPage() call.
#ifndef JEFECHECK_QT_PREFERENCES_WINDOW_H
#define JEFECHECK_QT_PREFERENCES_WINDOW_H

#include <QDialog>

#include <memory>

// Forward-declared only — gfcStructures.h pulls glad/glad.h, which cannot
// share a translation unit with QOpenGLWidget on macOS (see
// developer_notes.md §1). This header is included by MainWindow_qt.cpp,
// which also includes GlViewport_qt.h (QOpenGLWidget), so the full
// gfcSettings definition must stay confined to the .cpp.
class gfcSettings;

class QListWidget;
class QStackedWidget;
class QWidget;

class PreferencesWindow_Qt : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesWindow_Qt(QWidget* parent = nullptr);
    ~PreferencesWindow_Qt() override;

private:
    void addPage(const QString& title, QWidget* page);
    void buildGeneralPage();
    void buildEnginePage();
    void buildFormatsPage();
    void buildSearchPathsPage();
    void buildPlaceholderPage(const QString& title, const QString& note);

    // Wraps `content` in a CollapsibleSection and returns it for adding to a
    // page layout. Scaffolding for later tasks — the three current pages
    // don't use it yet.
    QWidget* section(const QString& title, QWidget* content);

    QListWidget* sidebar_ = nullptr;
    QStackedWidget* pages_ = nullptr;

    // Snapshot of `sett` taken on open; restored verbatim on Cancel so
    // in-progress edits don't leak into the live global. Held behind a
    // unique_ptr so this header doesn't need the complete gfcSettings type
    // (see the forward-declaration comment above); allocated/populated in
    // the .cpp where gfcStructures.h is available.
    std::unique_ptr<gfcSettings> sett_backup_;
};

#endif

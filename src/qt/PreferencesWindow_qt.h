// Preferences dialog for the Qt build. Mirrors FLTK's preferencesWindow:
// a sidebar list selects a page, each page binds widgets to the global
// `sett` (gfcSettings). Done writes the settings via saveSettings().
//
// Pages cover: General, Engine, Formats. Remote, Paths, Text are
// placeholder pages for now — they need extra plumbing (per-track UI,
// filesystem pickers, font enumeration) that's better as follow-up
// PRs. The shell handles them so adding a page later is just an
// addPage() call.
#ifndef JEFECHECK_QT_PREFERENCES_WINDOW_H
#define JEFECHECK_QT_PREFERENCES_WINDOW_H

#include <QDialog>

class QListWidget;
class QStackedWidget;
class QWidget;

class PreferencesWindow_Qt : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesWindow_Qt(QWidget* parent = nullptr);

private:
    void addPage(const QString& title, QWidget* page);
    void buildGeneralPage();
    void buildEnginePage();
    void buildFormatsPage();
    void buildPlaceholderPage(const QString& title, const QString& note);

    QListWidget* sidebar_ = nullptr;
    QStackedWidget* pages_ = nullptr;
};

#endif

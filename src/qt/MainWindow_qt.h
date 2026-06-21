// JefeCheck's Qt shell window. Single QMainWindow with:
//   - Native menu bar (macOS pulls into system menu bar by default)
//   - Central GlViewport_Qt
//   - Plate Manager dock (bottom-left)
//   - Timeline + Transport dock (bottom-right, split alongside Plate Manager)
//   - FX Stack and LUT docks (right area, stacked into a tab group)
//
// Each dock is a standard QDockWidget — drag/float/redock comes free, and
// QMainWindow::saveState() persists the user's layout to QSettings.
#ifndef JEFECHECK_QT_MAIN_WINDOW_H
#define JEFECHECK_QT_MAIN_WINDOW_H

#include <QMainWindow>

#include <memory>
#include <string>
#include <vector>

class QDockWidget;
class FXParamPanel_Qt;
class FXStackPanel_Qt;
class GlViewport_Qt;
class LUTPanel_Qt;
class PlaylistPanel_Qt;
class RemoteDialog_Qt;
class PlateManager_Qt;
class QComboBox;
class QLabel;
class QMenu;
class TimelinePanel_Qt;
class QTimer;

namespace jefe::qt { class RenderBridge_Qt; }

class MainWindow_Qt : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow_Qt(QWidget* parent = nullptr);
    ~MainWindow_Qt() override;

    GlViewport_Qt* viewport() { return viewport_; }

    // Load `path` into plate `plateIdx` (0..3). Resolves a folder drop to
    // its first image, manages the viewport's GL context (texture uploads
    // happen on the calling thread), and refreshes the status bar.
    // Used by --open-file at startup and by drag-and-drop at runtime.
    void loadFileIntoPlate(int plateIdx, const QString& path);
    void loadFileIntoPlate(int plateIdx, const QString& path, float scale);

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onFileDropped(const QString& path, float scale);
    void openLoadWindow();
    void onLoadWindowDropForwarded(int plateIdx, const QString& path);

private:
    void buildMenuBar();
    void buildDocks();
    void restoreLayout();
    void saveLayout();

    // Session save/restore.
    void doSaveSession(bool forceDialog);
    void doOpenSession();
    void openSessionPath(const QString& path);   // GL-current load + bookkeeping
    void rebuildRecentSessionsMenu();
    void updateSessionTitle();
    void maybeRestoreSessionAtStartup();
    QString currentSessionPath_;
    QMenu*  recentMenu_ = nullptr;
    bool    lastExitWasClean_ = true;

    void startAutoload();
    void autoloadStep();

    class LoadWindowDialog_Qt* loadWindowDialog_ = nullptr;

    GlViewport_Qt* viewport_ = nullptr;
    QDockWidget* plateDock_ = nullptr;
    QDockWidget* timelineDock_ = nullptr;
    QDockWidget* fxDock_ = nullptr;
    QDockWidget* fxParamsDock_ = nullptr;
    QDockWidget* lutDock_ = nullptr;
    QDockWidget* playlistDock_ = nullptr;
    PlateManager_Qt* plateManagerWidget_ = nullptr;
    TimelinePanel_Qt* timelinePanelWidget_ = nullptr;
    LUTPanel_Qt* lutPanelWidget_ = nullptr;
    FXStackPanel_Qt* fxPanelWidget_ = nullptr;
    FXParamPanel_Qt* fxParamPanelWidget_ = nullptr;
    PlaylistPanel_Qt* playlistPanelWidget_ = nullptr;
    QComboBox* depthCombo_ = nullptr;
    QLabel* layoutStatusLabel_ = nullptr;
    QLabel* trackStatusLabel_ = nullptr;
    QLabel* loadedStatusLabel_ = nullptr;
    QLabel* startupStatusLabel_ = nullptr;
    QTimer* playbackTimer_ = nullptr;
    // The playback timer runs fast (for tight FPS pacing) but the timeline/
    // status read-back only needs ~60 Hz, so it's throttled to every Nth
    // tick. See the timer lambda in the constructor.
    int uiRefreshCounter_ = 0;

    // Incremental autoload state. The autoload walks LUT files first
    // (cheap, just glGenTextures), then FX files (expensive, GLSL
    // compile per .jfx). Each step processes one file and reposts via
    // QTimer::singleShot(0) so the event loop runs between compiles —
    // without that yield the AX system can't register the window for
    // tests until the load finishes (5-10 seconds), breaking the WDA
    // launch handshake.
    enum class AutoloadPhase { Idle, LUTs, FXs, Done };
    AutoloadPhase autoloadPhase_ = AutoloadPhase::Idle;
    std::vector<std::string> lutPaths_;
    std::vector<std::string> fxPaths_;
    int autoloadIdx_ = 0;

    std::unique_ptr<jefe::qt::RenderBridge_Qt> renderBridge_;
};

#endif

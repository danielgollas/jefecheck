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

class QDockWidget;
class GlViewport_Qt;
class LUTPanel_Qt;
class PlateManager_Qt;
class QLabel;
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

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onFileDropped(const QString& path);

private:
    void buildMenuBar();
    void buildDocks();
    void restoreLayout();
    void saveLayout();

    GlViewport_Qt* viewport_ = nullptr;
    QDockWidget* plateDock_ = nullptr;
    QDockWidget* timelineDock_ = nullptr;
    QDockWidget* fxDock_ = nullptr;
    QDockWidget* lutDock_ = nullptr;
    PlateManager_Qt* plateManagerWidget_ = nullptr;
    TimelinePanel_Qt* timelinePanelWidget_ = nullptr;
    LUTPanel_Qt* lutPanelWidget_ = nullptr;
    QLabel* layoutStatusLabel_ = nullptr;
    QLabel* trackStatusLabel_ = nullptr;
    QTimer* playbackTimer_ = nullptr;

    std::unique_ptr<jefe::qt::RenderBridge_Qt> renderBridge_;
};

#endif

#include "MainWindow_qt.h"

#include "FXLutPanel_qt.h"
#include "GlViewport_qt.h"
#include "ImageLoadBridge_qt.h"
#include "PlateManager_qt.h"
#include "RenderBridge_qt.h"
#include "SequenceLoadBridge_qt.h"
#include "TimelinePanel_qt.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QStatusBar>

namespace {
constexpr const char* kSettingsGeometry = "MainWindow/geometry";
constexpr const char* kSettingsState    = "MainWindow/state";
}

MainWindow_Qt::MainWindow_Qt(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("JefeCheck");
    resize(1280, 800);

    viewport_ = new GlViewport_Qt(this);
    setCentralWidget(viewport_);

    // Initialize the rendering pipeline's GUI bridges before the bridge
    // starts driving paintGL. Routed through a Qt-free TU because the
    // plateManager / trackManager headers pull glad, and Qt's
    // QOpenGLWidget can't share a TU with glad on macOS.
    jefe::qt::initializeRenderingChain();

    // Wire the JefeCheck rendering chain into the Qt viewport. The bridge
    // forwards onDraw/onResize to plateManager.draw().
    renderBridge_ = std::make_unique<jefe::qt::RenderBridge_Qt>();
    viewport_->setListener(renderBridge_.get());

    statusBar()->showMessage("Drop an image onto the viewport to load it.");

    connect(viewport_, &GlViewport_Qt::fileDropped,
            this, &MainWindow_Qt::onFileDropped);

    setDockOptions(QMainWindow::AnimatedDocks
                   | QMainWindow::AllowNestedDocks
                   | QMainWindow::AllowTabbedDocks);

    buildMenuBar();
    buildDocks();
    restoreLayout();
}

// Out-of-line destructor: lets the unique_ptr<RenderBridge_Qt> see the
// full RenderBridge_Qt definition (included above) when generating the
// deleter, instead of forcing the header to include RenderBridge_qt.h.
MainWindow_Qt::~MainWindow_Qt() = default;

void MainWindow_Qt::buildMenuBar() {
    auto* mb = menuBar();

    auto* fileMenu = mb->addMenu("&File");
    fileMenu->addAction("&Load Sequence…",
                        QKeySequence(Qt::CTRL | Qt::Key_O),
                        []() { /* TODO: wire to load callback */ });
    fileMenu->addAction("&Render…",
                        QKeySequence(Qt::CTRL | Qt::Key_R),
                        []() { /* TODO */ });
    fileMenu->addSeparator();
    fileMenu->addAction("&Preferences…", []() { /* TODO */ });
    fileMenu->addSeparator();
    fileMenu->addAction("&Quit",
                        QKeySequence::Quit,
                        []() { QApplication::quit(); });

    auto* viewMenu = mb->addMenu("&View");
    // Toggle actions for each dock. createDockWidget() exposes a built-in
    // toggleViewAction() that flips visibility and tracks state for us.
    auto rememberDockToggle = [viewMenu](QDockWidget* d) {
        if (!d) return;
        viewMenu->addAction(d->toggleViewAction());
    };
    // Filled in after buildDocks() runs, see below.
    (void)rememberDockToggle;

    mb->addMenu("&Help");
}

void MainWindow_Qt::buildDocks() {
    // Plate Manager — bottom-left of the bottom dock area.
    plateDock_ = new QDockWidget("Plate Manager", this);
    plateDock_->setObjectName("PlateManagerDock");
    plateManagerWidget_ = new PlateManager_Qt(plateDock_);
    plateDock_->setWidget(plateManagerWidget_);
    plateDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::BottomDockWidgetArea, plateDock_);

    // Mirror viewport-driven plate edits (drag pan, wheel zoom, keyboard
    // shortcuts) back into the plate cards so the spinboxes stay in sync.
    connect(viewport_, &GlViewport_Qt::plateStateChanged,
            plateManagerWidget_, &PlateManager_Qt::refreshAllCards);

    // The 2x2 minimum (wide + tall) applies only when the dock is on the
    // top or bottom edge. Floating, or docked to a side edge, drops to a
    // single-column minimum so the user can run it as a tall narrow
    // column. Generous chrome budget — Qt's dock framing, grid margins,
    // and inter-card spacing add up to more than back-of-envelope math
    // suggests.
    constexpr int kCardMaxW = 320;
    constexpr int kCardMinH = 84;
    constexpr int kHorizDockMinW = 2 * kCardMaxW + 80;   // 2 cols
    constexpr int kHorizDockMinH = 2 * kCardMinH + 60;   // 2 rows
    constexpr int kSingleColMinW = kCardMaxW + 60;       // 1 col
    constexpr int kSingleColMinH = kCardMinH + 40;       // 1 row

    auto updatePlateMins = [this]() {
        const bool wantsTwoColumns =
            !plateDock_->isFloating() &&
            (dockWidgetArea(plateDock_) == Qt::TopDockWidgetArea ||
             dockWidgetArea(plateDock_) == Qt::BottomDockWidgetArea);
        if (wantsTwoColumns) {
            plateDock_->setMinimumWidth(kHorizDockMinW);
            plateDock_->setMinimumHeight(kHorizDockMinH);
        } else {
            plateDock_->setMinimumWidth(kSingleColMinW);
            plateDock_->setMinimumHeight(kSingleColMinH);
        }
    };
    updatePlateMins();
    connect(plateDock_, &QDockWidget::topLevelChanged,
            plateDock_, [updatePlateMins](bool) { updatePlateMins(); });
    connect(plateDock_, &QDockWidget::dockLocationChanged,
            plateDock_, [updatePlateMins](Qt::DockWidgetArea) { updatePlateMins(); });

    // Timeline + Transport — bottom-right; split alongside the plate dock.
    timelineDock_ = new QDockWidget("Timeline", this);
    timelineDock_->setObjectName("TimelineDock");
    timelineDock_->setWidget(new TimelinePanel_Qt(timelineDock_));
    timelineDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::BottomDockWidgetArea, timelineDock_);

    // Place the timeline to the right of the plate manager so they share the
    // bottom strip side-by-side.
    splitDockWidget(plateDock_, timelineDock_, Qt::Horizontal);

    // FX Stack and LUTs — right side, stacked as tabs.
    fxDock_ = new QDockWidget("FX Stack", this);
    fxDock_->setObjectName("FXStackDock");
    fxDock_->setWidget(new FXStackPanel_Qt(fxDock_));
    fxDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::RightDockWidgetArea, fxDock_);

    lutDock_ = new QDockWidget("LUTs", this);
    lutDock_->setObjectName("LUTDock");
    lutDock_->setWidget(new LUTPanel_Qt(lutDock_));
    lutDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::RightDockWidgetArea, lutDock_);

    // Tab them together.
    tabifyDockWidget(fxDock_, lutDock_);
    fxDock_->raise();

    // Hook each dock's toggle into the View menu now that they exist.
    auto* viewMenu = menuBar()->findChild<QMenu*>(QString(), Qt::FindDirectChildrenOnly);
    // Find the View menu by title (findChild by name doesn't work — menus
    // don't have meaningful object names by default).
    QMenu* found = nullptr;
    for (QAction* a : menuBar()->actions()) {
        if (a->menu() && a->text().contains("View")) {
            found = a->menu();
            break;
        }
    }
    (void)viewMenu;
    if (found) {
        found->addAction(plateDock_->toggleViewAction());
        found->addAction(timelineDock_->toggleViewAction());
        found->addAction(fxDock_->toggleViewAction());
        found->addAction(lutDock_->toggleViewAction());
    }
}

void MainWindow_Qt::restoreLayout() {
    QSettings s;
    bool restored = false;
    if (s.contains(kSettingsGeometry)) {
        restoreGeometry(s.value(kSettingsGeometry).toByteArray());
    }
    if (s.contains(kSettingsState)) {
        restored = restoreState(s.value(kSettingsState).toByteArray());
    }
    if (!restored) {
        // First-launch defaults: shrink the bottom dock strip to its
        // minimum dimensions so the central viewport gets the rest of the
        // window. Without this, Qt distributes vertical space ~half/half.
        // Run after the event loop starts so the docks have real geometry.
        QMetaObject::invokeMethod(this, [this]() {
            resizeDocks({plateDock_, timelineDock_},
                        {plateDock_->minimumWidth(), 9999},
                        Qt::Horizontal);
            resizeDocks({plateDock_}, {plateDock_->minimumHeight()}, Qt::Vertical);
        }, Qt::QueuedConnection);
    }
}

void MainWindow_Qt::saveLayout() {
    QSettings s;
    s.setValue(kSettingsGeometry, saveGeometry());
    s.setValue(kSettingsState, saveState());
}

void MainWindow_Qt::closeEvent(QCloseEvent* e) {
    saveLayout();
    QMainWindow::closeEvent(e);
}

void MainWindow_Qt::onFileDropped(const QString& path) {
    if (!viewport_ || path.isEmpty()) return;

    const QString name = QFileInfo(path).fileName();

    // GL texture uploads happen inside loadPreview, so the viewport's
    // context must be current on the calling thread.
    viewport_->makeCurrent();
    const bool ok = jefe::qt::loadFileIntoPlate(path.toStdString(), 0);
    viewport_->doneCurrent();

    if (!ok) {
        statusBar()->showMessage(
            QString("Load failed: %1").arg(path), 5000);
        return;
    }

    viewport_->update();
    statusBar()->showMessage(
        QString("%1 loaded into Track A").arg(name));
}

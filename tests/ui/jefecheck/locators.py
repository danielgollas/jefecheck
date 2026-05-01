"""Object-name locators for JefeCheck widgets.

Qt bridges `objectName` to the AXIdentifier as the full dotted parent
chain. Tests target widgets via `identifier ENDSWITH '<name>'` predicates,
which are robust against widget reparenting and tab-order changes.

Each constant here is just the trailing piece (the leaf objectName).
"""

BUNDLE_ID = "com.danielgollas.jefecheck.qt"

WINDOW = "MainWindow"
VIEWPORT = "viewport"

# Status-bar widgets (permanent right-aligned indicators)
STATUSBAR_LAYOUT = "statusbar.layout.label"
STATUSBAR_TRACK = "statusbar.track.label"
STATUSBAR_LOADED = "statusbar.loaded.label"
STATUSBAR_STARTUP = "statusbar.startup.label"
STATUSBAR_DEPTH = "statusbar.depth.combo"

# Docks
DOCK_PLATEMANAGER = "dock.platemanager"
DOCK_TIMELINE = "dock.timeline"
DOCK_FXSTACK = "dock.fxstack"
DOCK_LUTS = "dock.luts"

# Menus / actions. Note: macOS folds Qt's QMenuBar into the system
# menu bar, which lives in a separate AX tree from the application
# window — `by_object_name(menu.*)` does NOT resolve via Mac2.
# These constants are kept as documentation of the canonical
# objectNames; functional menu testing happens via the keyboard
# shortcut bound to each action instead.
MENU_FILE = "menu.file"
MENU_FILE_LOAD = "menu.file.load"
MENU_FILE_PREFERENCES = "menu.file.preferences"
MENU_FILE_QUIT = "menu.file.quit"
MENU_VIEW = "menu.view"
MENU_VIEW_FULLSCREEN = "menu.view.fullscreen"
MENU_HELP = "menu.help"
# NOTE: Qt's macOS NSMenuItem AX bridge does NOT propagate QAction's
# objectName — every menu item appears with `identifier='qt_itemFired:'`
# under Mac2. Look up menu items by `title` instead (see test_minspecs.py
# for the elementType==54 + title pattern). The QAction objectNames
# below are kept for future Linux/Windows runs where Qt's QMenu does
# expose them via AT-SPI / UIA.
MENU_HELP_ABOUT = "menu.help.about"
MENU_HELP_SPECS = "menu.help.specs"

# Min Specs dialog (Help → System Specs). The dialog itself is a real
# QDialog so its child widgets DO surface objectNames as identifiers.
DIALOG_MINSPECS = "dialog.minspecs"
DIALOG_MINSPECS_GLVERSION = "dialog.minspecs.glversion"
DIALOG_MINSPECS_GLVENDOR = "dialog.minspecs.glvendor"
DIALOG_MINSPECS_GLRENDERER = "dialog.minspecs.glrenderer"
DIALOG_MINSPECS_BUTTONS = "dialog.minspecs.buttons"

# Transport
TRANSPORT_PLAY = "transport.play.button"
TRANSPORT_REWIND = "transport.rewind.button"
TRANSPORT_STEPBACK = "transport.stepback.button"
TRANSPORT_STEPFORWARD = "transport.stepforward.button"
TRANSPORT_FASTFORWARD = "transport.fastforward.button"
TRANSPORT_LOOP = "transport.loop.combo"
TRANSPORT_FRAME = "transport.frame.spin"
TRANSPORT_IN = "transport.in.spin"
TRANSPORT_OUT = "transport.out.spin"
TRANSPORT_FPS = "transport.fps.spin"

# LUT panel
LUT_LIST = "lut.list"
LUT_APPLY = "lut.apply.button"
LUT_REFRESH = "lut.refresh.button"

# FX Stack panel
FXSTACK_AVAILABLE = "fxstack.available.list"
FXSTACK_STACK = "fxstack.stack.list"
FXSTACK_ADD = "fxstack.add.button"
FXSTACK_REMOVE = "fxstack.remove.button"
FXSTACK_REFRESH = "fxstack.refresh.button"
FXSTACK_STATUS = "fxstack.status.label"

# Preferences
PREFERENCES_DIALOG = "preferences.dialog"
PREFERENCES_SIDEBAR = "preferences.sidebar"
PREFERENCES_DONE = "preferences.done.button"
PREFERENCES_CANCEL = "preferences.cancel.button"


def plate(plate_id: int, role: str) -> str:
    """Build a per-plate locator: plate(0, 'gamma.spin') -> 'plate.0.gamma.spin'."""
    return f"plate.{plate_id}.{role}"

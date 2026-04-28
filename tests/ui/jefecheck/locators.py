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

# Docks
DOCK_PLATEMANAGER = "dock.platemanager"
DOCK_TIMELINE = "dock.timeline"
DOCK_FXSTACK = "dock.fxstack"
DOCK_LUTS = "dock.luts"

# Menus / actions
MENU_FILE = "menu.file"
MENU_FILE_LOAD = "menu.file.load"
MENU_FILE_PREFERENCES = "menu.file.preferences"
MENU_FILE_QUIT = "menu.file.quit"

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

# Preferences
PREFERENCES_DIALOG = "preferences.dialog"
PREFERENCES_SIDEBAR = "preferences.sidebar"
PREFERENCES_DONE = "preferences.done.button"
PREFERENCES_CANCEL = "preferences.cancel.button"


def plate(plate_id: int, role: str) -> str:
    """Build a per-plate locator: plate(0, 'gamma.spin') -> 'plate.0.gamma.spin'."""
    return f"plate.{plate_id}.{role}"

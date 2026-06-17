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

# Timeline widgets (inside the timeline dock). The scrubber and the
# track-rows lane are single painted QWidgets — their sub-regions are
# not separate AX elements, so only the container objectNames resolve.
TIMELINE_SCRUBBER = "timeline.scrubber"
TIMELINE_TRACKS = "timeline.tracks"

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

# FX Params panel (read-only viewer of active plate's FX stack params)
DOCK_FXPARAMS = "dock.fxparams"
FXPARAMS_PANEL = "fxparams.panel"
FXPARAMS_STATUS = "fxparams.status.label"

# Remote sessions dialog (PR-41a — modal File → Remote Session…)
DIALOG_REMOTE = "dialog.remote"
REMOTE_DONE = "remote.done.button"
REMOTE_SERVER_NAME = "remote.server.name.edit"
REMOTE_SERVER_PORT = "remote.server.port.spin"
REMOTE_SERVER_PASSWORD = "remote.server.password.edit"
REMOTE_SERVER_START = "remote.server.start.button"
REMOTE_CLIENT_NAME = "remote.client.name.edit"
REMOTE_CLIENT_IP = "remote.client.ip.edit"
REMOTE_CLIENT_PORT = "remote.client.port.spin"
REMOTE_CLIENT_PASSWORD = "remote.client.password.edit"
REMOTE_CLIENT_CONNECT = "remote.client.connect.button"
REMOTE_DISCONNECT = "remote.disconnect.button"
REMOTE_STATUS = "remote.status.label"

# Playlist dock (PR-40)
DOCK_PLAYLIST = "dock.playlist"
PLAYLIST_PANEL = "playlist.panel"
PLAYLIST_LIST = "playlist.list"
PLAYLIST_ADD = "playlist.add.button"
PLAYLIST_REMOVE = "playlist.remove.button"
PLAYLIST_UP = "playlist.up.button"
PLAYLIST_DOWN = "playlist.down.button"
PLAYLIST_CLEAR = "playlist.clear.button"
PLAYLIST_STATUS = "playlist.status.label"

# Render dialog (PR-39a — minimal: quadrant/format/range/scale/path/prefix)
DIALOG_RENDER = "dialog.render"
RENDER_QUADRANT = "dialog.render.quadrant.combo"
RENDER_FORMAT = "dialog.render.format.combo"
RENDER_STARTFRAME = "dialog.render.startframe.spin"
RENDER_ENDFRAME = "dialog.render.endframe.spin"
RENDER_PADDING = "dialog.render.padding.spin"
RENDER_SCALE = "dialog.render.scale.spin"
RENDER_PATH = "dialog.render.path.edit"
RENDER_BROWSE = "dialog.render.browse.button"
RENDER_AUTORANGE = "dialog.render.autorange.button"
RENDER_PREFIX = "dialog.render.prefix.edit"
RENDER_POSTFIX = "dialog.render.postfix.edit"
RENDER_PREVIEW = "dialog.render.preview.label"
RENDER_STATUS = "dialog.render.status.label"
RENDER_RENDER = "dialog.render.render.button"
RENDER_DONE = "dialog.render.done.button"

# Load Sequence Manager (Cmd+L) — modal QDialog.
LOAD_WINDOW            = "dialog.loadwindow"
LOAD_WINDOW_LOAD_ALL   = "dialog.loadwindow.button.loadAll"
LOAD_STRIP_FMT         = "dialog.loadwindow.strip.{idx}"           # idx in 0..3
LOAD_FILENAME_FMT      = "dialog.loadwindow.strip.{idx}.filename"
LOAD_BROWSE_FMT        = "dialog.loadwindow.strip.{idx}.browse"
LOAD_RECENT_FMT        = "dialog.loadwindow.strip.{idx}.recent"
LOAD_FROM_FMT          = "dialog.loadwindow.strip.{idx}.from"
LOAD_TO_FMT            = "dialog.loadwindow.strip.{idx}.to"
LOAD_SCALE_FMT         = "dialog.loadwindow.strip.{idx}.scale"
LOAD_BITDEPTH_FMT      = "dialog.loadwindow.strip.{idx}.bitdepth"
LOAD_CHANNELS_FMT      = "dialog.loadwindow.strip.{idx}.channels"
LOAD_CROP_FMT          = "dialog.loadwindow.strip.{idx}.crop"
LOAD_RELOAD_FMT        = "dialog.loadwindow.strip.{idx}.reload"
LOAD_UNLOAD_FMT        = "dialog.loadwindow.strip.{idx}.unload"
LOAD_ESTIMATES_FMT     = "dialog.loadwindow.strip.{idx}.estimates"
LOAD_HEADER_FMT        = "dialog.loadwindow.strip.{idx}.header"

# Preferences
PREFERENCES_DIALOG = "preferences.dialog"
PREFERENCES_SIDEBAR = "preferences.sidebar"
PREFERENCES_DONE = "preferences.done.button"
PREFERENCES_CANCEL = "preferences.cancel.button"

# Preferences → Engine panel widgets. PREFS_* prefix mirrors the
# object-name pattern the PR-introducing-this-feature uses on the
# widget side ("prefs.engine.<field>") — note the leaf objectName
# differs from the `preferences.engine.*.combo` convention used by
# pre-existing Engine widgets.
PREFS_DEFAULT_DECODE_FILTER = "prefs.engine.defaultDecodeFilter"


def plate(plate_id: int, role: str) -> str:
    """Build a per-plate locator: plate(0, 'gamma.spin') -> 'plate.0.gamma.spin'."""
    return f"plate.{plate_id}.{role}"

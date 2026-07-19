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
# Default bit depth moved to Preferences → Playback & Engine
# (preferences.engine.bitdepth.combo); status-bar visibility toggles via
# View → Show Status Bar (menu.view.statusbar).

# Docks
DOCK_PLATEMANAGER = "dock.platemanager"
DOCK_TIMELINE = "dock.timeline"
DOCK_LUTS = "dock.luts"

# Timeline widgets (inside the timeline dock). The scrubber and the
# track-rows lane are single painted QWidgets — their sub-regions are
# not separate AX elements, so only the container objectNames resolve.
TIMELINE_SCRUBBER = "timeline.scrubber"
TIMELINE_TRACKS = "timeline.tracks"
TIMELINE_THUMBS_TOGGLE = "transport.thumbnails.toggle"

# Menus / actions. Note: macOS folds Qt's QMenuBar into the system
# menu bar, which lives in a separate AX tree from the application
# window — `by_object_name(menu.*)` does NOT resolve via Mac2.
# These constants are kept as documentation of the canonical
# objectNames; functional menu testing happens via the keyboard
# shortcut bound to each action instead.
MENU_FILE = "menu.file"
MENU_FILE_LOAD = "menu.file.load"
MENU_FILE_SAVE_SESSION = "menu.file.savesession"
MENU_FILE_OPEN_SESSION = "menu.file.opensession"
MENU_FILE_RECENT = "menu.file.recent"
MENU_FILE_PREFERENCES = "menu.file.preferences"
MENU_FILE_QUIT = "menu.file.quit"
MENU_VIEW = "menu.view"
MENU_VIEW_FULLSCREEN = "menu.view.fullscreen"
MENU_VIEW_CCFAVORITES = "menu.view.ccfavorites"
MENU_VIEW_HISTOGRAM = "menu.view.histogram"            # Ctrl+H (active quad)
MENU_VIEW_HISTOGRAM_ALL = "menu.view.histogramall"     # Ctrl+Alt+H (all plates)
MENU_VIEW_STATUSBAR = "menu.view.statusbar"            # checkable: show/hide status bar
MENU_HELP = "menu.help"
# NOTE: Qt's macOS NSMenuItem AX bridge does NOT propagate QAction's
# objectName — every menu item appears with `identifier='qt_itemFired:'`
# under Mac2. Look up menu items by `title` instead (see test_minspecs.py
# for the elementType==54 + title pattern). The QAction objectNames
# below are kept for future Linux/Windows runs where Qt's QMenu does
# expose them via AT-SPI / UIA.
MENU_HELP_ABOUT = "menu.help.about"
MENU_HELP_SPECS = "menu.help.specs"
MENU_HELP_MANUAL = "menu.help.manual"            # F1
MENU_HELP_QUICKSTART = "menu.help.quickstart"
MENU_HELP_ISSUES = "menu.help.issues"            # GitHub issue tracker
MENU_HELP_ONSCREEN = "menu.help.onscreen"        # on-screen help overlay toggle

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
# LUT preview/inspector (the 1D curve / 3D cube canvases are painted/GL
# widgets whose content isn't AX-addressable — only the container + toggle
# resolve; behavior is verified manually).
LUT_PREVIEW = "lut.preview"
LUT_PREVIEW_TOGGLE = "lut.preview.toggle"

# FX panel (combined effect-controls for the active plate — replaces the
# old separate FX Stack browser). "+ Add FX" menu button at top, a list of
# per-FX cards (active checkbox + remove button + inline param editors),
# drag-to-reorder. Per-FX object names are templated by stack index N:
#   fxparams.fx{N}.active.check, fxparams.fx{N}.remove.button,
#   fxparams.fx{N}.param.{name}.{spin|check|combo|value}
DOCK_FXPARAMS = "dock.fxparams"
FXPARAMS_PANEL = "fxparams.panel"
FXPARAMS_STATUS = "fxparams.status.label"
FXPARAMS_ADD = "fxparams.addfx.button"
FXPARAMS_LIST = "fxparams.list"

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

# Playlist dock (PlaylistPanel_Qt). Button object names follow the
# playlist.button.* scheme; checkboxes/combo/status added in the rewritten
# panel. Old names (playlist.add.button / playlist.remove.button /
# playlist.up.button / playlist.down.button / playlist.clear.button /
# playlist.save.button / playlist.load.button) are removed.
# Per-card leaves (templated by item index i):
#   playlist.card.<i>         — the PlaylistItemCard QWidget
#   playlist.card.<i>.name    — item name label
#   playlist.card.<i>.chevron — expand/collapse toggle
#   playlist.card.<i>.remove  — per-card remove button
DOCK_PLAYLIST           = "dock.playlist"
PLAYLIST_PANEL          = "playlist.panel"
PLAYLIST_LIST           = "playlist.list"
PLAYLIST_ADD_CURRENT    = "playlist.button.addcurrent"
PLAYLIST_ADD_FILES      = "playlist.button.addfiles"
PLAYLIST_REMOVE         = "playlist.button.remove"
PLAYLIST_UP             = "playlist.button.up"
PLAYLIST_DOWN           = "playlist.button.down"
PLAYLIST_CLEAR          = "playlist.button.clear"
PLAYLIST_LOAD           = "playlist.button.load"
PLAYLIST_SAVE           = "playlist.button.save"
PLAYLIST_COMPACT        = "playlist.check.compact"
PLAYLIST_FULLPATHS      = "playlist.check.fullpaths"
PLAYLIST_AUTOADVANCE    = "playlist.check.autoadvance"
PLAYLIST_LOOP           = "playlist.check.loop"
PLAYLIST_SCALEOVERRIDE  = "playlist.check.scaleoverride"
PLAYLIST_SCALECOMBO     = "playlist.combo.scale"
PLAYLIST_STATUS         = "playlist.status.label"

# Render dialog (PR-39a — minimal: quadrant/format/range/scale/path/prefix)
DIALOG_RENDER = "dialog.render"
RENDER_QUADRANT = "dialog.render.quadrant.combo"
RENDER_FORMAT = "dialog.render.format.combo"
RENDER_QUALITY_STACK = "dialog.render.quality.stack"
RENDER_JPEG_QUALITY = "dialog.render.jpegquality.spin"
RENDER_PNG_LEVEL = "dialog.render.pnglevel.spin"
RENDER_TIFF_COMP = "dialog.render.tiffcomp.combo"
RENDER_EXR_DEPTH = "dialog.render.exrdepth.combo"
RENDER_EXR_COMP = "dialog.render.exrcomp.combo"
RENDER_PNG_BITDEPTH = "dialog.render.pngbitdepth.combo"     # 8 / 16-bit
RENDER_TIFF_BITDEPTH = "dialog.render.tiffbitdepth.combo"   # 8 / 16-bit
RENDER_JPEG_PROGRESSIVE = "dialog.render.jpegprogressive.check"
RENDER_JPEG_SUBSAMPLING = "dialog.render.jpegsubsampling.combo"
RENDER_VIDEO_FPS = "dialog.render.videofps.spin"
RENDER_VIDEO_QUALITY = "dialog.render.videoquality.spin"
RENDER_VIDEO_BITRATE_MODE = "dialog.render.videobitratemode.combo"
RENDER_VIDEO_BITRATE = "dialog.render.videobitrate.spin"
RENDER_VIDEO_PRESET = "dialog.render.videopreset.combo"
RENDER_STARTFRAME = "dialog.render.startframe.spin"
RENDER_ENDFRAME = "dialog.render.endframe.spin"
RENDER_PADDING = "dialog.render.padding.spin"
RENDER_RESOLUTION = "dialog.render.resolution.combo"   # Source / 75% / 50% / 25% / Custom
RENDER_WIDTH = "dialog.render.width.spin"
RENDER_HEIGHT = "dialog.render.height.spin"
RENDER_PATH = "dialog.render.path.edit"
RENDER_BROWSE = "dialog.render.browse.button"
RENDER_AUTORANGE = "dialog.render.autorange.button"
RENDER_PREFIX = "dialog.render.prefix.edit"
RENDER_POSTFIX = "dialog.render.postfix.edit"
RENDER_PREVIEW = "dialog.render.preview.label"
RENDER_PROGRESS = "dialog.render.progress.bar"
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
PREFERENCES_PAGES = "preferences.pages"
PREFERENCES_BUTTONS = "preferences.buttons"
PREFERENCES_DONE = "preferences.done.button"
PREFERENCES_CANCEL = "preferences.cancel.button"

# Preferences → General panel widgets (JEF-16 Task 1/8).
PREFS_GENERAL_BGCOLOR = "preferences.general.bgcolor.button"
PREFS_GENERAL_CHECKERBOARD = "preferences.general.checkerboard.check"
PREFS_GENERAL_BROWSEPATH = "preferences.general.browsepath.edit"
PREFS_GENERAL_BROWSEPATH_BUTTON = "preferences.general.browsepath.button"
PREFS_GENERAL_FULLSCREEN = "preferences.general.fullscreen.check"
PREFS_GENERAL_OPENLOAD_AT_START = "preferences.general.openloadatstart.check"
PREFS_GENERAL_RECOVERY = "preferences.general.recovery.check"
PREFS_GENERAL_STARTUP = "preferences.general.startup.combo"
PREFS_GENERAL_ASPECT_OPACITY = "preferences.general.aspectopacity.spin"
PREFS_GENERAL_THUMBNAILS = "preferences.general.thumbnails.check"
PREFS_GENERAL_FEEDBACK_SIZE = "preferences.general.feedbacksize.spin"
PREFS_GENERAL_FEEDBACK_FADE = "preferences.general.feedbackfade.spin"

# Preferences → Playback & Engine panel widgets. Standardized (JEF-16
# Task 2) onto the `preferences.engine.*.combo` leaf convention used by
# the other Engine widgets.
# renderingEngine / vsync / numOfPartitions / forcePBO controls were removed
# (JEF-16 audit: inert — no runtime effect); balanceReads was dropped entirely
# (superseded by the read-ahead queue cap). Only the queue control remains.
PREFS_ENGINE_QUEUE = "preferences.engine.queue.spin"
PREFS_ENGINE_OIIO_THREADS = "preferences.engine.oiiothreads.spin"
PREFS_DEFAULT_DECODE_FILTER = "preferences.engine.decodefilter.combo"
PREFS_DEFAULT_BIT_DEPTH = "preferences.engine.bitdepth.combo"

# Preferences → Formats panel widgets (JEF-16 Task 3).
PREFS_FORMATS_EXR_IGNORE_DISPLAY = "preferences.formats.exrignoredisplay.check"
PREFS_FORMATS_EXR_IGNORE_ASPECT = "preferences.formats.exrignoreaspect.check"
PREFS_FORMATS_STRAIGHT_ALPHA = "preferences.formats.straightalpha.check"
PREFS_FORMATS_APPLY_ORIENTATION = "preferences.formats.applyorientation.check"

# Preferences → Search Paths panel widgets (JEF-16 Task 4).
PREFS_SEARCH_ENABLE = "preferences.search.enable.check"
PREFS_SEARCH_RECURSIVE = "preferences.search.recursive.check"
PREFS_SEARCH_PATHS_LIST = "preferences.search.paths.list"
PREFS_SEARCH_ADD = "preferences.search.add.button"
PREFS_SEARCH_REMOVE = "preferences.search.remove.button"

# Preferences → Remote panel widgets (JEF-16 Task 5).
PREFS_REMOTE_NICKNAME = "preferences.remote.nickname.edit"
PREFS_REMOTE_CHAT_FADE = "preferences.remote.chatfade.spin"
PREFS_REMOTE_CHAT_AUTOFADE = "preferences.remote.chatautofade.check"
PREFS_REMOTE_CHAT_TEXTBG = "preferences.remote.chattextbg.check"
PREFS_REMOTE_CHAT_FONTSIZE = "preferences.remote.chatfontsize.spin"
PREFS_REMOTE_CHAT_OPACITY = "preferences.remote.chatopacity.spin"
PREFS_REMOTE_CHAT_LINES = "preferences.remote.chatlines.spin"
PREFS_REMOTE_POINTER_FADE = "preferences.remote.pointerfade.spin"
PREFS_REMOTE_POINTER_COLOR = "preferences.remote.pointercolor.button"

# Preferences → Text panel widgets (JEF-16 Task 6). Deferred (Done-writes)
# persistence — see qt_prefs_persist.cpp applyTextPrefs()/PreferencesWindow_Qt
# writeTextPrefs().
PREFS_TEXT_SIZE = "preferences.text.size.spin"
PREFS_TEXT_COLOR = "preferences.text.color.button"
PREFS_TEXT_HINT = "preferences.text.hint.combo"
PREFS_TEXT_FILTER = "preferences.text.filter.combo"
PREFS_TEXT_GAMMA = "preferences.text.gamma.spin"
PREFS_TEXT_SHADOW_ENABLED = "preferences.text.shadowenabled.check"
PREFS_TEXT_SHADOW_OFFSET_X = "preferences.text.shadowoffx.spin"
PREFS_TEXT_SHADOW_OFFSET_Y = "preferences.text.shadowoffy.spin"
PREFS_TEXT_SHADOW_BLUR = "preferences.text.shadowblur.spin"
PREFS_TEXT_SHADOW_COLOR = "preferences.text.shadowcolor.button"


def plate(plate_id: int, role: str) -> str:
    """Build a per-plate locator: plate(0, 'gamma.spin') -> 'plate.0.gamma.spin'."""
    return f"plate.{plate_id}.{role}"

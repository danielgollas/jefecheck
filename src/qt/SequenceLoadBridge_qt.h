// Tiny shim between the Qt UI and the rendering chain's manager
// singletons. Lives in its own TU because the manager headers pull glad,
// and Qt's QOpenGLWidget headers pull system OpenGL — the two refuse to
// share a translation unit on macOS. This file pulls glad; the .cpp pulls
// the chain headers and stays Qt-free.
#ifndef JEFECHECK_QT_SEQUENCE_LOAD_BRIDGE_H
#define JEFECHECK_QT_SEQUENCE_LOAD_BRIDGE_H

#include <string>
#include <vector>

class gfcPlateGUI_Qt;

namespace jefe::qt {

// Wires up gfcPlateGUI_Qt and gfcSequenceGUI_Qt instances on every
// plate / sequence and constructs the gfcPlateManagerGUI_Qt. Safe to
// call once at app startup; calling again would leak the previous GUIs.
void initializeRenderingChain();

// Default texture format (bit depth) used by the Qt drag-drop / Cmd+O
// load path. Mirrors gfcSettings::defaultTextureFormat. The Qt status
// bar's depth combo reads/writes through these accessors so
// MainWindow_qt.cpp doesn't need to include gfcStructures.h directly
// — that header pulls glad and won't share a TU with QtGui on macOS.
int  getDefaultTextureFormat();
void setDefaultTextureFormat(int format);

// Default decode-filter ID used by the OIIO loader's resize path
// (gfcImageLoaderOIIO routes params.filterType into ImageBufAlgo::resize).
// Mirrors gfcSettings::defaultDecodeFilter — exposed via the bridge so
// MainWindow_qt.cpp can restore from QSettings without including
// gfcStructures.h (which drags glad). Values are UIConstants.h FILTER*_ID
// enum entries.
int  getDefaultDecodeFilter();
void setDefaultDecodeFilter(int filterId);

// Walks the install-time LUT path (sett.lutPath, falling back to
// <Resources>/FX/, then ./FX/) and loads every .lut/.cube/.cub/.tga
// via lutManager. Each load may call glGenTextures, so the caller
// MUST make the GL context current before invoking. Called from
// MainWindow_Qt once the QOpenGLWidget is alive.
void initializeInstallLUTs();

// Loads Roboto Regular + Bold from the install bundle's fonts/ dir
// (with a dev-mode fallback to ./fonts/, matching CLAUDE.md's symlink
// convention) and configures the GfcTextRenderer's DPI / shadow
// defaults. The renderer's drawText path short-circuits when no font
// is loaded, so without this every gfc_gl_draw call from gfcPlate
// (plate label, frame number, AOI corner readouts) is a silent no-op.
//
// `dpiScale` is the QOpenGLWidget's devicePixelRatioF() — the renderer
// bakes glyph atlases at fontSize * dpiScale texels and renders in a
// pixel-exact ortho projection, so the value must match the framebuffer's
// physical:logical pixel ratio (2 on macOS Retina).
//
// The bundled fonts are tiny (~170KB each) and FreeType reads them into
// a std::vector — no GL state is touched on first call. The atlas
// caches are cleared on font reload, which DOES call glDeleteTextures,
// so the caller MUST make the GL context current before invoking.
void initializeTextRenderer(float dpiScale);

// Per-frame tick. Drives the playback engine and reads back
// plateManager's "dirty" flag (setChanged was called). Caller should
// schedule this on a QTimer at ~60 Hz and ask the viewport to repaint
// when the return value is true. Cheap when nothing's playing — just
// a timestep update + a flag swap.
bool tickPlayback();

// Hands back the gfcPlateGUI_Qt that gfcPlate reads its rendering
// state from for plate `whichPlate`. PlateCard_Qt binds its widgets
// to this so user edits land on the plate the viewport is drawing,
// not a parallel copy. Returns null until initializeRenderingChain()
// has run, or when the plate index is out of range.
gfcPlateGUI_Qt* getPlateGUIQt(int whichPlate);

// Active-plate selection. The viewport-side keyboard shortcuts (fit,
// flip, flop, track-cycle) target the active plate; clicking a plate
// card calls setActivePlate to choose which one. Out-of-range indices
// are silently ignored.
void setActivePlate(int whichPlate);
int  getActivePlate();

// Transport / timeline hooks. The Qt TimelinePanel widgets call into
// these for play, step, scrub, in/out, FPS, and loop mode; the
// per-tick refreshFromPlayback() reads the current state back so the
// playhead and frame counter advance during playback.
void togglePlayFwd();
void rewindPlayback();
void fastFwdPlayback();
void seekToFrame(int frame);
void setLoopMode(int mode);     // 0 once, 1 loop, 2 bounce
int  getLoopMode();
void setTargetFPS(float fps);
float getTargetFPS();
int  getCurrentFrame();
int  getFromFrame();
int  getToFrame();
int  getInPoint();
int  getOutPoint();
void setInPoint(int frame);
void setOutPoint(int frame);
bool isPlaying();

// LUT browser — backs the Qt LUT dock. Names mirror lutManager's
// public API (getAllNames). loadLUTFile pulls a .lut/.cub/.cube/.tga
// off disk; applyLUTToActivePlate(idx) calls plateManager.setLUT on
// whichever plate is active. autoloadLUTs scans a directory and
// loads everything matching the supported extensions.
void autoloadLUTs(const std::string& path);
std::vector<std::string> getLutNames();
bool loadLUTFile(const std::string& path);
void applyLUTToActivePlate(int guiLutIndex);
void applyLUTToPlate(int plateIdx, int guiLutIndex);
int  getLUTOnActivePlate();

// FX browser / stack — backs the Qt FX Stack dock. Each FX is a
// shader effect (.jfx + .frag/.vert) loaded into fxManager; each
// plate has its own gfcFXStack of selected effects in render order.
//
// initializeInstallFXs scans the standard FX directory (same path
// resolution as initializeInstallLUTs) and calls fxManager.loadFX on
// every .jfx; like LUTs the load compiles GLSL via the ARB shader
// extensions, so a GL context must be current when called.
//
// addFXToActivePlate / removeFXFromActivePlate operate on whichever
// plate is currently active. The stack-side functions
// (getFXStackOnPlate, removeFXFromPlate) take an explicit plate idx
// for tests and the per-card UI.
void initializeInstallFXs();
std::vector<std::string> getAvailableFXNames();
std::vector<std::string> getFXStackOnPlate(int plateIdx);
void addFXToActivePlate(int fxIndex);
void removeFXFromPlate(int plateIdx, int stackIndex);
void clearFXStackOnPlate(int plateIdx);

// FX parameter metadata, flattened across groups in declaration order.
// Mirrors gfcFXWidget without dragging glad/gfcfx.h into Qt translation
// units. The Qt FX param panel renders this list — read-only in PR-38;
// PR-38b will add a setter and per-row editors.
enum class FXParamType {
    Unknown,
    Float,
    Bool,
    Choice,
    Texture,
    Cube,
    LUT,
    Spacer,
    Newline,
    Other,
};

struct FXParamMeta {
    std::string group;
    std::string name;
    std::string label;
    std::string varName;
    std::string tooltip;
    FXParamType type = FXParamType::Unknown;
    float value = 0.0f;
    float minimum = 0.0f;
    float maximum = 0.0f;
    float step = 0.0f;
    float defaultValue = 0.0f;
    std::vector<std::string> options;
};

struct FXMeta {
    std::string name;
    std::string menuName;
    std::string author;
    std::string version;
    std::string description;
    bool active = false;
    bool loadedAndCompiled = false;
    std::vector<FXParamMeta> params;
};

// Returns the FX stack on `plateIdx` with full per-FX param metadata.
// Walks gfcFXStack copies, so this is a snapshot — refresh on
// plateStateChanged or after add/remove. Empty for invalid plate idx.
std::vector<FXMeta> getFXStackMetaOnPlate(int plateIdx);

// Mutates an FX widget value on `plateIdx`'s stack. The Qt FX param
// panel's editor widgets call this on user edit; gfcFX::bind reads
// the new value at the next composite. Float widgets pass `value`
// verbatim, bool widgets pass 0.0 / 1.0, choice widgets pass the
// option index as a float (matching processNetFXAttribInfo). Texture
// /cube/LUT slots are not yet wired — those rebind GL texture handles
// rather than scalar uniforms and will land separately. Calls
// plateManager.setChanged() so the next paintGL repaints.
void setFXParamValueOnPlate(int plateIdx,
                            int fxIndex,
                            const std::string& groupName,
                            const std::string& widgetName,
                            float value);

// Render dialog (PR-39). Qt-friendly mirror of `gfcRenderParams`
// without dragging glad / FLTK headers into the Qt TU. Format mirrors
// the gfcRenderFormats enum: 0=JPEG, 1=EXR, 2=TIFF, 3=TGA, 4=BMP,
// 5=PNG. PR-39a passes a small subset (path / prefix / postfix /
// padding / range / scale / format / quadrant); the format-specific
// quality fields, video codec, and movie-creation pipeline land in
// PR-39b. Defaults match `gfcRenderParams`'s ctor values.
struct RenderParams {
    int quadrant = 0;
    int format   = 0;        // gfcRenderFormats enum value
    int from     = 1;
    int to       = 1;
    int padding  = 4;
    float scale  = 1.0f;
    std::string path;        // output directory (no trailing slash required)
    std::string prefix;
    std::string postfix;
    std::string formatString;  // e.g. "jpg" — drives extension; CreateRenderFilename appends it
};

// Returns a sample filename built from `params` using the existing
// `CreateRenderFilename` helper. Used by the dialog to render a live
// "first frame: …" preview as the user edits path/prefix/format.
std::string previewRenderFilename(const RenderParams& params);

// Synchronously runs `plateManager.renderPlate(params)` on the GUI
// thread. PR-39a accepts that the UI freezes during render — async
// + cancel button comes in PR-39b along with a QThread driver. The
// caller MUST have a current GL context (the Qt mainwindow's
// QOpenGLWidget keeps one current). Returns the count of frames
// the renderer wrote.
int triggerSyncRender(const RenderParams& params);

// Cancel hooks. Today only meaningful from a worker thread (PR-39b);
// stubbed here for symmetry with the FLTK callback shape.
void abortRender();
bool isRendering();

// Remote sessions (PR-41). The Qt RemotePanel uses these to drive
// gfcNetworkManager + the existing gfcNetworkClient/Server GUI
// abstractions. PR-41a only ships the connect/status hooks; chat,
// participant list, and saving the chat log come in PR-41b along
// with a per-event refresh signal so the panel can react to remote
// peers connecting/disconnecting in real time.
//
// `connectAsServer` calls gfcNetworkManager::startServer with the
// supplied params; `connectAsClient` calls startConnection. Both
// silently no-op if a session is already active in the other role.
struct RemoteServerParams {
    std::string serverName;
    int port = 60000;
    std::string password;
};

struct RemoteClientParams {
    std::string clientName;
    std::string serverIP;
    int port = 60000;
    std::string password;
};

void connectAsServer(const RemoteServerParams& params);
void connectAsClient(const RemoteClientParams& params);
void disconnectRemote();
bool isRemoteConnected();
bool isRemoteServer();

// Playlist (PR-40). The Qt PlaylistPanel calls these to drive
// gfcPlaylistManager + gfcTrackManager without dragging glad in.
//
// `getPlaylistItemNames` returns one entry per playlist item; the
// display name is the basename of the item's first track filename
// (full paths visible in tooltips / status bar in PR-40b).
//
// `addPlaylistFile(path)` builds a single-track gfcPlaylistItem via
// `gfcPlaylistManager::createPlaylistItemFrom({path})` and appends.
//
// `loadPlaylistItem(index)` mirrors the FLTK double-click handler —
// `trackManager.setPlaylistItem(playlistManager.getItem(index))` —
// so the chosen playlist entry replaces the active sequences.
std::vector<std::string> getPlaylistItemNames();
void addPlaylistFile(const std::string& path);
void removePlaylistItem(int index);
void movePlaylistItem(int index, int direction);  // -1 up, +1 down
void clearPlaylist();
void loadPlaylistItem(int index);
int  getSelectedPlaylistItem();

// Startup health checks. The main window's "Startup:" status label
// reads these to render Loading / Ready / Errors. Tests poll for
// "Ready" before driving the panel so they don't race the autoload.
//
// Expected counts are computed by walking the install directory (the
// same path initializeInstallLUTs/FXs use); loaded counts query
// fxManager / lutManager. A discrepancy means a .jfx or .lut/.cube
// failed parsing or shader compile — the user gets a status hint, the
// suite catches the regression.
int getExpectedFXCount();
int getLoadedFXCount();
int getExpectedLUTCount();
int getLoadedLUTCount();

// Incremental autoload — used by MainWindow_Qt to drive the LUT/FX
// load one file per QTimer::singleShot(0, ...) iteration. Returning
// to the event loop between each shader compile keeps the main
// thread responsive: the AX system can register the window, paint
// events fire, and Mac2 driver queries (find main window, find
// element by predicate) get answered while the load is still in
// progress. The monolithic autoloadFXsFromPath was blocking the
// main thread for 5-10 seconds — long enough to break the WDA
// launch handshake on test runs.
//
// resolveInstallPath returns the directory the install bundle ships
// LUTs/FXs in, with the same fallback chain as initializeInstallLUTs.
// Empty string means no directory was found. getInstall*Paths return
// the list of files to load, sorted alphabetically. loadOne*File
// performs the load (GL context must be current). finalizeFXLoad
// runs sortFXs + rebuildFXHashMap once after the FX list is fully
// populated.
std::string resolveInstallPath();
std::vector<std::string> getInstallLUTPaths(const std::string& dir);
std::vector<std::string> getInstallFXPaths(const std::string& dir);
void loadOneLUTFile(const std::string& path);
void loadOneFXFile(const std::string& path);
void finalizeFXLoad();

// EXR layer selection — backs the per-plate layer combo on PlateCard_Qt.
// `getLayersOnPlate` returns the EXR layer names the loader discovered
// when the plate's current track was previewed (one entry per layer; an
// empty vector if no sequence is loaded yet, or a single empty-string
// entry for non-EXR files). `getActiveLayerOnPlate` returns the layer
// name currently driving the OIIO loader's channel selection. `setLayer-
// OnPlate` switches the layer and triggers a full async sequence reload
// (matching the FLTK shift-click-on-timeline behavior); plate→track
// resolution happens inside the bridge so the UI doesn't need to know
// about gfcTrackManager.
std::vector<std::string> getLayersOnPlate(int plateIdx);
std::string getActiveLayerOnPlate(int plateIdx);
void setLayerOnPlate(int plateIdx, const std::string& layerName);

// Loads the file into sequence `whichSequence` as a preview frame and
// flips the matching plate's GUI into showPreview mode so the rendering
// chain picks up the new frame on the next draw call. Caller is
// responsible for making the GL context current — generateTexture()
// uploads in the calling thread. Returns true on success.
//
// `kickOffSequenceLoad`: when the previewed file turns out to be part
// of a numbered image sequence (findSequenceFiles found > 1 file),
// also start the async multi-frame load via gfcSequence::startLoading.
// Frames flow into the rawFrames queue; tickPlayback() drains them
// onto the GPU. Defaults to true so dropping a single image of a
// sequence does the obvious thing — load the whole sequence.
// `scale` is a 0..1 multiplier applied to the per-sequence load scale
// (the FLTK loadWindow's scale chooser stored "100", "50", "25").
// Drag-drop maps Shift = 0.5 and Shift+Cmd = 0.25; plain drop and
// Cmd+O pass 1.0. Out-of-range values clamp to (0, 1].
bool loadFileIntoPlate(const std::string& path,
                       int whichSequence,
                       bool kickOffSequenceLoad = true,
                       float scale = 1.0f);

// Pan / zoom hooks called from GlViewport_Qt's mouse handlers. They
// drive a specific plate's transform through plateManager. dx/dy are
// the per-event delta in pixels; zoomDelta is the wheel scroll amount
// (positive zooms in). All three call plateManager.setChanged() so
// the next paintGL pass picks up the change.
void panPlate(int plateIdx, float dx, float dy);
void zoomPlate(int plateIdx, float zoomDelta);

// Hit-test for the plate under viewport pixel (x, y). y is top-down
// in Qt's coord system. Falls back to 0 in single-plate mode.
int plateAtViewportPos(int x, int y, int viewportW, int viewportH);

// Basename of the sequence currently bound to plate `plateIdx`, or
// empty string if no frame has been loaded yet. Reads through
// trackManager → gfcSequence::filenameGeneric, which is the source
// path the loader populates from --open-file or drag-and-drop. Used
// by the status-bar "Loaded:" label and by behavioral tests that
// need to verify the load actually reached gfcSequence.
std::string getLoadedSequenceName(int plateIdx);

// Keyboard-shortcut hooks. `framingMode` is one of UIConstants.h's
// FRAMING*_ID values; the others operate on the currently-active
// plate (or all plates when the *All variant is used). All of them
// flag plateManager dirty so the next paintGL picks up the change.
void setFramingMode(int framingMode);
void fitActivePlate();
void fitAllPlates();
void toggleFlipActive();
void toggleFlopActive();
void toggleFlipAll();
void toggleFlopAll();
void cycleTrackOnActivePlate(int direction);  // -1 prev, +1 next

// Cycle the active plate's text-overlay mode through 0=off, 1=basic,
// 2=extended (filename + resolution + format + EXIF metadata). The
// `*All` variant cycles every plate in lockstep; both the active and
// all-plate paths reset to mode 0 if all plates were already past
// mode 2, mirroring the FLTK behavior. textMode is per-plate so
// users can keep one plate clean while another shows metadata.
void toggleTextModeActive();
void toggleTextModeAll();

// Plate reset shortcuts. `resetActivePlate` clears every per-plate
// override on the active plate (zoom, pan, rotation, flip/flop,
// channel masks, color correction) — same scope as the FLTK Ctrl+R.
// `resetAllPlates` runs that across the four plates. The color-only
// variants leave transformations alone and only reset gamma /
// exposure / contrast / brightness / saturation, mirroring FLTK's
// Shift+R behavior. All four flag plateManager dirty so the next
// paint shows the cleaned state, and call refreshAllCards via the
// caller so the plate-card spinboxes pick up the new values.
void resetActivePlate();
void resetAllPlates();
void resetActiveColorCorrection();
void resetAllColorCorrections();

// Set the displayed track for `plateIdx` directly. Mirrors
// `applyLUTToPlate` — drives plateManager.setTrackOnPlate (which
// updates both the GUI and the plate's internal track field) and
// flags the manager dirty so the next paintGL picks up the new
// sequence. The plate-card's track combo routes through this so
// combo changes actually swap the rendered sequence; without it the
// GUI value object updates but gfcPlate::track stays stale.
void setTrackOnPlate(int plateIdx, int trackIdx);

// Track currently bound to plate `plateIdx` (0..3 for tracks A..D),
// reading through plateManager so it reflects post-bridge state.
// Returns -1 for invalid indices.
int getTrackOnPlate(int plateIdx);

// Playback control. Only pause is wired today; oneFrameFwd / Rev
// are no-ops until the Qt build runs the playback manager loop, but
// stubbing them here lets the keyboard handler stay symmetric with
// the FLTK reference.
void pausePlayback();
void stepFrame(int direction);  // -1 reverse, +1 forward

}  // namespace jefe::qt

#endif

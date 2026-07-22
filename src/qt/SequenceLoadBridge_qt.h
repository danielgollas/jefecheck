// Tiny shim between the Qt UI and the rendering chain's manager
// singletons. Lives in its own TU because the manager headers pull glad,
// and Qt's QOpenGLWidget headers pull system OpenGL — the two refuse to
// share a translation unit on macOS. This file pulls glad; the .cpp pulls
// the chain headers and stays Qt-free.
#ifndef JEFECHECK_QT_SEQUENCE_LOAD_BRIDGE_H
#define JEFECHECK_QT_SEQUENCE_LOAD_BRIDGE_H

#include <functional>
#include <string>
#include <utility>
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

// Decoupled playback tick (preferred over tickPlayback). Splits the
// per-tick work into a cheap, no-GL timing step and an expensive,
// GL-bound texture upload so the timer can run at a high rate (for tight
// FPS pacing) without paying makeCurrent/doneCurrent on every tick.
//
//   tickPlaybackTiming()       — advances currentFrame at the target FPS,
//                                updates animations and track widgets, and
//                                returns the plateManager dirty flag. No GL
//                                context required; safe to call at 250+ Hz.
//   hasPendingTextureUploads() — true when any track has decoded frames
//                                queued for GPU upload. Gate the GL trio on
//                                this so makeCurrent only runs when needed.
//   uploadPendingTextures()    — drains the rawFrames queues onto GL
//                                textures. Caller MUST make the viewport's
//                                GL context current first.
bool tickPlaybackTiming();
bool hasPendingTextureUploads();
void uploadPendingTextures();

// Cheap predicate — true when the playback engine is actively playing
// OR when at least one track has frames waiting in its rawFrames queue
// that need to be uploaded to GL. When both are false, the 60Hz timer
// callback in MainWindow_qt.cpp can skip the makeCurrent/tickPlayback
// pair entirely — saves ~60 GL-context switches per second at idle.
bool needsPlaybackTick();
bool hasActiveViewportAnimation();

// Destructively reads plateManager's dirty flag. The idle timer calls this
// when NOT ticking playback/animation so a bare setChanged() (any state edit
// whose call site didn't force its own viewport repaint — e.g. a mirrored
// remote change) still repaints once while playback is stopped.
bool consumePlateChanged();

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
void setPlayDirection(int direction);   // FLTK '.' = +1 fwd, ',' = -1 rev
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
void setInPointAndLoad();   // FLTK Alt+I: set in point + reload tracks there
bool isPlaying();

// Histogram overlay (Ctrl+H active quad / Ctrl+Alt+H all plates). Toggles
// gfcPlate's in-viewport draggable histogram sub-window; the render path
// already draws it when visible.
void toggleHistogramActiveQuad();
void toggleHistogramAll();

// On-screen help overlay (FLTK's bare 'h' toggleHelp; bare H is flop in
// Qt, so this is a Help-menu item with no single-key shortcut).
void toggleOnScreenHelp();

// Color-pick dispatch for in-viewport overlays (the draggable histogram
// sub-window). Coordinates are framebuffer pixels, bottom-left origin
// (the caller applies the device-pixel-ratio + Y-flip). The down/drag
// calls return nonzero when an overlay consumed the event, so the caller
// can suppress its own pan/zoom for that drag. The caller MUST have the
// viewport GL context current (these run a selection render + read-back).
// Source pixel size of the given plate's current frame (0 if nothing loaded).
// The render dialog uses it to seed/scale the output-resolution controls.
void getRenderSourceSize(int quadrant, int& w, int& h);

int  viewportPickDown(int xFb, int yFb, bool ctrl, bool alt, bool shift);
int  viewportPickDrag(int xFb, int yFb, int dxFb, int dyFb,
                      bool ctrl, bool alt, bool shift);
void viewportPickUp(int xFb, int yFb, bool ctrl, bool alt, bool shift);

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

// Per-plate slot writes from PlateCard_Qt only update the Qt plate GUI
// (e.g. `gui->setGamma(v)`). The actual gfcPlate fields are mirrored
// from the GUI via `updateValueFromGUI`; without an explicit propagate
// call after each edit, the super-shader rebuild never sees the new
// value and color-correction controls silently do nothing. Call this
// after any direct `gui->setX(...)` write that should affect rendering
// (gamma, exposure, BCS, flip/flop, RGBA mask, scale, pan, rotation).
//
// Cheap — iterates 4 plates and calls each plate's updateValuesFromGUI.
// Does NOT touch layout (framingMode) or active-quad selection, unlike
// the older updateAllFromGUI helper that has its own use sites.
void propagatePlateChanges();

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

// Enable / disable an FX in-place on a plate's stack (active checkbox).
// Bounds-checked in gfcFXStack::setActive; flags plateManager dirty so
// the next paintGL re-composites with the new active set.
void setFXActiveOnPlate(int plateIdx, int fxIndex, bool active);

// Drag-to-reorder: move the FX at `from` to position `to` on the plate's
// stack (erase + insert semantics, not an adjacent swap). No-op for
// invalid/equal indices; flags plateManager dirty.
void moveFXOnPlate(int plateIdx, int from, int to);

// Categorized "+" menu source. Returns one entry per available FX where
// `.first` is the fxManager index to pass to addFXToActivePlate and
// `.second` is that FX's menuName ("Category/Subcategory/Name"), falling
// back to its plain name when menuName is empty. The ordering/indexing
// matches getAvailableFXNames() and addFXToActivePlate (both walk
// fxManager's post-sortFXs fxArray), so .first is a valid fxIndex.
std::vector<std::pair<int, std::string>> getAvailableFXMenu();

// Choices for an FX cube (3D LUT) / lut (1D LUT) param picker. Each pair is
// (globalLutIndex, displayName). The global index is what gfcFX::bind() reads
// and what setFXParamValueOnPlate stores as the param value — NOT the list
// position (display order ≠ stored value). Mirrors the FLTK Fl_Choice fed by
// lutManager.get3DLutNames() / get1DLutNames().
std::vector<std::pair<int, std::string>> getCubeLutChoices();
std::vector<std::pair<int, std::string>> getLut1DChoices();

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
    int outWidth  = 0;       // target output resolution (0 = source size)
    int outHeight = 0;
    std::string path;        // output directory (no trailing slash required)
    std::string prefix;
    std::string postfix;
    std::string formatString;  // e.g. "jpg" — drives extension; CreateRenderFilename appends it
    // Format-specific quality knobs. Defaults mirror gfcRenderParams's ctor
    // so a caller that doesn't set them keeps the prior behavior.
    int jpegQuality     = 95;   // 0..100
    bool jpegProgressive = false;
    int jpegSubsampling = 0;    // 0 4:4:4, 1 4:2:2, 2 4:2:0
    int pngQuality      = 6;    // zlib level 0..9
    int tiffCompression = 0;    // 0 LZW, 1 none, 2 zip
    int exrCompression  = 3;    // index into the EXR compression list (zip)
    int exrFormat       = 0;    // GFC_HALF=0, GFC_FLOAT=1
    int bitsPerChannel  = 8;    // 8 or 16 (PNG/TIFF)
    bool bakeCropBars   = false; // burn aspect/crop letterbox bars into output
};

// Returns a sample filename built from `params` using the existing
// `CreateRenderFilename` helper. Used by the dialog to render a live
// "first frame: …" preview as the user edits path/prefix/format.
std::string previewRenderFilename(const RenderParams& params);

// Synchronously renders `params`'s frame range on the GUI thread. The
// caller MUST have a current GL context (the Qt mainwindow's
// QOpenGLWidget keeps one current). Returns the count of frames written.
//
// `onProgress(framesDone, framesTotal)` is invoked after each frame so the
// dialog can drive a progress bar (it calls processEvents to repaint). The
// render runs one frame per renderPlate call to make that granular.
int triggerSyncRender(const RenderParams& params,
                      const std::function<void(int, int)>& onProgress = {});

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

// JEF-27 cloud coordinator mode. Cloud host: dials the coordinator URL and
// asks it to create a session (the port is ignored); the assigned short code
// is surfaced via remoteSessionCode() once it arrives. Cloud client: dials the
// coordinator and joins by that code. Both funnel into
// gfcNetworkManager::startServer / startConnection with coordinatorMode set.
//
// WARNING: connectAsCloudHost BLOCKS for up to ~5s waiting for the coordinator
// to assign the session code (gfcNetworkManager::startServer's bounded wait).
// Callers MUST invoke it OFF the GUI thread (QtConcurrent / a worker) so the Qt
// event loop keeps running, then marshal the result back to the UI thread.
struct RemoteCloudHostParams {
    std::string coordinatorUrl;
    std::string password;
};

struct RemoteCloudJoinParams {
    std::string clientName;
    std::string coordinatorUrl;
    std::string sessionCode;
    std::string password;
};

void connectAsCloudHost(const RemoteCloudHostParams& params);
void connectAsCloudClient(const RemoteCloudJoinParams& params);

// The coordinator-assigned session code (empty when not a cloud host or before
// the code has been assigned). Reads gfcNetworkManager::getAssignedSessionCode.
std::string remoteSessionCode();

void disconnectRemote();

// Headless two-process connection smoke-test helpers (--remote-test).
// Server role: host on `port`, pump for `settleMs` ms, return peak
// participant count. Client role: connect to `ip:port`, hold for `holdMs` ms
// (optionally sending a play message), then return.
void remoteTestPeerConnect(const std::string& ip, int port, int holdMs, bool play,
                           int connectTimeoutMs = 3000);
bool remoteTestServerSawPlay(int port, int settleMs);
// Split-phase host for the WebRTC harness (see .cpp for the one-shot-play
// rationale): start + await the loopback client, then settle for the peer's play.
bool remoteTestServerStart(int port, int loopbackTimeoutMs);
bool remoteTestServerSettleForPlay(int settleMs);
// JEF-27 Task 3: --coord-test cloud-coordinator E2E helpers. Host role: start in
// coordinator mode (create-session), wait for the assigned code + loopback client
// to come up. Peer role: join by code, hold, toggle play. See the .cpp.
bool coordTestHostStart(const std::string& coordUrl, int loopbackTimeoutMs);
std::string coordTestGetCode();
void coordTestPeerJoin(const std::string& coordUrl, const std::string& code,
                       int holdMs, bool play, int connectTimeoutMs = 12000);
bool coordTestSettleForPlay(int settleMs);
bool isRemoteConnected();
bool isRemoteServer();
std::vector<std::string> remoteParticipants();
std::string              remoteStatusText();
std::vector<std::string> remoteChatLog();
std::vector<std::string> remoteErrors();
std::vector<std::string> remoteNetworkLog();
void sendChatMessageText(const std::string& text);
bool pumpNetwork();

struct ChatEntry {
    std::string sender;
    std::string message;
    std::string timeHHMM;
    int  type;     // GFCNETMESSAGETYPE_NORMAL / _SYSTEM / _LOAD
    bool isSelf;
    int  color;    // packed RGB, 0 = unset
};
std::vector<ChatEntry> remoteChatEntries();

// Chat overlay + keyboard chat entry (Task 7).
// drawNetworkOverlay renders the ported networkManager.draw() chat/pointer
// overlay into the current GL context (w, h are framebuffer pixels).
// remoteChatModeActive returns true while the user is composing a message.
// remoteChatBegin / remoteChatCancel start/discard; remoteChatAppend /
// remoteChatBackspace edit; remoteChatSubmit sends and resets.
void drawNetworkOverlay(int w, int h);
bool remoteChatModeActive();
void remoteChatBegin();
void remoteChatCancel();
void remoteChatBackspace();
void remoteChatAppend(const std::string& s);
void remoteChatSubmit();

// Remote pointer broadcast (Task 8). Sends the local cursor position to all
// session peers. xPx/yPx are framebuffer pixel coords with GL bottom-left
// framebuffer pixels, GL bottom-left origin (caller applies dpr scale and
// Y-flip); quadID is the plate under the cursor (dragPlate_). Converts to the
// plate's image space before sending. No-ops when disconnected or quadID < 0.
void sendRemotePointer(int xPx, int yPx, int quadID);

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
// Save/load the whole playlist as a `.jpl` (XML). `savePlaylistFile`
// appends `.jpl` if the path has no extension.
void savePlaylistFile(const std::string& path);
void loadPlaylistFile(const std::string& path);

// Build a playlist item from the CURRENT live setup — every track's load
// params, the per-plate FX stacks, and the full program state (layout,
// playback mode/FPS, in/out, per-plate CC/flip/flop/crop/RGBA, per-track
// offset/hold). Mirrors gfcTrackManager::getPlaylistItem().
void addCurrentAsPlaylistItem();

// Build one multi-track item (A..D) from the given files and append it.
void addPlaylistFiles(const std::vector<std::string>& paths);

// Append more tracks to an existing item (drop-media-on-card).
void appendTracksToPlaylistItem(int index, const std::vector<std::string>& paths);

// Per-track detail for one item, for the card's collapsible body. Plain
// POD so the card TU stays glad-free (no gfc* headers). All fields derive
// from gfcLoadParams.
struct PlaylistTrackDetail {
    std::string letter;       // "A".."D"
    std::string path;         // full path; the panel shortens per toggle
    int  fromFrame = 0;
    int  toFrame = 0;
    int  totalFrames = 0;
    int  scalePct = 100;
    std::string filter;       // "linear" / "bilinear"
    bool crop = false;
    std::string bitDepth;     // "8"/"16"/"16f"/"32f"/"dxt1"
};
std::vector<PlaylistTrackDetail> getPlaylistItemDetail(int index);

// Scale override applied before each playlist load (RAM-limited / remote).
// pct in {25,50,100}; 0 clears the override. Maps to setScaleOverride.
void setPlaylistScaleOverride(int pct);

// Auto-advance support. consumePlaylistAdvanceSignal() returns true exactly
// once when forward playback reaches the end in ONCE mode (edge-detected in
// the playback tick); reading it clears the latch. isPlaylistItemPlayingOnce
// reports whether the current playback mode is ONCE.
// currentContentIsPlaylistItem() returns true only when the content currently
// loaded into the rendering chain was loaded via a playlist entry (either
// loadPlaylistItem or loadPlaylistItemAndPlay). Quick-load (Cmd+O), drag-drop,
// and clearPlaylist all clear this flag so auto-advance never fires against
// content the user loaded independently of the playlist.
bool consumePlaylistAdvanceSignal();
bool isPlaylistItemPlayingOnce();
bool currentContentIsPlaylistItem();

// Load a playlist item and (re)start forward playback from its first frame —
// used by auto-advance. Normal double-click load stays loadPlaylistItem().
void loadPlaylistItemAndPlay(int index);

// Stop playback if currently playing (used to halt at the end of a non-looping
// playlist so the once-mode end-clamp doesn't spin the idle tick).
void pausePlaybackIfPlaying();

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

// Native aspect ratio of the plate's loaded preview frame, formatted
// "W.WW:1" (e.g. "2.39:1"), or empty when no frame is loaded yet. Derived
// from the decoded frame's pixel dimensions. The Aspect control shows this
// next to its "original" entry so the user sees the file's true ratio.
std::string getPlateNativeAspect(int plateIdx);

// Headless self-test for JEF-21: drives every aspect-ratio preset string
// through the real GUI conversion (gfcPlateGUI_Qt::setAspectChoice ->
// aspectFromString) and asserts the resulting renderer aspect uses the
// height/width convention gfcPlate::calculatePolySizesCropEtc expects
// (every landscape "W:H" preset must map to a value in (0,1), not W/H).
// Prints ASPECT-TEST PASS/FAIL and returns true on pass. Needs no GL.
bool runAspectSelfTest();

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

// Gang-pan: pan every plate by the same delta in one shot. The FLTK
// build invokes this on Alt+drag in GlViewport.cpp — the Qt viewport
// mirrors the modifier convention. plateManager.panAllPlates handles
// the iteration internally and calls setChanged once at the end.
void panAllPlates(float dx, float dy);

// Gang-zoom: scale every plate by the same delta in one shot. Wraps
// plateManager.zoomAllPlates. Used by viewport key+drag color paths
// that share the gang-modifier convention.
void zoomAllPlates(float zoomDelta);

// Color-correction deltas. Each call applies `delta` to the named
// field additively (the underlying gfcPlateManager setters take an
// `isDelta` flag; we pass 1 so the value is summed onto the current
// field rather than overwritten). Single-plate variants target
// `plateIdx` and are no-ops when plateIdx < 0. Gang variants hit
// every plate via the matching plateManager.set*All() path. Each
// flags plateManager dirty so the next paintGL repaints.
//
// Used by GlViewport_Qt's W/E/Q/D/S key+drag handlers to mirror
// FLTK GlViewport.cpp's adjustmentValue = (eventX - prevX) * 0.01
// convention (drag right = increase).
void adjustPlateGamma(int plateIdx, float delta);
void adjustPlateExposure(int plateIdx, float delta);
void adjustPlateBrightness(int plateIdx, float delta);
// Aspect / crop on a plate (aspect = content-height/width factor the crop math
// uses; e.g. 0.5 letterboxes a square source). Used by the headless --cc-test
// to exercise the render's crop-bar baking.
void setPlateAspect(int plateIdx, float aspect);
void setPlateCrop(int plateIdx, bool on);
void adjustPlateContrast(int plateIdx, float delta);
void adjustPlateSaturation(int plateIdx, float delta);

void adjustAllPlatesGamma(float delta);
void adjustAllPlatesExposure(float delta);
void adjustAllPlatesBrightness(float delta);
void adjustAllPlatesContrast(float delta);
void adjustAllPlatesSaturation(float delta);

// Drive every plate's showPreview flag from a single writer. Used by
// GlViewport_Qt::setLoadWindowOpen — while the Load Sequence Manager
// is open every plate renders its track's previewFrame; closing the
// dialog flips them all back. Routed through the bridge so the
// viewport TU doesn't have to pull gfcplatemanager.h (which drags
// glad into Qt headers). Calls plateManager.setChanged().
void setAllPlatesShowPreview(bool showPreview);

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

// Publishes the GL framebuffer that represents "the screen" for the current
// context. QOpenGLWidget renders into its own FBO (not 0); the FX/FBO code in
// gfcPlate must rebind THIS to return to screen, or the viewport (and sibling
// plates) go black after an FX pass. GlViewport_Qt::paintGL calls this each
// frame with defaultFramebufferObject().
void setScreenFBO(unsigned fbo);

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

// Per-track load estimates surfaced to the Qt Load Window's strip
// estimates label: total frame count, an approximate decoded-RGBA
// byte budget, and a wall-clock estimate based on the last
// loadPreview() timing. The byte count is bpp * width * height *
// frames using the GUI's currently-selected bit depth.
struct TrackEstimates {
    int    frames;
    size_t bytes;
    float  seconds;
};

TrackEstimates getTrackEstimates(int trackIdx);

// Re-runs loadPreview on the given track. Caller must ensure the
// viewport's GL context is current before invoking. Returns true if
// the preview decoded successfully.
bool reloadTrackPreview(int trackIdx);

// Aborts any in-flight load, clears loaded frames, drops preview,
// resets GUI defaults.
void unloadAndClearTrack(int trackIdx);

// For each track with a non-empty filename, abort any in-flight load
// and (re)start a full sequence load. Returns the number started.
int startLoadingAllTracks();

// Per-track parameter snapshot for the Qt Load Window. Mirrors the
// FLTK loadWindow's per-track widget state without dragging
// gfcSequenceGUI/gfcSequence into Qt translation units. `channel-
// Options` is populated only when the underlying GUI is a
// gfcSequenceGUI_Qt (the channel/layer list the OIIO loader
// discovered on the last preview); `filenameGeneric` is the
// numbered-sequence pattern (e.g. /path/foo.####.dpx) the loader
// derived from the source path.
struct TrackParams {
    std::string filename;
    int from              = 1;
    int to                = 1;
    int scalePct          = 100;
    int compression       = 0;
    int channel           = 0;
    bool crop             = false;
    std::vector<std::string> channelOptions;
    std::string filenameGeneric;
};

TrackParams getTrackParams(int trackIdx);

// Per-track snapshot for the timeline track rows. All frame values are
// in global-timeline coordinates (offset already folded in), so the
// widget can map them straight through the same x<->frame transform the
// scrubber uses. `present` is false for an empty track (draw a
// placeholder lane). `loadedCount` frames starting at `firstLoadedFrame`
// are decoded; the rest of [rangeStart, rangeEnd] is not-yet-loaded.
struct TrackTimelineState {
    bool        present          = false;
    int         rangeStart       = 1;
    int         rangeEnd         = 1;
    int         offset           = 0;
    int         numFrames        = 0;
    int         firstLoadedFrame = 1;
    int         loadedCount      = 0;
    std::string label;
};

TrackTimelineState getTrackTimelineState(int trackIdx);

// Frame offset of a track on the global timeline. setTrackOffset shifts
// the whole sequence left/right (drag / "Set offset..." popup) and flags
// the viewport dirty so it repaints at the new frame mapping.
int  getTrackOffset(int trackIdx);
void setTrackOffset(int trackIdx, int offset);

// Begin decoding the track's assigned sequence starting at `frame`
// (timeline coordinates). Wraps trackManager.startLoadingSequenceAt;
// the loader thread fills rawFrames and the playback tick uploads them,
// so no GL context is required here. No-op for an empty track.
void startLoadingTrackAt(int trackIdx, int frame);

// Hold-last-frame mode (right-click popup). gfcSequence::getHoldMode()
// is an int; the widget only needs on/off, so the bridge translates:
// getTrackHoldMode == (getHoldMode() != 0); setTrackHoldMode(true) ->
// setHoldMode(1), (false) -> setHoldMode(0).
bool getTrackHoldMode(int trackIdx);
void setTrackHoldMode(int trackIdx, bool hold);

void setTrackFilename(int trackIdx, const std::string& path);
void setTrackFrom(int trackIdx, int v);
void setTrackTo(int trackIdx, int v);
void setTrackScalePct(int trackIdx, int pct);  // 100 / 50 / 25
void setTrackCompression(int trackIdx, int compEnum);
void setTrackChannel(int trackIdx, int channelIdx);
void setTrackCrop(int trackIdx, bool on);

// A copy of a frame's filmstrip thumbnail for the timeline widget.
// present=false when thumbnails are off, the track/frame is invalid, or
// that frame hasn't decoded yet. rgba is tightly-packed RGBA8 (wrap as
// QImage::Format_RGBA8888).
struct ThumbPixels {
    bool present = false;
    int  w = 0;
    int  h = 0;
    std::vector<unsigned char> rgba;
};

ThumbPixels getTrackThumbnail(int track, int frameIndex);

bool getThumbnailsEnabled();        // reads sett.showThumbnails
void setThumbnailsEnabled(bool on); // writes sett.showThumbnails; setChanged()

// Qt-safe snapshot of a LUT for the LUT-panel preview. The bridge .cpp
// (which includes trilerp.h) copies the sample data out so the widget
// never touches CubeLUT/glad. guiLutIndex is the LUT-panel row:
// 0 = "(No LUT)" → valid=false; row r>=1 → lutManager.getLUT(r-1).
struct LutPreviewData {
    bool        valid    = false;
    int         type     = 0;      // CubeLUT::LUTTYPES
    bool        is3D     = false;  // type != JEFECHECK1D
    int         size     = 0;      // samples (1D) or cube edge (3D)
    int         fromBits = 0;
    int         toBits   = 0;
    float       max1D    = 1.0f;
    std::string name;
    // 1D: `size` output samples (raw, in [0, max1D]).
    std::vector<float> curve1D;
    // 3D: structured cube grid (with adjacency, for faces/lattice/dots).
    // `cubeSize` is the working edge after any subsample; `cubeRGB` holds
    // cubeSize^3 nodes × 3 floats = the node's clamped output RGB, x-major:
    // node (x,y,z) at index ((x*cubeSize + y)*cubeSize + z) * 3.
    int                cubeSize = 0;
    std::vector<float> cubeRGB;
};

LutPreviewData getLutPreview(int guiLutIndex);

// Lightweight per-LUT metadata for the sortable LUT browser table (no cube
// data copied). One entry per loaded LUT, in lutManager order; the panel's
// gui index for entry i is (i + 1) (row 0 is the "(No LUT)" slot).
struct LutSummary {
    std::string name;
    bool is3D     = false;
    int  size     = 0;
    int  fromBits = 0;
    int  toBits   = 0;
};
std::vector<LutSummary> getLutSummaries();

// --- Session save/restore ---------------------------------------------------
// loadSession / loadRecoverySession decode preview frames, so the CALLER must
// make the viewport's GL context current first (the bridge is context-free).
bool saveSession(const std::string& path);
bool loadSession(const std::string& path);
// Recovery / last-session file (getApplicationDataPath()+recoverySession.jcs).
bool getHasRecoverableSession();
bool loadRecoverySession();     // loads it; does NOT delete (caller decides)
void writeRecoverySession();    // writes it unconditionally
void removeRecoverySession();
// Recent sessions (manager keeps sett.recentSessions, cap 5).
std::vector<std::string> getRecentSessions();
void setRecentSessions(const std::vector<std::string>& paths);
// Recent playlists (JEF-18: sett.recentPlaylists, cap 5). loadPlaylistFile /
// savePlaylistFile push onto this automatically; these are for QSettings
// seed/read-back at startup/close.
std::vector<std::string> getRecentPlaylists();
void setRecentPlaylists(const std::vector<std::string>& paths);
// Startup-session preference (0 Empty / 1 Reopen / 2 Ask).
int  getStartupSessionBehavior();
void setStartupSessionBehavior(int mode);
// General prefs (JEF-16 Task 1) — read-only accessors so MainWindow_qt.cpp
// can apply "start in fullscreen" / "open Load window at startup" and seed
// the Quick Load dialog's default directory without including
// gfcStructures.h (which drags glad, incompatible with QOpenGLWidget in
// this TU — see developer_notes.md §1).
bool getStartFullscreen();
bool getOpenLoadWindowAtStartup();
std::string getDefaultBrowsePath();
// Color-correction favorites (slot 0..4, on the active plate).
void saveCCFavoriteFromActive(int slot);
void applyCCFavoriteToActive(int slot);
bool saveCCFavoritesFile(const std::string& path);
bool loadCCFavoritesFile(const std::string& path);
std::string getFavoritesFilePath();   // getApplicationDataPath()+favorites.jcs

}  // namespace jefe::qt

#endif

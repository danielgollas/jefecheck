// Implementation lives in a Qt-free TU so it can pull the manager
// headers (which include glad) without colliding with Qt's QOpenGLWidget
// system-OpenGL include path.
#include "SequenceLoadBridge_qt.h"

#include "../gfcplatemanager.h"
#include "../gfcplaybackmanager.h"
#include "../gfctrackmanager.h"
#include "../gfclutmanager.h"
#include "../gfcfxmanager.h"
#include "../gfcfxstack.h"
#include "../gfcStructures.h"
#include "../gfcrenderparams.h"
#include "../gfcplaylistmanager.h"
#include "../gfcplaylistitem.h"
#include "../gfcnetworkmanager.h"
#include "../gfcSequence.h"
#include "../gfcsequencegui.h"
#include "../gfcTextRenderer.h"
#include "../ui/IApplication.h"
#include "gfcplategui_qt.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

extern gfcPlateManager plateManager;
extern gfcPlaybackManager playbackManager;
extern gfcPlaylistManager playlistManager;
extern gfcTrackManager trackManager;
extern gfcLUTManager lutManager;
extern gfcFXManager fxManager;
extern gfcNetworkManager networkManager;
extern gfcSettings sett;

namespace jefe::qt {

int getDefaultTextureFormat() {
    return sett.defaultTextureFormat;
}

void setDefaultTextureFormat(int format) {
    sett.defaultTextureFormat = format;
}

void initializeRenderingChain() {
    // FLTK's main.cpp probes GL_ARB_shader_objects + GL_EXT_framebuffer_object
    // and writes these flags. The Qt build skips that probe but uses the
    // same render path (gfcPlate::draw, super-shader, gfcFX::load), all of
    // which check the flags. Set them unconditionally — every macOS /
    // Linux / Windows GL stack the Qt build supports has both extensions.
    // Without this, gfcFX::load returns early with "FXs not Supported"
    // and fxArray stays empty no matter how many .jfx files we hand it.
    sett.glsl = true;
    sett.fbo = true;

    plateManager.initializeWidgets();
    trackManager.initializeWidgets();
    playbackManager.initializeWidgets();

    // FLTK's readSettings populates sett.lutPath and pre-loads LUTs from
    // the install / env path; that whole function is FLTK-gated. Until
    // we factor a Qt-friendly version, default the path here so the LUT
    // browser has content to show on first run. Any preferences the
    // user saves later override this on the next launch.
    // Defer LUT autoload — it walks every .lut/.cub on disk and each
    // calls glGenTextures, which requires a current GL context. The
    // viewport's QOpenGLWidget context isn't ready until after the
    // window is shown and the first paintGL fires. MainWindow_Qt calls
    // initializeInstallLUTs() once the GL context is alive.
    if (sett.lutPath.empty()) {
        sett.lutPath = ::getApplicationDataPath() + "FX/";
    }
}

void initializeTextRenderer(float dpiScale) {
    namespace fs = std::filesystem;
    // Path resolution mirrors the FX/LUT autoload: prefer the bundled
    // path, fall back to the dev-mode `./fonts/` symlink. FLTK's
    // main.cpp tries both in the same order; we keep the convention so
    // a CLAUDE.md-style dev setup (symlink fonts → src/fonts) keeps
    // working in the Qt build too.
    const std::string bundled = ::getApplicationDataPath() + "fonts/";
    const std::string dev     = "fonts/";
    const std::string regularName = "Roboto-Regular.ttf";
    const std::string boldName    = "Roboto-Bold.ttf";

    auto tryLoadRegular = [&](const std::string& dir) -> bool {
        const std::string p = dir + regularName;
        std::error_code ec;
        if (!fs::exists(p, ec)) return false;
        return textRenderer().loadFont(p);
    };
    auto tryLoadBold = [&](const std::string& dir) -> bool {
        const std::string p = dir + boldName;
        std::error_code ec;
        if (!fs::exists(p, ec)) return false;
        return textRenderer().loadBoldFont(p);
    };

    if (!tryLoadRegular(bundled)) tryLoadRegular(dev);
    if (!tryLoadBold(bundled))    tryLoadBold(dev);

    // DPI scale drives the atlas bake size (fontSize * dpiScale texels)
    // so glyphs stay crisp on Retina. The shadow offset is in physical
    // pixels — 1 logical pixel down-right matches FLTK's default and
    // gives plate labels a readable drop without the blurry feel of a
    // larger blur radius.
    textRenderer().setDPIScale(dpiScale);
    textRenderer().setShadowEnabled(true);
    textRenderer().setShadowOffset(dpiScale, -dpiScale);
    textRenderer().setShadowColor(0, 0, 0, 0.5f);
    textRenderer().setShadowBlur(0);
}

void initializeInstallLUTs() {
    // LUT autoload path resolution. Try, in order:
    //   1. sett.lutPath (whatever readSettings or the prefs window saved)
    //   2. <bundle Resources>/FX/  (release/install convention)
    //   3. ./FX/                   (CLAUDE.md dev-mode symlink)
    // First match wins. Logged so the user can tell from the terminal
    // which one the binary picked up.
    namespace fs = std::filesystem;
    std::vector<std::string> candidates;
    if (!sett.lutPath.empty()) candidates.push_back(sett.lutPath);
    candidates.push_back(::getApplicationDataPath() + "FX/");
    candidates.push_back("FX/");
    std::string chosen;
    std::error_code ec;
    for (const auto& p : candidates) {
        if (p.empty()) continue;
        if (fs::exists(p, ec) && fs::is_directory(p, ec)) {
            chosen = p;
            break;
        }
    }
    if (chosen.empty()) {
        fprintf(stderr, "[jefecheck] No LUT/FX directory found. Tried:\n");
        for (const auto& p : candidates) {
            fprintf(stderr, "    - %s\n", p.c_str());
        }
        return;
    }
    fprintf(stderr, "[jefecheck] Loading LUTs/FX from %s\n", chosen.c_str());
    sett.lutPath = chosen;
    autoloadLUTs(chosen);
    plateManager.updateAllGUILUTWidgets();
}

bool tickPlayback() {
    // playbackManager.update() is the heart of the playback engine —
    // advances currentFrame at target FPS, calls plateManager.setChanged()
    // when a new frame should display.
    playbackManager.update();

    // Drives the per-plate flip/flop rotations and any other smoothly-
    // animated state. Without this, gfcPlate::updateRot never runs, so
    // pressing H/V toggles the flag but rX/rY stay at 0 and the
    // rotation never plays — flip/flop look like no-ops.
    plateManager.updateAnimations();

    // Drain one frame from each sequence's rawFrames queue and upload
    // it to a GL texture. The loader thread fills the queue; this call
    // is what makes those frames visible to the renderer. Cheap when
    // queues are empty (early return on try_lock fail). Caller MUST
    // make the viewport's GL context current before calling — texture
    // uploads happen on the calling thread.
    trackManager.generateTextures();

    // After uploading, walk the plates to flip any that are still in
    // showPreview mode but whose sequence now has at least one playback
    // frame on the GPU. The drop / loadFileIntoPlate path enables
    // showPreview so the previewFrame paints immediately; without this
    // flip the plate would never advance to the playback path
    // (gfcPlate::getFrameAndSequence's `if (showPreview)` branch never
    // resolves). FLTK gets the flip for free because its
    // `getShowPreview()` reads `loadWindow->visible()` — closing the
    // Load dialog implicitly switches the plate. Qt has no Load dialog,
    // so we have to drive the transition explicitly here.
    //
    // Single-frame "sequences" stay in showPreview = true: the preview
    // frame *is* the displayed image, and the playback path would just
    // re-load the same texture for no visual benefit. Detected by
    // numPreviewFrames == 1 (multi-image discovery from
    // findSequenceFiles).
    for (int p = 0; p < GFC_MAX_PLATES; ++p) {
        auto* gui = dynamic_cast<gfcPlateGUI_Qt*>(plateManager.getPlateGUI(p));
        if (!gui || !gui->getShowPreview()) continue;
        const int trackIdx = plateManager.getTrackOnPlate(p);
        auto* seq = trackManager.getSequence(trackIdx);
        if (!seq) continue;
        if (seq->getNumPreviewFrames() <= 1) continue;
        if (seq->getRangeEnd() <= 0) continue;  // no frames uploaded yet
        plateManager.setPlateShowPreview(p, false);
        plateManager.setChanged();
    }

    // Pushes per-track widget state (visible range, current frame, etc.)
    // into the gfcSequenceGUI for each sequence. In the Qt build the
    // GUI methods are mostly stubs today, but calling this keeps the
    // surface symmetric with FLTK so wiring a real timeline later is
    // a matter of filling in stubs, not threading the call.
    trackManager.updateTrackWidgets();

    // Drain the dirty flag here so callers know when to redraw without
    // having to query plateManager themselves. Any input path that calls
    // setChanged() (drop, keys, mouse, playback frame advance) gets
    // picked up here.
    const bool dirty = plateManager.getChanged();
    return dirty;
}

gfcPlateGUI_Qt* getPlateGUIQt(int whichPlate) {
    return dynamic_cast<gfcPlateGUI_Qt*>(plateManager.getPlateGUI(whichPlate));
}

void setActivePlate(int whichPlate) {
    plateManager.setActiveQuad(whichPlate);
    plateManager.setChanged();
}

int getActivePlate() {
    return plateManager.getActiveQuad();
}

void togglePlayFwd() {
    if (playbackManager.isPlaying()) {
        playbackManager.pause();
    } else {
        playbackManager.startPlayFwd();
    }
    plateManager.setChanged();
}

void rewindPlayback() {
    playbackManager.rew();
}

void fastFwdPlayback() {
    playbackManager.ffwd();
}

void seekToFrame(int frame) {
    playbackManager.setCurrentFrame(frame);
    plateManager.setChanged();
}

void setLoopMode(int mode) {
    playbackManager.setPlaybackMode(mode);
}

int getLoopMode() {
    return playbackManager.getPlaybackMode();
}

void setTargetFPS(float fps) {
    playbackManager.setTargetFPS(fps);
}

float getTargetFPS() {
    return playbackManager.getTargetFPS();
}

int getCurrentFrame() {
    return playbackManager.getCurrentFrame();
}

int getFromFrame() {
    return playbackManager.getFromFrame();
}

int getToFrame() {
    return playbackManager.getToFrame();
}

int getInPoint() {
    return playbackManager.getInPoint();
}

int getOutPoint() {
    return playbackManager.getOutPoint();
}

void setInPoint(int frame) {
    playbackManager.setInPoint(frame);
}

void setOutPoint(int frame) {
    playbackManager.setOutPoint(frame);
}

bool isPlaying() {
    return playbackManager.isPlaying() != 0;
}

void autoloadLUTs(const std::string& path) {
    // FLTK's loadLUTsFromPath uses fl_filename_list and is gated to the
    // FLTK build. The Qt build uses std::filesystem instead so we don't
    // pull FLTK back in just to enumerate a directory.
    namespace fs = std::filesystem;
    if (path.empty()) return;
    std::error_code ec;
    if (!fs::exists(path, ec) || !fs::is_directory(path, ec)) return;

    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        // Lower-case to make the match case-insensitive.
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (ext == ".lut" || ext == ".cub" || ext == ".cube" || ext == ".tga") {
            files.push_back(entry.path().string());
        }
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        lutManager.loadLUT(f);
        jefe::ui::IApplication::instance().processEvents();
    }
    fprintf(stderr, "[jefecheck] Loaded %zu LUT file(s) from %s\n",
            files.size(), path.c_str());
    fflush(stderr);
}

namespace {
// Count files in `path` whose lower-cased extension is in `exts`.
// Used by the expected-count checks; cheap (one directory scan).
int countFilesByExt(const std::string& path,
                    std::initializer_list<const char*> exts) {
    namespace fs = std::filesystem;
    if (path.empty()) return 0;
    std::error_code ec;
    if (!fs::exists(path, ec) || !fs::is_directory(path, ec)) return 0;
    int n = 0;
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        for (const char* want : exts) {
            if (ext == want) { ++n; break; }
        }
    }
    return n;
}

// Walk `path` and ask fxManager to load every .jfx file. fxManager's
// loadFX compiles GLSL via ARB shader extensions, so a GL context
// must be current before calling — same constraint as autoloadLUTs.
// Files are loaded in alphabetical order so the user-facing list is
// stable across runs.
//
// Yields the Qt event loop between each load. Without this, the
// 35-shader compile pass blocks the main thread for 3-5 seconds
// straight; AX queries (and the WDA "find main window" handshake
// that precedes them) time out, breaking back-to-back test launches.
void autoloadFXsFromPath(const std::string& path) {
    namespace fs = std::filesystem;
    if (path.empty()) return;
    std::error_code ec;
    if (!fs::exists(path, ec) || !fs::is_directory(path, ec)) return;

    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(path, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (ext == ".jfx") files.push_back(entry.path().string());
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        fxManager.loadFX(f);
        // Pump pending events so AX queries get answered between
        // shader compiles. Cheap when the queue is empty.
        jefe::ui::IApplication::instance().processEvents();
    }
    fxManager.sortFXs();
    fxManager.rebuildFXHashMap();
    fprintf(stderr, "[jefecheck] Loaded %zu FX file(s) from %s\n",
            files.size(), path.c_str());
    fflush(stderr);
}
}  // namespace

int getExpectedFXCount() {
    return countFilesByExt(sett.lutPath, {".jfx"});
}

int getLoadedFXCount() {
    // fxManager has no public count getter; getMenuNames is cheap
    // (just iterates fxArray) and returns a vector we can size.
    return (int)fxManager.getMenuNames().size();
}

int getExpectedLUTCount() {
    // .tga / image-based LUT loading is disabled in this build (see
    // CLAUDE.md "Known issues" — trilerp.cpp IMAGELUT2D returns -1
    // pending OIIO image reading). Counting .tga files in the
    // expected total would always show a mismatch on a healthy
    // install, so we exclude them.
    return countFilesByExt(sett.lutPath, {".lut", ".cub", ".cube"});
}

int getLoadedLUTCount() {
    return (int)lutManager.getAllNames().size();
}

std::string resolveInstallPath() {
    namespace fs = std::filesystem;
    std::vector<std::string> candidates;
    if (!sett.lutPath.empty()) candidates.push_back(sett.lutPath);
    candidates.push_back(::getApplicationDataPath() + "FX/");
    candidates.push_back("FX/");
    std::error_code ec;
    for (const auto& p : candidates) {
        if (p.empty()) continue;
        if (fs::exists(p, ec) && fs::is_directory(p, ec)) {
            sett.lutPath = p;
            return p;
        }
    }
    return {};
}

std::vector<std::string> getInstallLUTPaths(const std::string& dir) {
    namespace fs = std::filesystem;
    if (dir.empty()) return {};
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return {};
    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        // Skip .tga — image-based LUT loading is disabled (see
        // getExpectedLUTCount).
        if (ext == ".lut" || ext == ".cub" || ext == ".cube") {
            files.push_back(entry.path().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::string> getInstallFXPaths(const std::string& dir) {
    namespace fs = std::filesystem;
    if (dir.empty()) return {};
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return {};
    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (ext == ".jfx") files.push_back(entry.path().string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

void loadOneLUTFile(const std::string& path) {
    const int before = (int)lutManager.getAllNames().size();
    lutManager.loadLUT(path);
    const int after = (int)lutManager.getAllNames().size();
    if (after == before) {
        // lutManager.loadLUT prints a generic "Error loading LUT" with
        // no filename; tag it with which file we were trying so the
        // user can fix the .cube/.lut/.cub on disk (or strip it from
        // the install dir).
        fprintf(stderr, "[jefecheck] LUT failed to parse: %s\n",
                path.c_str());
        fflush(stderr);
    }
}

void loadOneFXFile(const std::string& path) {
    fxManager.loadFX(path);
}

void finalizeFXLoad() {
    fxManager.sortFXs();
    fxManager.rebuildFXHashMap();
}

void initializeInstallFXs() {
    // Reuses sett.lutPath for resolution — historically LUTs and FXs
    // ship from the same FX/ directory in the install bundle and the
    // dev tree (see CLAUDE.md). initializeInstallLUTs sets sett.lutPath
    // to the chosen directory; we simply scan it again for .jfx files.
    if (!sett.lutPath.empty()) autoloadFXsFromPath(sett.lutPath);
}

std::vector<std::string> getAvailableFXNames() {
    return fxManager.getMenuNames();
}

std::vector<std::string> getFXStackOnPlate(int plateIdx) {
    auto* stack = plateManager.getFXStack(plateIdx);
    if (!stack) return {};
    std::vector<std::string> names;
    const int n = stack->getNumOfFXs();
    names.reserve(n);
    for (int i = 0; i < n; ++i) {
        gfcFX fx = stack->getFX(i);
        names.push_back(fx.menuName.empty() ? fx.name : fx.menuName);
    }
    return names;
}

void addFXToActivePlate(int fxIndex) {
    const int q = plateManager.getActiveQuad();
    if (q < 0) return;
    auto* stack = plateManager.getFXStack(q);
    if (!stack) return;
    // fxManager::getFX returns a copy — gfcFXStack::addFX takes by
    // value and stores its own copy. Out-of-range indices return a
    // default-constructed gfcFX (no shaders), which renders as a
    // pass-through; cheap to detect by checking name afterwards but
    // not worth the extra bridge round-trip in the common path.
    gfcFX fx = fxManager.getFX(fxIndex);
    if (fx.name.empty() && fx.menuName.empty()) return;
    stack->addFX(fx);
    plateManager.setChanged();
}

void removeFXFromPlate(int plateIdx, int stackIndex) {
    auto* stack = plateManager.getFXStack(plateIdx);
    if (!stack) return;
    auto all = stack->getAllFXs();
    if (stackIndex < 0 || stackIndex >= (int)all.size()) return;
    // gfcFXStack lacks a single-FX remove — clear the stack and
    // re-add everything except the removed entry. Cheap; stacks rarely
    // exceed a few FXs and addFX is just a vector push.
    stack->clearStack();
    for (int i = 0; i < (int)all.size(); ++i) {
        if (i == stackIndex) continue;
        stack->addFX(all[i]);
    }
    plateManager.setChanged();
}

void clearFXStackOnPlate(int plateIdx) {
    auto* stack = plateManager.getFXStack(plateIdx);
    if (!stack) return;
    stack->clearStack();
    plateManager.setChanged();
}

namespace {
FXParamType mapWidgetType(GFC_FX_GUI_TYPE t) {
    switch (t) {
        case FX_GUI_FLOAT:   return FXParamType::Float;
        case FX_GUI_BOOL:    return FXParamType::Bool;
        case FX_GUI_CHOICE:  return FXParamType::Choice;
        case FX_GUI_TEXTURE: return FXParamType::Texture;
        case FX_GUI_CUBE:    return FXParamType::Cube;
        case FX_GUI_LUT:     return FXParamType::LUT;
        case FX_GUI_SPACER:  return FXParamType::Spacer;
        case FX_GUI_NEWLINE: return FXParamType::Newline;
        case FX_GUI_UNKNOWN: return FXParamType::Unknown;
        default:             return FXParamType::Other;
    }
}
}  // namespace

void setFXParamValueOnPlate(int plateIdx,
                            int fxIndex,
                            const std::string& groupName,
                            const std::string& widgetName,
                            float value) {
    auto* stack = plateManager.getFXStack(plateIdx);
    if (!stack) return;
    stack->setWidgetValue(fxIndex, groupName, widgetName, value);
    plateManager.setChanged();
}

namespace {
gfcRenderParams toCoreRenderParams(const RenderParams& src) {
    gfcRenderParams p;
    p.quadrant     = src.quadrant;
    p.format       = src.format;
    p.formatString = src.formatString;
    p.from         = src.from;
    p.to           = src.to;
    p.frame        = src.from;
    p.padding      = src.padding;
    p.scale        = src.scale;
    p.path         = src.path;
    p.prefix       = src.prefix;
    p.postfix      = src.postfix;
    return p;
}
}  // namespace

std::string previewRenderFilename(const RenderParams& params) {
    gfcRenderParams p = toCoreRenderParams(params);
    return CreateRenderFilename(p);
}

int triggerSyncRender(const RenderParams& params) {
    gfcRenderParams p = toCoreRenderParams(params);
    std::vector<std::string> rendered;
    plateManager.renderPlate(p, &rendered);
    return static_cast<int>(rendered.size());
}

void abortRender() {
    plateManager.abortRender();
}

bool isRendering() {
    return plateManager.isRendering();
}

namespace {
std::string playlistItemDisplayName(const gfcPlaylistItem& item) {
    if (item.loadParams.empty()) return "(empty item)";
    const std::string& path = item.loadParams[0].fileName;
    if (path.empty()) return "(unnamed)";
    auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
}  // namespace

std::vector<std::string> getPlaylistItemNames() {
    std::vector<std::string> names;
    auto* entries = playlistManager.getPlaylist();
    if (!entries) return names;
    names.reserve(entries->size());
    for (const auto& item : *entries) {
        names.push_back(playlistItemDisplayName(item));
    }
    return names;
}

void addPlaylistFile(const std::string& path) {
    if (path.empty()) return;
    auto item = playlistManager.createPlaylistItemFrom({path});
    playlistManager.addItemlist(item);
}

void removePlaylistItem(int index) {
    auto* entries = playlistManager.getPlaylist();
    if (!entries || index < 0 || index >= (int)entries->size()) return;
    playlistManager.removePlaylistItem(index);
}

void movePlaylistItem(int index, int direction) {
    auto* entries = playlistManager.getPlaylist();
    if (!entries || index < 0 || index >= (int)entries->size()) return;
    playlistManager.movePlaylistItem(index, direction);
}

void clearPlaylist() {
    playlistManager.clearPlaylist();
}

void loadPlaylistItem(int index) {
    auto* entries = playlistManager.getPlaylist();
    if (!entries || index < 0 || index >= (int)entries->size()) return;
    trackManager.setPlaylistItem(playlistManager.getItem(index));
    playlistManager.setSelectedItem(index);
}

int getSelectedPlaylistItem() {
    return playlistManager.selectedItem;
}

void connectAsServer(const RemoteServerParams& params) {
    if (networkManager.getConnected()) return;
    gfcServerParams sp;
    std::snprintf(sp.serverName, sizeof(sp.serverName), "%s",
                  params.serverName.c_str());
    std::snprintf(sp.password,   sizeof(sp.password),   "%s",
                  params.password.c_str());
    sp.port = params.port;
    networkManager.startServer(&sp);
}

void connectAsClient(const RemoteClientParams& params) {
    if (networkManager.getConnected()) return;
    gfcConnectionParams cp;
    cp.serverIP  = params.serverIP;
    cp.port      = params.port;
    cp.password  = params.password;
    cp.nickname  = params.clientName;
    networkManager.startConnection(&cp);
}

void disconnectRemote() {
    if (!networkManager.getConnected()) return;
    // gfcNetworkManager::stopConnection is declared but never defined
    // — the FLTK side never wired client-side disconnect either, and
    // RakNet itself tears the client peer down on app exit. Server-
    // side stop is supported. PR-41b will fill in the client-side
    // disconnect path (likely via the existing peer destructor).
    if (networkManager.getIsServer()) {
        networkManager.stopServer();
    }
}

bool isRemoteConnected() {
    return networkManager.getConnected();
}

bool isRemoteServer() {
    return networkManager.getIsServer();
}

std::vector<FXMeta> getFXStackMetaOnPlate(int plateIdx) {
    auto* stack = plateManager.getFXStack(plateIdx);
    if (!stack) return {};
    std::vector<FXMeta> result;
    const int n = stack->getNumOfFXs();
    result.reserve(n);
    for (int i = 0; i < n; ++i) {
        gfcFX fx = stack->getFX(i);
        FXMeta meta;
        meta.name              = fx.name;
        meta.menuName          = fx.menuName;
        meta.author            = fx.author;
        meta.version           = fx.version;
        meta.description       = fx.description;
        meta.active            = fx.active;
        meta.loadedAndCompiled = fx.loadedAndCompiled;
        // Walk groups in insertion order — the gfcFX side stores
        // widgetsOrder per group but std::map iteration order on
        // group names is alphabetical by key. The FLTK control
        // window walked groups by name too, so this matches.
        for (auto& kv : fx.groups) {
            const std::string& groupName = kv.first;
            gfcFXWidgetGroup& g = kv.second;
            for (const std::string& wname : g.widgetsOrder) {
                auto it = g.widgets.find(wname);
                if (it == g.widgets.end()) continue;
                const gfcFXWidget& w = it->second;
                FXParamMeta p;
                p.group        = groupName;
                p.name         = wname;
                p.label        = w.label;
                p.varName      = w.varName;
                p.tooltip      = w.tooltip;
                p.type         = mapWidgetType(w.type);
                p.value        = w.value;
                p.minimum      = w.minimum;
                p.maximum      = w.maximum;
                p.step         = w.step;
                p.defaultValue = w.defaultValue;
                p.options      = w.options;
                meta.params.push_back(std::move(p));
            }
        }
        result.push_back(std::move(meta));
    }
    return result;
}

std::vector<std::string> getLutNames() {
    return lutManager.getAllNames();
}

bool loadLUTFile(const std::string& path) {
    if (path.empty()) return false;
    lutManager.loadLUT(path);
    return true;
}

void applyLUTToActivePlate(int guiLutIndex) {
    // The Qt panel uses GUI-style indexing: row 0 = "(No LUT)",
    // row 1+ = lutManager array entries offset by 1. gfcPlate::setLUT
    // takes the raw lutManager index (-1 means "no LUT" — getLUT(-1)
    // returns an empty CubeLUT with texture id 0, which gives gfcPlate
    // a no-bind path).
    const int q = plateManager.getActiveQuad();
    if (q < 0) return;
    plateManager.setLUT(q, guiLutIndex - 1);
    plateManager.setChanged();
}

void applyLUTToPlate(int plateIdx, int guiLutIndex) {
    if (plateIdx < 0) return;
    plateManager.setLUT(plateIdx, guiLutIndex - 1);
    plateManager.setChanged();
}

int getLUTOnActivePlate() {
    // Returns the GUI index (matches the Qt panel's row).
    const int q = plateManager.getActiveQuad();
    if (q < 0) return 0;
    auto* gui = plateManager.getPlateGUI(q);
    return gui ? gui->getLUT() : 0;
}

void panPlate(int plateIdx, float dx, float dy) {
    if (plateIdx < 0) return;
    plateManager.panPlate(plateIdx, dx, dy);
    plateManager.setChanged();
}

void zoomPlate(int plateIdx, float zoomDelta) {
    if (plateIdx < 0) return;
    plateManager.zoomPlate(plateIdx, zoomDelta);
    plateManager.setChanged();
}

int plateAtViewportPos(int x, int y, int viewportW, int viewportH) {
    return plateManager.getPlateAtPosition(x, y, viewportW, viewportH);
}

void setFramingMode(int framingMode) {
    plateManager.setFramingMode(framingMode);
    plateManager.setChanged();
}

void fitActivePlate() {
    const int q = plateManager.getActiveQuad();
    if (q < 0) return;
    plateManager.fitToViewport(q);
    plateManager.setChanged();
}

void fitAllPlates() {
    plateManager.fitToViewportAll();
    plateManager.setChanged();
}

void toggleFlipActive() {
    const int q = plateManager.getActiveQuad();
    if (q < 0) return;
    plateManager.toggleFlip(q);
    plateManager.setChanged();
}

void toggleFlopActive() {
    const int q = plateManager.getActiveQuad();
    if (q < 0) return;
    plateManager.toggleFlop(q);
    plateManager.setChanged();
}

void toggleFlipAll() {
    plateManager.toggleFlipAll();
    plateManager.setChanged();
}

void toggleFlopAll() {
    plateManager.toggleFlopAll();
    plateManager.setChanged();
}

void toggleTextModeActive() {
    const int q = plateManager.getActiveQuad();
    if (q < 0) return;
    plateManager.toggleTextMode(q);
    plateManager.setChanged();
}

void toggleTextModeAll() {
    plateManager.toggleTextModeAll();
    plateManager.setChanged();
}

void resetActivePlate() {
    const int q = plateManager.getActiveQuad();
    if (q < 0) return;
    plateManager.resetPlate(q);
    plateManager.setChanged();
}

void resetAllPlates() {
    plateManager.resetAllPlates();
    plateManager.setChanged();
}

void resetActiveColorCorrection() {
    const int q = plateManager.getActiveQuad();
    if (q < 0) return;
    plateManager.resetColorCorrection(q);
    plateManager.setChanged();
}

void resetAllColorCorrections() {
    plateManager.resetAllColorCorrections();
    plateManager.setChanged();
}

void cycleTrackOnActivePlate(int direction) {
    const int q = plateManager.getActiveQuad();
    if (q < 0) return;
    int track = plateManager.getTrackOnPlate(q);
    track += (direction >= 0 ? 1 : -1);
    if (track < 0) track = 3;
    if (track > 3) track = 0;
    plateManager.setTrackOnPlate(q, track);
    plateManager.setChanged();
}

void setTrackOnPlate(int plateIdx, int trackIdx) {
    if (plateIdx < 0 || trackIdx < 0 || trackIdx > 3) return;
    plateManager.setTrackOnPlate(plateIdx, trackIdx);
    plateManager.setChanged();
}

int getTrackOnPlate(int plateIdx) {
    if (plateIdx < 0) return -1;
    return plateManager.getTrackOnPlate(plateIdx);
}

void pausePlayback() {
    playbackManager.pause();
}

void stepFrame(int direction) {
    if (direction >= 0) {
        playbackManager.oneFrameFwd();
    } else {
        playbackManager.oneFrameRev();
    }
}

std::string getLoadedSequenceName(int plateIdx) {
    if (plateIdx < 0) return {};
    const int track = plateManager.getTrackOnPlate(plateIdx);
    auto* seq = trackManager.getSequence(track);
    if (!seq) return {};
    // previewFrame.loaded is the cheap signal that loadPreview ran;
    // filenameGeneric holds the source path (single image or
    // numbered-sequence pattern). Empty = nothing's been loaded into
    // this track's sequence yet — the launch-default state for any
    // plate that --open-file didn't target.
    if (!seq->getPreviewFrame().loaded) return {};
    namespace fs = std::filesystem;
    fs::path p(seq->filenameGeneric);
    return p.filename().string();
}

bool loadFileIntoPlate(const std::string& path,
                       int whichSequence,
                       bool kickOffSequenceLoad,
                       float scale) {
    auto* seq = trackManager.getSequence(whichSequence);
    if (!seq || !seq->myGUI || path.empty()) {
        return false;
    }
    seq->myGUI->setFilename(path);

    // Apply the global default bit depth before loadPreview reads it.
    // gfcSequenceGUI::setCompression takes one of the
    // GFC_*BPC / GFC_*HALF enum values; gfcSettings::defaultTextureFormat
    // stores that enum directly (default GFC_16HALF).
    seq->myGUI->setCompression(sett.defaultTextureFormat);

    // Translate the 0..1 scale factor to the percentage string the
    // FLTK Choice widget convention expects ("100", "50", "25"). Clamp
    // to (0, 1] so a stray 0 or negative doesn't get sent through and
    // a > 1.0 doesn't try to upsample (the loader doesn't support it).
    if (scale <= 0.0f) scale = 1.0f;
    if (scale > 1.0f) scale = 1.0f;
    int pct = int(scale * 100.0f + 0.5f);
    // Defend against sub-1% inputs that round to 0 — the float
    // clamp catches scale<=0 but not scale=0.001 → "0%". The loader
    // has no defined behavior for 0% scale.
    if (pct < 1)   pct = 100;
    if (pct > 100) pct = 100;
    char scaleBuf[8];
    std::snprintf(scaleBuf, sizeof(scaleBuf), "%d", pct);
    seq->myGUI->setScale(scaleBuf);

    const std::string loaded = seq->loadPreview();
    if (loaded.empty()) {
        return false;
    }

    plateManager.setPlateShowPreview(whichSequence, true);

    // gfcPlate::showPreview, scale, track, channel masks etc. are read
    // from members that updateValuesFromGUI copies out of myGUI. The
    // FLTK build calls this once on startup; the Qt build never does,
    // so the plate would otherwise keep its uninitialized showPreview
    // and never render the preview frame we just loaded.
    plateManager.updateAllFromGUI();

    // loadPreview() runs findSequenceFiles, which clamps from_/to_ on
    // the gfcSequenceGUI_Qt to the discovered range. If that range is
    // > 1, kick off the async multi-frame loader so playback can step
    // through the whole sequence. Going through trackManager (rather
    // than seq->startLoading directly) also bumps playbackManager's
    // from/to to the track length, so Left/Right + Space land on real
    // frames. The thread uses try_lock on the rawFrames queue; the
    // per-tick generateTextures() drain (PR-16) puts frames on the
    // GPU. Without this, dropping an EXR stack would only ever show
    // frame 1.
    if (kickOffSequenceLoad && seq->getNumPreviewFrames() > 1) {
        trackManager.startLoadingSequence(whichSequence);
    }

    return true;
}

}  // namespace jefe::qt

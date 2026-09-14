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
#include "../gfcsessionmanager.h"
#include "../gfcreview.h"
#include "../gfcrevision.h"
#include "../gfcnote.h"
#include "../gfcNoteStore.h"
#include "../gfcNoteOverlay.h"
#include "../gfcNoteStamp.h"
#include "../gfcpickmanager.h"
#include "../xmlParser.h"
#include "../gfcSequence.h"
#include "../gfcsequencegui.h"
#include "../gfcTextRenderer.h"
#include "../ui/IApplication.h"
#include "gfcplategui_qt.h"
#include "gfcsequencegui_qt.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <thread>

extern gfcPlateManager plateManager;
extern gfcPlaybackManager playbackManager;
extern gfcPlaylistManager playlistManager;
extern gfcTrackManager trackManager;
extern gfcLUTManager lutManager;
extern gfcFXManager fxManager;
extern gfcNetworkManager networkManager;
extern gfcSessionManager sessionManager;
extern gfcPickManager pickManager;
extern gfcSettings sett;

namespace jefe::qt {

int getDefaultTextureFormat() {
    return sett.defaultTextureFormat;
}

void setDefaultTextureFormat(int format) {
    sett.defaultTextureFormat = format;
}

int getDefaultDecodeFilter() {
    return sett.defaultDecodeFilter;
}

void setDefaultDecodeFilter(int filterId) {
    sett.defaultDecodeFilter = filterId;
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

    // Wire the color-pick subsystem (FLTK did this in main.cpp). Without
    // it nothing registers with pickManager, so in-viewport overlays like
    // the histogram window never receive click/drag events. Plates are a
    // fixed-size vector built in the gfcPlateManager ctor, so they exist.
    pickManager.registerDrawee(&plateManager);
    pickManager.registerNotifee(&plateManager);
    plateManager.registerPlatesAsPickNotifees();
}

namespace {
// Build the gfcPickFlags bitfield doPicking expects. BUTTON1 is always set
// (we only dispatch picks for the left button); ctrl/alt/shift map to the
// GFC_PICK_MODIFIER_* bits.
unsigned int pickFlags(bool ctrl, bool alt, bool shift) {
    unsigned int f = GFC_PICK_MODIFIER_BUTTON1;
    if (ctrl)  f |= GFC_PICK_MODIFIER_CTRL;
    if (alt)   f |= GFC_PICK_MODIFIER_ALT;
    if (shift) f |= GFC_PICK_MODIFIER_SHIFT;
    return f;
}
}  // namespace

void getRenderSourceSize(int quadrant, int& w, int& h) {
    plateManager.getPlateSourceSize(quadrant, w, h);
}

int viewportPickDown(int xFb, int yFb, bool ctrl, bool alt, bool shift) {
    return pickManager.doPicking(GFC_PICK_EVENT_CLICK_DOWN,
                                 pickFlags(ctrl, alt, shift), xFb, yFb, 0, 0);
}

int viewportPickDrag(int xFb, int yFb, int dxFb, int dyFb,
                     bool ctrl, bool alt, bool shift) {
    return pickManager.doPicking(GFC_PICK_EVENT_DRAG,
                                 pickFlags(ctrl, alt, shift),
                                 xFb, yFb, dxFb, dyFb);
}

void viewportPickUp(int xFb, int yFb, bool ctrl, bool alt, bool shift) {
    pickManager.doPicking(GFC_PICK_EVENT_CLICK_UP,
                          pickFlags(ctrl, alt, shift), xFb, yFb, 0, 0);
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

bool needsPlaybackTick() {
    // Playback engine actively advancing — must tick to keep currentFrame
    // moving and to call plateManager.setChanged() each frame.
    if (playbackManager.isPlaying()) return true;
    // Any sequence with decoded-but-not-yet-uploaded frames also needs a
    // tick so trackManager.generateTextures() can drain them onto the GPU.
    // hasPendingRawFrames() is an O(1) queue::empty() check.
    for (int i = 0; i < 4; ++i) {
        gfcSequence* seq = trackManager.getSequence(i);
        if (seq && seq->hasPendingRawFrames()) return true;
    }
    // A fading feedback message (zoom/reset/flip overlay) or a settling
    // flip/flop rotation needs the tick to keep animating + repainting while
    // playback is stopped — otherwise it only appears/updates on the next
    // forced repaint (mouse move, play).
    if (plateManager.hasActiveAnimations()) return true;
    // The remote chat/status overlay fading out (or chat entry) needs the tick
    // to keep repainting while playback is stopped, same as plate animations.
    if (networkManager.overlayAnimating()) return true;
    return false;
}

// Any time-based animation that needs the viewport repainted every tick even
// when playback is stopped and no new frame is dirty: settling flip/flop,
// fading remote-pointer trails, or the fading chat/status overlay.
bool hasActiveViewportAnimation() {
    return plateManager.hasActiveAnimations() || networkManager.overlayAnimating();
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

namespace {
// Auto-advance edge state. ONCE mode clamps currentFrame at endLimit and
// leaves isPlaying() true (the playback manager never stops itself), so the
// "reached end" event is the transition prevFrame != endLimit -> currentFrame
// == endLimit while playing forward in ONCE mode. Latched here, consumed by
// the idle tick via consumePlaylistAdvanceSignal().
int  gPrevPlaybackFrame = -1;
bool gPlaylistAdvanceLatch = false;
// True only when the currently-playing content was loaded via a playlist
// entry (loadPlaylistItem / loadPlaylistItemAndPlay). Cleared by quick-load,
// drag-drop (loadFileIntoPlate), and clearPlaylist so auto-advance can't
// hijack unrelated playback.
bool gCurrentContentFromPlaylist = false;

// Mirror of gfcPlaybackManager::getEndLimit() (which is private) using only
// public accessors, so auto-advance can read the effective forward end
// boundary without promoting an engine method. Kept in sync with the engine
// definition in gfcplaybackmanager.cpp.
int playlistEffectiveEndLimit() {
    int tmp = 1;
    switch (playbackManager.getLoopPriority()) {
        case GFC_LOOPPRIORITY_SHORTEST:
            tmp = trackManager.getFirstLastLoaded() + 1; break;
        case GFC_LOOPPRIORITY_LONGEST:
            tmp = trackManager.getLastLastLoaded() + 1; break;
        case GFC_LOOPPRIORITY_TIMELINE:
            tmp = playbackManager.getOutPoint(); break;
    }
    const int out = playbackManager.getOutPoint();
    return (tmp < out) ? tmp : out;
}
}  // namespace

bool tickPlaybackTiming() {
    // No-GL half of tickPlayback(). playbackManager.update() advances
    // currentFrame at the target FPS off the wall clock, so running this
    // at a high rate (e.g. 250 Hz) lets a frame advance land within a few
    // ms of its true time instead of being quantized to a coarse tick —
    // this is what keeps the measured FPS pinned at the target instead of
    // wobbling. None of these calls touch the GL context.
    playbackManager.update();
    // Edge-detect once-mode end-of-playback for playlist auto-advance.
    if (playbackManager.isPlaying() &&
        playbackManager.getPlaybackMode() == LOOPMODEONCE_ID) {
        const int cur = playbackManager.getCurrentFrame();
        const int end = playlistEffectiveEndLimit();
        if (cur == end && gPrevPlaybackFrame != end && gPrevPlaybackFrame >= 0) {
            gPlaylistAdvanceLatch = true;
        }
        gPrevPlaybackFrame = cur;
    } else {
        gPrevPlaybackFrame = playbackManager.getCurrentFrame();
    }
    plateManager.updateAnimations();
    trackManager.updateTrackWidgets();
    return plateManager.getChanged();
}

bool hasPendingTextureUploads() {
    // Same probe as needsPlaybackTick()'s second clause, exposed on its
    // own so the timer can gate the expensive makeCurrent/generateTextures/
    // doneCurrent trio: only enter the GL context when there's actually a
    // decoded frame waiting to upload.
    for (int i = 0; i < 4; ++i) {
        gfcSequence* seq = trackManager.getSequence(i);
        if (seq && seq->hasPendingRawFrames()) return true;
    }
    return false;
}

bool consumePlateChanged() {
    // Destructively reads plateManager's dirty flag (set by setChanged() from
    // ANY state edit — local or remote). The idle timer uses this so that a
    // bare setChanged() while playback is stopped and nothing is animating
    // still forces exactly one repaint, instead of relying on every edit call
    // site to also request a viewport refresh. When playback/animation is
    // running, tickPlaybackTiming() drains the same flag instead, so it's
    // consumed exactly once per tick either way.
    return plateManager.getChanged();
}

void uploadPendingTextures() {
    // GL half of tickPlayback(): drain one frame from each sequence's
    // rawFrames queue and upload it via glTexImage2D. Caller MUST make the
    // viewport's GL context current first (uploads run on this thread).
    trackManager.generateTextures();
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

void setPlayDirection(int direction) {
    // FLTK's '.' (forward, +1) / ',' (reverse, -1). Sets playback heading
    // without toggling play/pause.
    playbackManager.setDirection(direction);
}

void setInPointAndLoad() {
    // FLTK's Alt+I: set the in point at the current frame and (re)start all
    // tracks loading from one frame earlier, so footage is decoded around
    // the new in point. Async — the per-tick texture drain uploads frames.
    const int cur = playbackManager.getCurrentFrame();
    trackManager.startLoadingAllAt(cur - 1);
    playbackManager.setInPoint(cur);
}

void fastFwdPlayback() {
    playbackManager.ffwd();
}

void seekToFrame(int frame) {
    playbackManager.setCurrentFrame(frame);
    plateManager.setChanged();
}

void toggleHistogramActiveQuad() {
    plateManager.toggleHistogramMode(plateManager.getActiveQuad());
    plateManager.setChanged();
}

void toggleHistogramAll() {
    plateManager.toggleHistogramModeAll();
    plateManager.setChanged();
}

void toggleOnScreenHelp() {
    plateManager.toggleHelp();
    plateManager.setChanged();
}

// The Qt loop-mode combo uses 0/1/2 for Once/Loop/Bounce, but the
// playback manager's switch in update() matches against the CBArgs
// enum values (LOOPMODEONCE_ID=22, LOOPMODELOOP_ID=23, LOOPMODEBOUNCE_ID=24).
// Without this translation, changing the combo wrote a value (0/1/2) that
// no case matched — the switch fell through silently and playback froze.
// Constructor-time default of LOOPMODEONCE_ID worked, so first-loop playback
// succeeded; any subsequent mode change broke everything. Translate at the
// bridge boundary in both directions.
static int comboIdxToLoopModeId(int idx) {
    switch (idx) {
        case 1:  return LOOPMODELOOP_ID;
        case 2:  return LOOPMODEBOUNCE_ID;
        case 0:  // fall through to default
        default: return LOOPMODEONCE_ID;
    }
}

static int loopModeIdToComboIdx(int id) {
    switch (id) {
        case LOOPMODELOOP_ID:   return 1;
        case LOOPMODEBOUNCE_ID: return 2;
        case LOOPMODEONCE_ID:   // fall through
        default:                return 0;
    }
}

void setLoopMode(int mode) {
    playbackManager.setPlaybackMode(comboIdxToLoopModeId(mode));
}

int getLoopMode() {
    return loopModeIdToComboIdx(playbackManager.getPlaybackMode());
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
    // .tga image-based LUTs now load via OIIO (gfcReadImageRGB8), so they
    // count toward the expected total alongside the text-based LUTs.
    return countFilesByExt(sett.lutPath, {".lut", ".cub", ".cube", ".tga"});
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
        // .tga = image-based LUT (now loaded via OIIO; e.g. the UnitCube
        // identity cube). .lut/.cub/.cube are text-based LUTs.
        if (ext == ".lut" || ext == ".cub" || ext == ".cube" || ext == ".tga") {
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
    plateManager.broadcastFXStack(q);   // mirror structural change to peers
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
    plateManager.broadcastFXStack(plateIdx);   // mirror structural change to peers
}

void clearFXStackOnPlate(int plateIdx) {
    auto* stack = plateManager.getFXStack(plateIdx);
    if (!stack) return;
    stack->clearStack();
    plateManager.setChanged();
    plateManager.broadcastFXStack(plateIdx);   // mirror structural change to peers
}

void setFXActiveOnPlate(int plateIdx, int fxIndex, bool active) {
    auto* stack = plateManager.getFXStack(plateIdx);
    if (!stack) return;
    stack->setActive(fxIndex, active);
    plateManager.setChanged();
    plateManager.broadcastFXStack(plateIdx);   // mirror structural change to peers
}

void moveFXOnPlate(int plateIdx, int from, int to) {
    auto* stack = plateManager.getFXStack(plateIdx);
    if (!stack) return;
    stack->moveFX(from, to);
    plateManager.setChanged();
    plateManager.broadcastFXStack(plateIdx);   // mirror structural change to peers
}

std::vector<std::pair<int, std::string>> getAvailableFXMenu() {
    // getMenuNames() walks fxManager's fxArray in order, and getFX(i)
    // (which addFXToActivePlate calls) indexes the same fxArray — so the
    // returned indices are valid fxIndex args for addFXToActivePlate.
    std::vector<std::string> menuNames = fxManager.getMenuNames();
    std::vector<std::pair<int, std::string>> result;
    result.reserve(menuNames.size());
    for (int i = 0; i < (int)menuNames.size(); ++i) {
        std::string label = menuNames[i];
        if (label.empty())
            label = fxManager.getFX(i).name;  // fall back to plain name
        result.emplace_back(i, label);
    }
    return result;
}

std::vector<std::pair<int, std::string>> getCubeLutChoices() {
    // 3D LUTs for an FX cube param. `.first` is the GLOBAL lutManager index
    // (what gfcFX::bind() reads via getLUT(value).texture3D and what
    // setFXParamValueOnPlate stores); `.second` is the display name.
    std::vector<std::pair<int, std::string>> result;
    for (const std::string& name : lutManager.get3DLutNames())
        result.emplace_back(lutManager.getLutIndexByName(name), name);
    return result;
}

std::vector<std::pair<int, std::string>> getLut1DChoices() {
    // 1D LUTs for an FX lut param. Same global-index convention as above
    // (bind() reads getLUT(value).texture1D).
    std::vector<std::pair<int, std::string>> result;
    for (const std::string& name : lutManager.get1DLutNames())
        result.emplace_back(lutManager.getLutIndexByName(name), name);
    return result;
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
    // Routes through the plate manager so the edit is both applied and
    // broadcast to remote peers (live FX-attrib streaming); setChanged() and
    // the histogram-cache clear happen inside setFXWidgetValue.
    plateManager.setFXWidgetValue(plateIdx, fxIndex, groupName, widgetName, value);
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
    p.outWidth     = src.outWidth;
    p.outHeight    = src.outHeight;
    p.path         = src.path;
    // CreateRenderFilename concatenates path+prefix directly, so the output
    // directory needs a trailing separator (QFileDialog hands one back
    // without it).
    if (!p.path.empty() && p.path.back() != '/' && p.path.back() != '\\') {
        p.path += '/';
    }
    p.prefix       = src.prefix;
    p.postfix      = src.postfix;
    p.jpegQuality     = src.jpegQuality;
    p.jpegProgressive = src.jpegProgressive;
    p.jpegSubsampling = src.jpegSubsampling;
    p.pngQuality      = src.pngQuality;
    p.tiffCompression = src.tiffCompression;
    p.exrCompression  = src.exrCompression;
    p.exrFormat       = src.exrFormat;
    p.bitsPerChannel  = src.bitsPerChannel;
    p.bakeCropBars    = src.bakeCropBars;
    p.burnInNotes     = src.burnInNotes;
    return p;
}
}  // namespace

std::string previewRenderFilename(const RenderParams& params) {
    gfcRenderParams p = toCoreRenderParams(params);
    return CreateRenderFilename(p);
}

int triggerSyncRender(const RenderParams& params,
                      const std::function<void(int, int)>& onProgress) {
    gfcRenderParams base = toCoreRenderParams(params);
    const int total = (base.to >= base.from) ? (base.to - base.from + 1) : 0;
    int done = 0;
    if (onProgress) onProgress(0, total);
    // Render one frame per renderPlate call so we can report progress (and
    // let the dialog repaint) between frames.
    for (int frame = base.from; frame <= base.to; ++frame) {
        gfcRenderParams pf = base;
        pf.from = frame;
        pf.to   = frame;
        std::vector<std::string> rendered;
        plateManager.renderPlate(pf, &rendered);
        done += static_cast<int>(rendered.size());
        if (onProgress) onProgress(done, total);
    }
    return done;
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
    gCurrentContentFromPlaylist = false;
}

void loadPlaylistItem(int index) {
    auto* entries = playlistManager.getPlaylist();
    if (!entries || index < 0 || index >= (int)entries->size()) return;
    trackManager.setPlaylistItem(playlistManager.getItem(index));
    playlistManager.setSelectedItem(index);
    gCurrentContentFromPlaylist = true;
}

int getSelectedPlaylistItem() {
    return playlistManager.selectedItem;
}

bool currentContentIsPlaylistItem() { return gCurrentContentFromPlaylist; }

// JEF-18: push a .jpl path onto the recent-playlists list (dedup → newest at
// the back → cap). Mirrors the session recent-list logic (gfcsessionmanager),
// but lives here because playlists have no shared-core load/save hook — every
// playlist open/save funnels through loadPlaylistFile/savePlaylistFile below,
// so this is the single place that keeps sett.recentPlaylists current.
static void noteRecentPlaylist(const std::string& path) {
    if (path.empty()) return;
    auto& rp = sett.recentPlaylists;
    for (size_t i = 0; i < rp.size(); ++i) {          // drop an existing copy
        if (rp[i] == path) { rp.erase(rp.begin() + i); break; }
    }
    rp.push_back(path);                               // newest at the back
    while ((int)rp.size() > sett.maxRecentPlaylists && !rp.empty())
        rp.erase(rp.begin());                         // trim oldest from the front
}

void savePlaylistFile(const std::string& path) {
    if (path.empty()) return;
    // Match the on-disk name savePlaylist will write (it appends .jpl if
    // missing) so the recent entry points at the real file.
    std::string p = path;
    if (p.size() < 4 || p.substr(p.size() - 4) != ".jpl") p += ".jpl";
    playlistManager.savePlaylist(p);
    noteRecentPlaylist(p);
}

void loadPlaylistFile(const std::string& path) {
    if (path.empty()) return;
    clearPlaylist();   // bridge wrapper — also clears the from-playlist flag
    playlistManager.loadPlaylist(path);
    noteRecentPlaylist(path);
}

std::vector<std::string> getRecentPlaylists() { return sett.recentPlaylists; }

void setRecentPlaylists(const std::vector<std::string>& paths) {
    sett.recentPlaylists = paths;
    if ((int)sett.recentPlaylists.size() > sett.maxRecentPlaylists)
        sett.recentPlaylists.resize(sett.maxRecentPlaylists);
}

void addCurrentAsPlaylistItem() {
    playlistManager.addItemlist(trackManager.getPlaylistItem());
}

void addPlaylistFiles(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    playlistManager.addItemlist(playlistManager.createPlaylistItemFrom(paths));
}

void appendTracksToPlaylistItem(int index, const std::vector<std::string>& paths) {
    auto* entries = playlistManager.getPlaylist();
    if (!entries || index < 0 || index >= (int)entries->size()) return;
    if (paths.empty()) return;
    playlistManager.appendTracksToItem(paths, index);
}

std::vector<PlaylistTrackDetail> getPlaylistItemDetail(int index) {
    std::vector<PlaylistTrackDetail> out;
    auto* entries = playlistManager.getPlaylist();
    if (!entries || index < 0 || index >= (int)entries->size()) return out;
    const gfcPlaylistItem& item = (*entries)[index];
    for (size_t i = 0; i < item.loadParams.size(); ++i) {
        const gfcLoadParams& lp = item.loadParams[i];
        PlaylistTrackDetail d;
        d.letter = std::string(1, char('A' + (int)i));
        d.path = lp.fileName;
        d.fromFrame = lp.fromFrame;
        d.toFrame = lp.toFrame;
        d.totalFrames = (lp.toFrame >= lp.fromFrame)
                        ? (lp.toFrame - lp.fromFrame + 1) : 0;
        // scale is a float: a fraction (1.0 == 100%) or already a percent.
        // Upsampling isn't supported, so anything > 1.5 is treated as a
        // percent value; otherwise it's a 0..1 fraction.
        d.scalePct = (lp.scale <= 1.5f)
                     ? int(lp.scale * 100.0f + 0.5f)
                     : int(lp.scale + 0.5f);
        d.filter = (lp.filterType == 0) ? "linear" : "bilinear";
        d.crop = lp.crop;
        switch (lp.compressed) {
            case GFC_8BPC:    d.bitDepth = "8";    break;
            case GFC_16BPC:   d.bitDepth = "16";   break;
            case GFC_16HALF:  d.bitDepth = "16f";  break;
            case GFC_4BPC:    d.bitDepth = "32f";  break;
            case GFC_S3TCDX1: d.bitDepth = "dxt1"; break;
            default:          d.bitDepth = "?";    break;
        }
        out.push_back(d);
    }
    return out;
}

void setPlaylistScaleOverride(int pct) {
    trackManager.setScaleOverride(pct);  // 0 = no override
}

bool consumePlaylistAdvanceSignal() {
    const bool v = gPlaylistAdvanceLatch;
    gPlaylistAdvanceLatch = false;
    return v;
}

bool isPlaylistItemPlayingOnce() {
    return playbackManager.getPlaybackMode() == LOOPMODEONCE_ID;
}

void loadPlaylistItemAndPlay(int index) {
    auto* entries = playlistManager.getPlaylist();
    if (!entries || index < 0 || index >= (int)entries->size()) return;
    // Clear any stale advance latch so starting fresh playback here (e.g. from
    // a future direct-jump path) can't trigger an immediate spurious advance.
    gPlaylistAdvanceLatch = false;
    trackManager.setPlaylistItem(playlistManager.getItem(index));
    playlistManager.setSelectedItem(index);
    gCurrentContentFromPlaylist = true;
    playbackManager.setCurrentFrame(playbackManager.getFromFrame());
    plateManager.setChanged();
    gPrevPlaybackFrame = playbackManager.getCurrentFrame();
    playbackManager.startPlayFwd();
}

void pausePlaybackIfPlaying() {
    if (playbackManager.isPlaying()) playbackManager.pause();
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
    if (networkManager.getIsServer()) networkManager.stopServer();
    else                              networkManager.stopConnection();
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

std::vector<LutSummary> getLutSummaries() {
    std::vector<LutSummary> out;
    const auto names = lutManager.getAllNames();
    out.reserve(names.size());
    for (int i = 0; i < (int)names.size(); ++i) {
        CubeLUT lut = lutManager.getLUT(i);   // copies; only on manual refresh
        LutSummary s;
        s.name     = lut.getNameNoPath();
        s.is3D     = (lut.type != CubeLUT::JEFECHECK1D);
        s.size     = lut.size;
        s.fromBits = lut.fromBits;
        s.toBits   = lut.toBits;
        out.push_back(std::move(s));
    }
    return out;
}

LutPreviewData getLutPreview(int guiLutIndex) {
    LutPreviewData d;
    if (guiLutIndex <= 0) return d;          // 0 = "(No LUT)"
    CubeLUT lut = lutManager.getLUT(guiLutIndex - 1);
    if (lut.size <= 0) return d;
    d.valid    = true;
    d.type     = lut.type;
    d.is3D     = (lut.type != CubeLUT::JEFECHECK1D);
    d.size     = lut.size;
    d.fromBits = lut.fromBits;
    d.toBits   = lut.toBits;
    d.max1D    = lut.maximum1DValue > 0.f ? lut.maximum1DValue : 1.f;
    d.name     = lut.getNameNoPath();

    if (!d.is3D) {
        const int n = std::min(lut.size, 1024);
        d.curve1D.reserve(n);
        for (int i = 0; i < n; ++i) d.curve1D.push_back(lut.lut1D[i]);
        return d;
    }

    // 3D: build a structured working grid (with adjacency, so the widget
    // can draw faces / lattice / dots). Subsample large cubes to a bounded
    // working edge so faces and lattice stay manageable.
    constexpr int kMaxCubeEdge = 33;
    const int s = lut.size;
    int stride = 1;
    while ((s + stride - 1) / stride > kMaxCubeEdge) ++stride;
    const int cs = (s + stride - 1) / stride;   // working edge (ceil)
    d.cubeSize = cs;
    d.cubeRGB.resize((size_t)cs * cs * cs * 3);
    auto clamp01 = [](double v) { return (float)(v < 0 ? 0 : (v > 1 ? 1 : v)); };
    for (int ix = 0; ix < cs; ++ix) {
        const int x = std::min(ix * stride, s - 1);
        for (int iy = 0; iy < cs; ++iy) {
            const int y = std::min(iy * stride, s - 1);
            for (int iz = 0; iz < cs; ++iz) {
                const int z = std::min(iz * stride, s - 1);
                const Vec3D& c = lut.cube[x][y][z];
                const size_t idx = (((size_t)ix * cs + iy) * cs + iz) * 3;
                d.cubeRGB[idx + 0] = clamp01(c.x);
                d.cubeRGB[idx + 1] = clamp01(c.y);
                d.cubeRGB[idx + 2] = clamp01(c.z);
            }
        }
    }
    return d;
}

void panPlate(int plateIdx, float dx, float dy) {
    if (plateIdx < 0) return;
    plateManager.panPlate(plateIdx, dx, dy);
    plateManager.setChanged();
}

void panAllPlates(float dx, float dy) {
    plateManager.panAllPlates(dx, dy);
    plateManager.setChanged();
}

void zoomPlate(int plateIdx, float zoomDelta) {
    if (plateIdx < 0) return;
    plateManager.zoomPlate(plateIdx, zoomDelta);
    plateManager.setChanged();
}

void zoomAllPlates(float zoomDelta) {
    plateManager.zoomAllPlates(zoomDelta);
    plateManager.setChanged();
}

// Color-correction adjustment helpers. `1` as the isDelta flag asks
// plateManager to sum the value onto the current field rather than
// overwrite — same convention the FLTK GlViewport key+drag path uses.
void adjustPlateGamma(int plateIdx, float delta) {
    if (plateIdx < 0) return;
    plateManager.setGamma(plateIdx, delta, 1);
    plateManager.setChanged();
}

void adjustPlateExposure(int plateIdx, float delta) {
    if (plateIdx < 0) return;
    plateManager.setExposure(plateIdx, delta, 1);
    plateManager.setChanged();
}

void setPlateAspect(int plateIdx, float aspect) {
    if (plateIdx < 0) return;
    plateManager.setAspect(plateIdx, aspect);
}

void setPlateCrop(int plateIdx, bool on) {
    if (plateIdx < 0) return;
    plateManager.setCrop(plateIdx, on ? 1 : 0);
}

void adjustPlateBrightness(int plateIdx, float delta) {
    if (plateIdx < 0) return;
    plateManager.setBrightness(plateIdx, delta, 1);
    plateManager.setChanged();
}

void adjustPlateContrast(int plateIdx, float delta) {
    if (plateIdx < 0) return;
    plateManager.setContrast(plateIdx, delta, 1);
    plateManager.setChanged();
}

void adjustPlateSaturation(int plateIdx, float delta) {
    if (plateIdx < 0) return;
    plateManager.setSaturation(plateIdx, delta, 1);
    plateManager.setChanged();
}

void adjustAllPlatesGamma(float delta) {
    plateManager.setGammaAll(delta, 1);
    plateManager.setChanged();
}

void adjustAllPlatesExposure(float delta) {
    plateManager.setExposureAll(delta, 1);
    plateManager.setChanged();
}

void adjustAllPlatesBrightness(float delta) {
    plateManager.setBrightnessAll(delta, 1);
    plateManager.setChanged();
}

void adjustAllPlatesContrast(float delta) {
    plateManager.setContrastAll(delta, 1);
    plateManager.setChanged();
}

void adjustAllPlatesSaturation(float delta) {
    plateManager.setSaturationAll(delta, 1);
    plateManager.setChanged();
}

void propagatePlateChanges() {
    plateManager.updatePlatesFromGUI();
    plateManager.setChanged();
}

void setAllPlatesShowPreview(bool showPreview) {
    for (int i = 0; i < 4; ++i) {
        plateManager.setPlateShowPreview(i, showPreview);
    }
    // setPlateShowPreview only writes to the Qt GUI side. gfcPlate::showPreview
    // (and the other plate fields — gamma, exposure, BCS, LUT) are mirrored
    // from the GUI by updateValueFromGUI. Without this propagation, the
    // dialog's flag flip never reaches the plate, the previewFrame never
    // renders, and after Load All the plate keeps stale color state (the
    // "gray until Shift-R" symptom).
    //
    // We use updatePlatesFromGUI rather than updateAllFromGUI here because
    // the latter also resets the layout (framingMode) and active quad from
    // the plate-manager-GUI's stale fields — the Qt build drives those via
    // separate paths (Cmd+1/2/3/4 shortcuts; plate-card clicks) that don't
    // round-trip through the plate-manager GUI.
    plateManager.updatePlatesFromGUI();
    plateManager.setChanged();
}

int plateAtViewportPos(int x, int y, int viewportW, int viewportH) {
    return plateManager.getPlateAtPosition(x, y, viewportW, viewportH);
}

void setFramingMode(int framingMode) {
    plateManager.setFramingMode(framingMode);
    plateManager.setChanged();
}

void setScreenFBO(unsigned fbo) {
    gScreenFBO = (GLuint)fbo;
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

namespace {
// Resolve a plate index to its track's gfcSequence — every layer-combo
// op is really "operate on whichever track the plate is mapped to".
gfcSequence* sequenceForPlate(int plateIdx) {
    if (plateIdx < 0) return nullptr;
    const int trackIdx = plateManager.getTrackOnPlate(plateIdx);
    if (trackIdx < 0) return nullptr;
    return trackManager.getSequence(trackIdx);
}
}  // namespace

std::vector<std::string> getLayersOnPlate(int plateIdx) {
    auto* seq = sequenceForPlate(plateIdx);
    if (!seq || !seq->myGUI) return {};
    auto* qtGUI = dynamic_cast<gfcSequenceGUI_Qt*>(seq->myGUI);
    if (!qtGUI) return {};
    return qtGUI->getChannelOptions();
}

std::string getActiveLayerOnPlate(int plateIdx) {
    auto* seq = sequenceForPlate(plateIdx);
    if (!seq || !seq->myGUI) return {};
    return seq->myGUI->getChannelName();
}

std::string getPlateNativeAspect(int plateIdx) {
    auto* seq = sequenceForPlate(plateIdx);
    if (!seq) return {};
    // getPreviewFrame() returns a shallow copy (gfcFrame has no destructor),
    // so reading its dimensions is cheap and safe. An unloaded frame defaults
    // to the 15x15 sentinel with loaded==false — gate on both.
    gfcFrame f = seq->getPreviewFrame();
    if (!f.loaded || f.sizeX <= 0 || f.sizeY <= 0) return {};
    const double ratio =
        static_cast<double>(f.sizeX) / static_cast<double>(f.sizeY);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f:1", ratio);
    return std::string(buf);
}

bool runAspectSelfTest() {
    // Exercise the exact path the plate card uses: the aspect control emits a
    // preset string, PlateCard forwards it to gfcPlateGUI_Qt::setAspectChoice,
    // which converts via aspectFromString and stores the renderer float read
    // back by getAspect(). gfcPlate::calculatePolySizesCropEtc treats that
    // float as content HEIGHT/WIDTH, so a landscape "W:H" preset (W > H) MUST
    // map to a value in (0,1) — the JEF-21 bug returned W/H (> 1), producing
    // off-frame (invisible) crop bars and an inverted anamorphic reshape.
    struct Case { const char* str; double expected; };
    const Case cases[] = {
        {"16:9",   9.0 / 16.0},
        {"4:3",    3.0 / 4.0},
        {"2.39:1", 1.0 / 2.39},
        {"2.35:1", 1.0 / 2.35},
        {"1.85:1", 1.0 / 1.85},
        {"1.37:1", 1.0 / 1.37},
    };
    gfcPlateGUI_Qt gui;
    bool pass = true;
    for (const auto& c : cases) {
        gui.setAspectChoice(c.str);
        const double got = gui.getAspect();
        // Direction check (the actual bug): landscape presets are wider than
        // tall, so height/width must be strictly < 1.
        const bool inRange = got > 0.0 && got < 1.0;
        // Value check: must equal the reciprocal of the displayed W:H ratio.
        const bool exact = std::fabs(got - c.expected) < 1e-4;
        if (!inRange || !exact) pass = false;
        std::printf("  aspect \"%s\": got=%.5f expected=%.5f range(0,1)=%d exact=%d\n",
                    c.str, got, c.expected, inRange ? 1 : 0, exact ? 1 : 0);
    }
    // "original" and unparseable text must yield gfcPlate's native sentinel.
    gui.setAspectChoice("original");
    const bool origOk = gui.getAspect() == -1.0f;
    if (!origOk) pass = false;
    std::printf("  aspect \"original\": got=%.5f sentinel(-1)=%d\n",
                gui.getAspect(), origOk ? 1 : 0);
    std::printf("ASPECT-TEST %s\n", pass ? "PASS" : "FAIL");
    std::fflush(stdout);
    return pass;
}

void setLayerOnPlate(int plateIdx, const std::string& layerName) {
    if (plateIdx < 0) return;
    const int trackIdx = plateManager.getTrackOnPlate(plateIdx);
    if (trackIdx < 0) return;
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq || !seq->myGUI) return;

    // Order matters: setChannel(name) must land before loadPreview()
    // because loadPreview only resets channel index to 0 when params.
    // channel == -1; the string the OIIO loader uses to pick a layer is
    // params.channelName, which getLoadParamsFromGUI pulls straight off
    // myGUI->getChannelName(). Without the rewrite first, the preview
    // would re-decode the previously-selected layer.
    seq->myGUI->setChannel(layerName);

    // Re-decode the preview frame against the new layer so the texture
    // currently on screen reflects the choice immediately. The OIIO
    // loader's layer pick is driven by params.channelName which we just
    // set above.
    seq->loadPreview();

    // The async multi-frame loader caches the channel choice into each
    // frame's load params at startLoadingSequence time, so a layer
    // switch needs a full re-decode. Matches the FLTK shift-click-on-
    // timeline behavior the user asked for. Cheap when the sequence is
    // a single frame (the per-tick generateTextures drain handles the
    // texture refresh from loadPreview alone).
    if (seq->getNumPreviewFrames() > 1) {
        trackManager.startLoadingSequence(trackIdx);
    }

    // Mirror the layer choice to remote peers (they re-decode via the async
    // loader; no-op when not in a session).
    networkManager.sendLayerChange(plateIdx, layerName);

    plateManager.setChanged();
}

bool loadFileIntoPlate(const std::string& path,
                       int whichSequence,
                       bool kickOffSequenceLoad,
                       float scale) {
    auto* seq = trackManager.getSequence(whichSequence);
    if (!seq || !seq->myGUI || path.empty()) {
        return false;
    }
    // Quick-load / drag-drop clears the playlist arming so auto-advance
    // can't fire against content the user loaded independently.
    gCurrentContentFromPlaylist = false;
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

TrackEstimates getTrackEstimates(int trackIdx) {
    TrackEstimates est{0, 0, 0.0f};
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq || !seq->myGUI) return est;

    const int from = seq->myGUI->getFrom();
    const int to   = seq->myGUI->getTo();
    if (to < from) return est;
    est.frames = to - from + 1;

    const gfcFrame pf = seq->getPreviewFrame();
    int bpp = 4;
    switch (seq->myGUI->getCompression()) {
        case GFC_8BPC:     bpp = 4; break;
        case GFC_4BPC:     bpp = 2; break;
        case GFC_16BPC:
        case GFC_16HALF:   bpp = 8; break;
        case GFC_S3TCDX1:  bpp = 1; break;
        default:           bpp = 4; break;
    }
    const size_t w = (size_t)pf.quadSizeX;
    const size_t h = (size_t)pf.quadSizeY;
    est.bytes = w * h * (size_t)bpp * (size_t)est.frames;

    const double secsPerFrame = seq->getPreviewElapsedSecs() > 0.0
                                  ? seq->getPreviewElapsedSecs()
                                  : 0.025;
    est.seconds = (float)(secsPerFrame * est.frames);
    return est;
}

bool reloadTrackPreview(int trackIdx) {
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq || !seq->myGUI) return false;
    if (seq->myGUI->getFilename().empty()) {
        seq->clearPreviewFrame();
        return false;
    }
    const std::string loaded = seq->loadPreview();
    return !loaded.empty();
}

void unloadAndClearTrack(int trackIdx) {
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq || !seq->myGUI) return;
    trackManager.stopLoadingSequence(trackIdx);
    seq->unloadAndClear();
    plateManager.setChanged();
}

int startLoadingAllTracks() {
    // Loading tracks directly (Load Window "Load All", Open Session) is not a
    // playlist load — disarm auto-advance so it can't hijack this content.
    gCurrentContentFromPlaylist = false;
    int started = 0;
    for (int i = 0; i < 4; ++i) {
        auto* seq = trackManager.getSequence(i);
        if (!seq || !seq->myGUI) continue;
        if (seq->myGUI->getFilename().empty()) continue;
        trackManager.stopLoadingSequence(i);
        seq->stopLoading();
        trackManager.startLoadingSequence(i);
        ++started;
    }
    return started;
}

ThumbPixels getTrackThumbnail(int track, int frameIndex) {
    ThumbPixels out;
    if (!sett.showThumbnails) return out;
    auto* seq = trackManager.getSequence(track);
    if (!seq) return out;
    const GfcThumbnail& t = seq->getThumbnail(frameIndex);
    if (t.w <= 0 || t.h <= 0 || t.rgba.empty()) return out;
    out.present = true;
    out.w = t.w;
    out.h = t.h;
    out.rgba = t.rgba;   // copy out; widget must not hold seq memory
    return out;
}

bool getThumbnailsEnabled() {
    return sett.showThumbnails;
}

void setThumbnailsEnabled(bool on) {
    sett.showThumbnails = on;
    plateManager.setChanged();
}

TrackTimelineState getTrackTimelineState(int trackIdx) {
    TrackTimelineState s;
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq) return s;
    s.present = !seq->isEmpty();
    s.offset  = seq->getOffset();
    // gfcSequence ranges are 0-based (an 8-frame clip is 0..7), but the
    // timeline and the playback limits are 1-based — getEndLimit/
    // getStartLimit add +1 to the sequence range, and `to` is the frame
    // count. Convert here so the bar lines up with the 1-based scrubber/
    // playhead (without +1 the last frame's slice never fills).
    s.rangeStart = seq->getRangeStart() + 1;   // offset already folded in
    s.rangeEnd   = seq->getRangeEnd() + 1;
    s.numFrames  = seq->getNumFrames();
    s.loadedCount = seq->getLoadedFrameCount();
    // v1: anchor the loaded fill at the sequence's range start. Loading
    // is sequential, so [rangeStart, rangeStart + loadedCount) is the
    // decoded run. (Precise fill positioning for alt-click load-from-X
    // is a deferred refinement noted in the spec.)
    s.firstLoadedFrame = s.rangeStart;
    if (seq->myGUI && !seq->myGUI->getFilename().empty()) {
        namespace fs = std::filesystem;
        s.label = fs::path(seq->filenameGeneric.empty()
                               ? seq->myGUI->getFilename()
                               : seq->filenameGeneric).filename().string();
    }
    return s;
}

int getTrackOffset(int trackIdx) {
    auto* seq = trackManager.getSequence(trackIdx);
    return seq ? seq->getOffset() : 0;
}

void setTrackOffset(int trackIdx, int offset) {
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq) return;
    seq->setOffset(offset);
    plateManager.setChanged();
}

void startLoadingTrackAt(int trackIdx, int frame) {
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq || seq->isEmpty()) return;
    trackManager.startLoadingSequenceAt(trackIdx, frame);
}

bool getTrackHoldMode(int trackIdx) {
    auto* seq = trackManager.getSequence(trackIdx);
    return seq ? (seq->getHoldMode() != 0) : false;
}

void setTrackHoldMode(int trackIdx, bool hold) {
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq) return;
    seq->setHoldMode(hold ? 1 : 0);
    plateManager.setChanged();
}

TrackParams getTrackParams(int trackIdx) {
    TrackParams p;
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq || !seq->myGUI) return p;
    p.filename        = seq->myGUI->getFilename();
    p.from            = seq->myGUI->getFrom();
    p.to              = seq->myGUI->getTo();
    p.scalePct        = (int)(seq->myGUI->getScale() + 0.5f);
    p.compression     = seq->myGUI->getCompression();
    p.channel         = seq->myGUI->getChannel();
    p.crop            = seq->myGUI->getCrop() != 0;
    p.filenameGeneric = seq->filenameGeneric;
    if (auto* gui = dynamic_cast<gfcSequenceGUI_Qt*>(seq->myGUI)) {
        p.channelOptions = gui->getChannelOptions();
    }
    return p;
}

void setTrackFilename(int trackIdx, const std::string& path) {
    auto* seq = trackManager.getSequence(trackIdx);
    if (seq && seq->myGUI) seq->myGUI->setFilename(path);
}

void setTrackFrom(int trackIdx, int v) {
    auto* seq = trackManager.getSequence(trackIdx);
    if (seq && seq->myGUI) seq->myGUI->setFromFrame(v);
}

void setTrackTo(int trackIdx, int v) {
    auto* seq = trackManager.getSequence(trackIdx);
    if (seq && seq->myGUI) seq->myGUI->setToFrame(v);
}

void setTrackScalePct(int trackIdx, int pct) {
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq || !seq->myGUI) return;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d", pct);
    seq->myGUI->setScale(buf);
}

void setTrackCompression(int trackIdx, int compEnum) {
    auto* seq = trackManager.getSequence(trackIdx);
    if (seq && seq->myGUI) seq->myGUI->setCompression(compEnum);
}

void setTrackChannel(int trackIdx, int channelIdx) {
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq || !seq->myGUI) return;
    seq->myGUI->setChannel(channelIdx);
    // The OIIO loader picks the EXR layer from params.channelName
    // (resolved via myGUI->getChannelName() in getLoadParamsFromGUI),
    // not the int index. Mirror the per-plate setLayerOnPlate path:
    // resolve idx → name through the Qt GUI's channel-options cache
    // and write that too. Without this, switching channels through
    // the Load Window int-only setter leaves channelName stale and
    // the loader re-decodes the previously-selected layer.
    if (auto* gui = dynamic_cast<gfcSequenceGUI_Qt*>(seq->myGUI)) {
        const auto& opts = gui->getChannelOptions();
        if (channelIdx >= 0 && channelIdx < (int)opts.size()) {
            seq->myGUI->setChannel(opts[channelIdx]);
        }
    }
}

void setTrackCrop(int trackIdx, bool on) {
    auto* seq = trackManager.getSequence(trackIdx);
    if (seq && seq->myGUI) seq->myGUI->setCrop(on ? 1 : 0);
}

// --- Session save/restore ---------------------------------------------------

static std::string recoveryPath() {
    return ::getApplicationDataPath() + "recoverySession.jcs";
}

bool saveSession(const std::string& path) {
    if (path.empty()) return false;
    sessionManager.saveSession(path);
    return true;
}

bool loadSession(const std::string& path) {
    if (path.empty()) return false;
    sessionManager.loadSession(path);   // caller made GL context current
    plateManager.setChanged();
    return true;
}

bool getHasRecoverableSession() { return sessionManager.checkCrashedSession(); }

bool loadRecoverySession() {
    if (!sessionManager.checkCrashedSession()) return false;
    // Load but leave the file in place (loadCrashedSession deletes; we want it
    // to also serve "reopen last session").
    sessionManager.loadSession(recoveryPath());
    plateManager.setChanged();
    return true;
}

void writeRecoverySession() {
    // Unconditional (not gated on enableCrashRecoverySession) so "reopen last"
    // always has content; the launch preference governs whether it's consumed.
    sessionManager.saveSession(recoveryPath());
}

void removeRecoverySession() { sessionManager.removeCrashSession(); }

std::vector<std::string> getRecentSessions() { return sett.recentSessions; }

void setRecentSessions(const std::vector<std::string>& paths) {
    sett.recentSessions = paths;
    if ((int)sett.recentSessions.size() > sett.maxRecentSessions)
        sett.recentSessions.resize(sett.maxRecentSessions);
}

int  getStartupSessionBehavior() { return sett.startupSessionBehavior; }
void setStartupSessionBehavior(int mode) { sett.startupSessionBehavior = mode; }

bool getStartFullscreen() { return sett.startFullscreen != 0; }
bool getOpenLoadWindowAtStartup() { return sett.openLoadWindowAtStartup != 0; }
std::string getDefaultBrowsePath() { return sett.defaultBrowsePath; }

void saveCCFavoriteFromActive(int slot) {
    plateManager.saveFavoriteColorCorrectionFromPlate(slot);  // active quad
}

void applyCCFavoriteToActive(int slot) {
    plateManager.setFavoriteColorCorrectionOnPlate(slot);     // active quad
    plateManager.setChanged();
}

bool saveCCFavoritesFile(const std::string& path) {
    XMLNode root = XMLNode::createXMLTopNode("ccFavorites");
    plateManager.saveFavoriteColorCorrectionsToNode(root);
    return root.writeToFile(path.c_str()) == eXMLErrorNone;
}

bool loadCCFavoritesFile(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(path, ec)) return false;
    XMLNode root = XMLNode::openFileHelper(path.c_str(), "ccFavorites");
    if (root.isEmpty()) return false;
    plateManager.loadFavoriteColorCorrectionsFromNode(root);
    return true;
}

std::string getFavoritesFilePath() {
    return ::getApplicationDataPath() + "favorites.jcs";
}

std::vector<std::string> remoteParticipants() { return networkManager.participantNames(); }
std::string              remoteStatusText()   { return networkManager.connectionStatusText(); }
std::vector<std::string> remoteChatLog()      { return networkManager.chatLogLines(); }
std::vector<std::string> remoteErrors()       { return networkManager.drainErrors(); }
std::vector<std::string> remoteNetworkLog()   { return networkManager.networkLogLines(); }

std::vector<ChatEntry> remoteChatEntries() {
    std::vector<ChatEntry> out;
    for (auto& d : networkManager.chatEntries()) {
        ChatEntry e;
        e.sender   = d.sender;
        e.message  = d.message;
        e.timeHHMM = d.timeHHMM;
        e.type     = d.type;
        e.isSelf   = d.isSelf;
        e.color    = d.color;
        out.push_back(e);
    }
    return out;
}

// JEF-39: forward-declared here so pumpNetwork() (just below) can drain
// inbound note sync events; defined with the rest of the Notes dock support
// near the end of this file, alongside the gfcReview storage it touches.
namespace { bool applyInboundNoteSyncEvents(); void syncPlateNotesImpl(); }

bool pumpNetwork() {
    static bool        prevConnected = false;
    static size_t      prevPeers     = 0;
    static size_t      prevChat      = 0;
    static std::string prevStatus;
    networkManager.update();
    const bool        nowConnected = networkManager.getConnected();
    const size_t      nowPeers     = networkManager.participantNames().size();
    const size_t      nowChat      = networkManager.chatLogLines().size();
    const std::string nowStatus    = networkManager.connectionStatusText();
    // Repaint whenever any inbound packet was processed this tick: client.Update()
    // applies mirrored plate/playback/FX state to the managers, but QOpenGLWidget
    // only repaints on local input — without this the receiver wouldn't redraw
    // remote changes until the user interacted locally.
    const bool gotInbound = networkManager.consumeGotMessages();
    // JEF-39: apply inbound note add/remove/lock events into the in-memory
    // gfcReview store (see the Notes dock section below) so a remote peer's
    // markup shows up here without a local interaction. Folded into the same
    // "changed" signal as chat/participants so MainWindow's tick refreshes
    // the Notes dock exactly the way it already refreshes the Remote dialog.
    const bool notesApplied = applyInboundNoteSyncEvents();
    // A note that arrived from a peer changed the store; push it through to
    // the plates or it stays invisible until something else happens to sync.
    if (notesApplied) syncPlateNotesImpl();
    const bool changed = (nowConnected != prevConnected) ||
                         (nowPeers != prevPeers) || (nowChat != prevChat) ||
                         (nowStatus != prevStatus) || gotInbound || notesApplied;
    prevConnected = nowConnected; prevPeers = nowPeers; prevChat = nowChat;
    prevStatus = nowStatus;
    return changed;
}

// Child/client role: connect, pump until connected (or timeout), optionally
// start playback (mirrors a play message — used by Task 5), then hold.
// Both halves of --remote-test print timestamped milestones. The test used to
// report only a final participants/mirrored_play line, which cannot tell apart
// "the peer started too slowly", "the peer never connected and played into
// nothing" and "play arrived but was not mirrored" -- three different faults
// with the same summary.
namespace {
long long remoteTestMsSince(const std::chrono::steady_clock::time_point& t0) {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0).count();
}
}  // namespace

void remoteTestPeerConnect(const std::string& ip, int port, int holdMs, bool play) {
    const auto t0 = std::chrono::steady_clock::now();
    RemoteClientParams cp; cp.clientName = "peer"; cp.serverIP = ip; cp.port = port; cp.password = "";
    connectAsClient(cp);
    printf("REMOTE-PEER: +%lldms connectAsClient returned\n", remoteTestMsSince(t0));
    fflush(stdout);
    for (int t = 0; t < 3000 && !isRemoteConnected(); t += 10) {
        pumpNetwork();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const bool connected = isRemoteConnected();
    printf("REMOTE-PEER: +%lldms connected=%d\n", remoteTestMsSince(t0), connected ? 1 : 0);
    fflush(stdout);
    if (play) {
        togglePlayFwd();   // sends a play/pause message to the server
        printf("REMOTE-PEER: +%lldms play toggled (reaches the server only if connected)\n",
               remoteTestMsSince(t0));
        fflush(stdout);
    }
    for (int t = 0; t < holdMs; t += 10) {
        pumpNetwork();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    printf("REMOTE-PEER: +%lldms hold finished\n", remoteTestMsSince(t0));
    fflush(stdout);
}

// Orchestrator/server role: host, pump while the child connects and toggles
// play, and report whether the mirrored play state arrived on this (server) side.
bool remoteTestServerSawPlay(int port, int settleMs) {
    const auto t0 = std::chrono::steady_clock::now();
    RemoteServerParams sp; sp.serverName = "jefe-remote-test"; sp.port = port; sp.password = "";
    connectAsServer(sp);
    printf("REMOTE-SERVER: +%lldms hosting on %d\n", remoteTestMsSince(t0), port);
    fflush(stdout);
    bool sawPlay = false;
    size_t lastParticipants = (size_t)-1;
    for (int t = 0; t < settleMs; t += 10) {
        pumpNetwork();
        const size_t n = remoteParticipants().size();
        if (n != lastParticipants) {
            printf("REMOTE-SERVER: +%lldms participants=%zu\n", remoteTestMsSince(t0), n);
            fflush(stdout);
            lastParticipants = n;
        }
        if (isPlaying()) { sawPlay = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    printf("REMOTE-SERVER: +%lldms window closed, sawPlay=%d\n",
           remoteTestMsSince(t0), sawPlay ? 1 : 0);
    fflush(stdout);
    return sawPlay;
}

// --- Chat overlay + keyboard chat entry (Task 7) ----------------------------

void drawNetworkOverlay(int w, int h) { networkManager.draw(w, h); }

// --- Remote pointer broadcast (Task 8) --------------------------------------

void sendRemotePointer(int xPx, int yPx, int quadID) {
    if (!networkManager.getConnected()) return;
    if (quadID < 0) return;   // not over a plate
    // Drop unchanged positions and throttle to ~60Hz so drag motion can't
    // flood the reliable-ordered channel shared with playback/CC/FX/chat.
    static int lastX = INT_MIN, lastY = INT_MIN;
    if (xPx == lastX && yPx == lastY) return;
    static std::chrono::steady_clock::time_point lastSend{};
    const auto now = std::chrono::steady_clock::now();
    if (now - lastSend < std::chrono::milliseconds(16)) return;
    lastSend = now;
    lastX = xPx; lastY = yPx;
    // Send the cursor in the plate's IMAGE space (not raw framebuffer pixels):
    // the receiver draws the pointer inside the matching plate, transformed by
    // that plate's pan/zoom/rotation, so it must arrive as (quadID, image-x,
    // image-y, scale). getCursorPositionIn2DSpace packs the scale into z. (This
    // matches the original FLTK send and also supplies the correct quad/scale
    // that Task 8 had hardcoded to 0/1.)
    Vec3D pos = plateManager.getCursorPositionIn2DSpace(xPx, yPx, quadID);
    gfcNetPointerInfo info;
    info.quadID = quadID;
    info.x = (int)pos.x;
    info.y = (int)pos.y;
    info.scale = pos.z;
    info.color = 0;
    networkManager.sendPointerInfoMessage(info);
}

void sendChatMessageText(const std::string& text) {
    // Panel chat-send path: set the text the manager reads and send it.
    networkManager.gChatTextString = text;
    networkManager.sendChatMessage();
    networkManager.gChatTextString.clear();
}

bool remoteChatModeActive() { return networkManager.gChatMode == 1; }
void remoteChatBegin()      { networkManager.gChatMode = 1; }
void remoteChatCancel()     { networkManager.gChatMode = 0; networkManager.gChatTextString.clear(); }
void remoteChatBackspace()  {
    auto& s = networkManager.gChatTextString;
    if (!s.empty()) s.pop_back();
}
void remoteChatAppend(const std::string& s) {
    if (networkManager.gChatTextString.size() + s.size() < 254)
        networkManager.gChatTextString += s;
}
void remoteChatSubmit() {
    networkManager.sendChatMessage();          // reads gChatTextString
    networkManager.gChatTextString.clear();
    networkManager.gChatMode = 0;
}

// -----------------------------------------------------------------------
// Notes dock (JEF-39 Task 6)
//
// One gfcReview per piece of footage (mediaPath is the identity, matching
// the spec), not per plate -- two plates showing the same sequence share a
// review, and gfcNote::quadID picks out which plate's markup a note is.
// g_noteReviews holds every review touched this session; reviewForPlate()
// resolves "whichever review belongs to the media currently on this plate"
// and lazily loads its sidecar (gfcNoteStore::load) the first time that
// media is asked about, mirroring gfcSessionManager's own lazy-XML shape.
// -----------------------------------------------------------------------
namespace {

// gfcReview declares its own destructor (~gfcReview(), gfcreview.h), which
// suppresses the implicit move constructor, and it holds a
// vector<gfcRevision> whose notes are unique_ptr -- so gfcReview is neither
// copyable nor movable. std::vector<gfcReview> can't grow (push_back would
// need to relocate existing elements). Hence unique_ptr<gfcReview> here.
std::vector<std::unique_ptr<gfcReview>> g_noteReviews;

// Finds the in-memory review for `normalisedPath`, loading its sidecar on
// first touch. `load()` leaves its argument untouched on failure, and the
// argument already has mediaPath set, so a review with no sidecar yet still
// comes back with the right identity and zero revisions.
gfcReview& reviewForPath(const std::string& normalisedPath) {
    for (auto& r : g_noteReviews) {
        if (r->mediaPath == normalisedPath) return *r;
    }
    auto r = std::make_unique<gfcReview>();
    r->mediaPath = normalisedPath;
    gfcNoteStore::load(normalisedPath, *r);
    g_noteReviews.push_back(std::move(r));
    return *g_noteReviews.back();
}

// nullptr when nothing is loaded on this plate -- mirrors the
// getPreviewFrame().loaded gate getLoadedSequenceName() already uses.
gfcReview* reviewForPlate(int plateIdx) {
    gfcSequence* seq = sequenceForPlate(plateIdx);
    if (!seq) return nullptr;
    // Gate on the MEDIA PATH, not on a loaded preview frame.
    //
    // This used to also require seq->getPreviewFrame().loaded, mirroring
    // getLoadedSequenceName(). That is wrong here, and intermittently so: the
    // preview frame is a Load-Sequence-Manager concept, populated when that
    // modal stages a track. A plate that loaded footage through Quick Load,
    // drag-drop or --open-file plays its numbered sequence frames and may have
    // no preview at all — so the Notes dock reported "no media loaded" over an
    // image that was visibly on screen, depending on which path had loaded it.
    //
    // A review is keyed by media path. If a plate has a path, it can have
    // notes; whether a preview frame happens to be decoded is unrelated.
    if (seq->filenameGeneric.empty()) return nullptr;
    return &reviewForPath(gfcNoteStore::normalisePath(seq->filenameGeneric));
}

// ---------------------------------------------------------------------------
// Store -> renderer. gfcPlate::setNotes takes BORROWED pointers, so what is
// pushed must outlive the next draw: these point into g_noteReviews, which
// lives for the session and is only mutated here, never mid-frame.
// ---------------------------------------------------------------------------
void syncPlateNotesImpl() {
    for (int i = 0; i < plateManager.plateCount(); ++i) {
        std::vector<const gfcNote*> out;
        if (gfcReview* review = reviewForPlate(i)) {
            // Every revision, locked or not: a locked round stays VISIBLE,
            // which is the whole reason the model has rounds — you give
            // Wednesday's notes while looking at Monday's.
            for (const auto& revision : review->revisions)
                for (const auto& n : revision.notes)
                    if (n) out.push_back(n.get());
        }
        plateManager.setPlateNotes(i, out);
    }
}

// ---------------------------------------------------------------------------
// Mouse drawing.
// ---------------------------------------------------------------------------
bool g_noteDrawArmed = false;
int  g_drawPlate = -1;
std::unique_ptr<gfcNote> g_drawNote;

/** Points collected so far; for arrow/box only the first and last matter. */
std::vector<gfcNotePoint> g_drawPoints;

bool applyInboundNoteSyncEvents() {
    bool applied = false;
    for (auto& ev : networkManager.drainNoteSyncEvents()) {
        using Kind = jefe::net::NoteSyncEvent::Kind;
        if (ev.kind == Kind::Add && ev.note) {
            gfcReview* review = reviewForPlate(ev.note->quadID);
            if (!review) continue;
            gfcRevision* open = review->openRevision();
            if (!open) open = &review->beginRevision(ev.note->author);
            if (open->addNote(std::move(ev.note))) {
                gfcNoteStore::save(*review);
                applied = true;
            }
        } else if (ev.kind == Kind::Remove) {
            for (auto& review : g_noteReviews) {
                bool removedHere = false;
                for (auto& revision : review->revisions) {
                    if (revision.removeNote(ev.noteId)) { removedHere = true; break; }
                }
                if (removedHere) { gfcNoteStore::save(*review); applied = true; break; }
            }
        } else if (ev.kind == Kind::RevisionLock) {
            for (auto& review : g_noteReviews) {
                bool lockedHere = false;
                for (auto& revision : review->revisions) {
                    if (revision.id == ev.revisionId) {
                        revision.locked = true;
                        revision.modified = time(nullptr);
                        lockedHere = true;
                    }
                }
                if (lockedHere) { gfcNoteStore::save(*review); applied = true; }
            }
        } else if (ev.kind == Kind::RevisionUnlock) {
            for (auto& review : g_noteReviews) {
                bool unlockedHere = false;
                for (auto& revision : review->revisions) {
                    if (revision.id == ev.revisionId && revision.locked) {
                        revision.locked = false;
                        revision.modified = time(nullptr);
                        unlockedHere = true;
                    }
                }
                if (unlockedHere) { gfcNoteStore::save(*review); applied = true; }
            }
        }
    }
    return applied;
}

// Drawing-tool selection state (Task 6 does not wire mouse drawing -- see
// the plan's file-ownership map). Kept as plain file-scope state so a
// future viewport hook has one accessor to read instead of the dock
// reaching into gfcPlate.
int   g_activeNoteTool  = NOTETOOL_FREEHAND;
float g_noteColorR = 1.0f, g_noteColorG = 0.2f, g_noteColorB = 0.2f;
int   g_noteSize   = 3;
bool  g_notesVisible = true;

}  // namespace

std::vector<RevisionRow> notesForPlate(int plateIdx) {
    std::vector<RevisionRow> out;
    gfcReview* review = reviewForPlate(plateIdx);
    if (!review) return out;
    // Most-recent first: beginRevision() appends, so walk in reverse.
    for (auto it = review->revisions.rbegin(); it != review->revisions.rend(); ++it) {
        RevisionRow rr;
        rr.id      = it->id;
        rr.author  = it->author;
        rr.created = static_cast<long long>(it->created);
        rr.locked  = it->locked;
        for (auto& n : it->notes) {
            // This review may hold notes for a sibling plate showing the
            // same footage (gfcNote::quadID) -- only this plate's markup
            // belongs in its own dock.
            if (n->quadID != plateIdx) continue;
            NoteRow nr;
            nr.id        = n->id;
            nr.author    = n->author;
            nr.typeIndex = static_cast<int>(n->noteType());
            nr.quadID    = n->quadID;
            nr.from      = n->from;
            nr.to        = n->to;
            nr.always    = n->always;
            nr.colorR    = n->colorR;
            nr.colorG    = n->colorG;
            nr.colorB    = n->colorB;
            nr.size      = n->size;
            rr.notes.push_back(std::move(nr));
        }
        out.push_back(std::move(rr));
    }
    return out;
}

bool notesAvailableForPlate(int plateIdx) {
    return reviewForPlate(plateIdx) != nullptr;
}

int noteLockState(int plateIdx) {
    gfcReview* review = reviewForPlate(plateIdx);
    if (!review) return NOTELOCK_NONE;
    if (review->openRevision() != nullptr) return NOTELOCK_OPEN;
    if (!review->revisions.empty() && review->revisions.back().locked) return NOTELOCK_LOCKED;
    return NOTELOCK_NONE;
}

bool isNotesHost() {
    return !isRemoteConnected() || isRemoteServer();
}

std::string localAuthorName() {
    return sett.nickName.empty() ? std::string("local") : sett.nickName;
}

bool lockOpenRevision(int plateIdx) {
    if (!isNotesHost()) return false;
    gfcReview* review = reviewForPlate(plateIdx);
    if (!review) return false;
    gfcRevision* open = review->openRevision();
    if (!open) return false;
    open->locked = true;
    open->modified = time(nullptr);
    gfcNoteStore::save(*review);
    networkManager.broadcastRevisionLock(open->id);
    return true;
}

bool unlockLatestRevision(int plateIdx) {
    if (!isNotesHost()) return false;
    gfcReview* review = reviewForPlate(plateIdx);
    if (!review || review->revisions.empty()) return false;
    gfcRevision& latest = review->revisions.back();
    if (!latest.locked) return false;
    latest.locked = false;
    latest.modified = time(nullptr);
    gfcNoteStore::save(*review);
    // Without this the unlock stayed on the host's machine: every peer kept
    // the round locked and could not add to it, while the host could.
    networkManager.broadcastRevisionUnlock(latest.id);
    return true;
}

bool canRemoveNote(int plateIdx, const std::string& noteId) {
    gfcReview* review = reviewForPlate(plateIdx);
    if (!review) return false;
    for (auto& revision : review->revisions) {
        for (auto& n : revision.notes) {
            if (n->id != noteId) continue;
            if (revision.locked) return false;
            return isNotesHost() || n->author == localAuthorName();
        }
    }
    return false;
}

bool removeNoteFromPlate(int plateIdx, const std::string& noteId) {
    if (!canRemoveNote(plateIdx, noteId)) return false;
    gfcReview* review = reviewForPlate(plateIdx);
    if (!review) return false;
    for (auto& revision : review->revisions) {
        if (revision.removeNote(noteId)) {
            gfcNoteStore::save(*review);
            networkManager.broadcastNoteRemove(noteId);
            return true;
        }
    }
    return false;
}

void setActiveNoteTool(int tool) { g_activeNoteTool = tool; }
int  activeNoteTool() { return g_activeNoteTool; }

void setActiveNoteColor(float r, float g, float b) {
    g_noteColorR = r; g_noteColorG = g; g_noteColorB = b;
}
void getActiveNoteColor(float& r, float& g, float& b) {
    r = g_noteColorR; g = g_noteColorG; b = g_noteColorB;
}
void setActiveNoteSize(int size) { g_noteSize = size; }
int  activeNoteSize() { return g_noteSize; }

bool notesVisible() { return g_notesVisible; }
void setNotesVisible(bool visible) { g_notesVisible = visible; }

void syncPlateNotes() { syncPlateNotesImpl(); }

bool stampNotesIntoExr(int plateIdx, const std::string& outExr,
                       bool writeHeader, bool writeLayer,
                       NoteStampResult& result) {
    result = NoteStampResult{};
    auto fail = [&](const std::string& why) {
        result.error = why;
        return false;
    };

    // The header attribute and the named layer only mean something in EXR.
    // Refuse anything else up front rather than write a PNG that silently
    // dropped both.
    std::string ext = outExr.size() >= 4 ? outExr.substr(outExr.size() - 4) : std::string();
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    if (ext != ".exr") return fail("the output must be an .exr file");

    gfcSequence* seq = sequenceForPlate(plateIdx);
    gfcReview* review = reviewForPlate(plateIdx);
    if (!seq || !review) return fail("no media is loaded on this plate");

    // Resolve the file of what the plate is DISPLAYING, which lives in one of
    // two places.
    //
    // A sequence loaded for playback populates the decoded-frames list, and
    // framePathAt() gives that frame's path -- never getFilenameatFrame(),
    // which is a display string that appends " - NOT IN RAM" to any frame not
    // decoded right now.
    //
    // A still opened through Quick Load, drag-drop or --open-file leaves that
    // list EMPTY and is drawn from the sequence's preview frame instead
    // (gfcPlate::getFrameAndSequence, showPreview branch). Asking only the
    // frames list reported "frame 1 is outside this plate's sequence" over an
    // image that was plainly on screen -- so fall back to the preview frame's
    // own path.
    //
    // Both of those are filled by an ASYNCHRONOUS decode, so right after a load
    // either can still be empty: the same stamp failed at 4.5 s on one run and
    // succeeded on the next with no code change. filenameGeneric is set when
    // the sequence is identified, before any decode, and for a single still it
    // is simply that file's path -- so it closes the race for stills. A true
    // sequence pattern ("name.####.exr") is not a path, and guessing its frame
    // numbering would risk stamping the wrong file, so that case says what to
    // do instead of guessing.
    const int frame = getCurrentFrame();
    result.sourcePath = seq->framePathAt(frame);
    if (result.sourcePath.empty()) {
        result.sourcePath = seq->getPreviewFrame().fileName;
    }
    if (result.sourcePath.empty() &&
        !seq->filenameGeneric.empty() &&
        seq->filenameGeneric.find('#') == std::string::npos) {
        result.sourcePath = seq->filenameGeneric;
    }
    if (result.sourcePath.empty()) {
        return fail("frame " + std::to_string(frame) +
                    " is not decoded yet -- wait for loading to finish and try again");
    }

    std::vector<const gfcNote*> notes;
    for (const auto& rev : review->revisions) {
        for (const auto& n : rev.notes) {
            if (n) notes.push_back(n.get());
        }
    }
    result.noteCount = (int)notes.size();

    if (!gfcNoteStamp::imageSize(result.sourcePath, &result.width, &result.height)) {
        return fail("cannot read " + result.sourcePath);
    }

    gfcNoteStamp::Options opt;
    opt.writeHeader = writeHeader;
    std::string err;
    if (writeLayer) {
        // Rasterised at the SOURCE image's own size. Notes are normalised to
        // the source, so this mapping is exact -- none of the plate's pan,
        // zoom or crop is involved.
        if (!gfcNoteOverlay::rasterise(notes, frame, plateIdx,
                                       result.width, result.height,
                                       opt.layerRGBA, &err)) {
            return fail(err);
        }
        opt.layerWidth = result.width;
        opt.layerHeight = result.height;
        for (size_t p = 3; p < opt.layerRGBA.size(); p += 4) {
            if (opt.layerRGBA[p] != 0) ++result.markedTexels;
        }
    }

    if (!gfcNoteStamp::stamp(result.sourcePath, outExr, *review, opt, &err)) {
        return fail(err);
    }
    return true;
}

void addDemoNotes(int plateIdx) {
    gfcReview* review = reviewForPlate(plateIdx);
    if (!review) {
        printf("NOTES-DEMO: no media on plate %d\n", plateIdx);
        fflush(stdout);
        return;
    }
    gfcRevision* rev = review->openRevision();
    if (!rev) rev = &review->beginRevision("demo");

    auto stamp = [&](gfcNote& n, float r, float g, float bl, int size) {
        n.quadID = plateIdx;
        n.always = true;          // visible on every frame, so the shot is stable
        n.author = "demo";
        n.colorR = r; n.colorG = g; n.colorB = bl;
        n.size = size;
    };

    // A freehand squiggle circling something, the way a supervisor actually
    // marks a frame.
    auto stroke = std::make_unique<gfcNoteStroke>();
    for (int i = 0; i <= 40; ++i) {
        const float t = float(i) / 40.0f * 6.2831853f;
        stroke->pts.push_back(gfcNotePoint{
            0.30f + 0.10f * std::cos(t),
            0.34f + 0.13f * std::sin(t) });
    }
    stamp(*stroke, 1.0f, 0.25f, 0.20f, 3);
    rev->addNote(std::move(stroke));

    auto arrow = std::make_unique<gfcNoteArrow>();
    arrow->tail = gfcNotePoint{0.62f, 0.20f};
    arrow->head = gfcNotePoint{0.44f, 0.32f};
    stamp(*arrow, 1.0f, 0.78f, 0.20f, 3);
    rev->addNote(std::move(arrow));

    auto box = std::make_unique<gfcNoteBox>();
    box->a = gfcNotePoint{0.55f, 0.55f};
    box->b = gfcNotePoint{0.86f, 0.82f};
    stamp(*box, 0.35f, 0.85f, 1.0f, 2);
    rev->addNote(std::move(box));

    auto text = std::make_unique<gfcNoteText>();
    text->anchor = gfcNotePoint{0.55f, 0.52f};
    text->text = "too warm here";
    stamp(*text, 0.35f, 0.85f, 1.0f, 2);
    rev->addNote(std::move(text));

    printf("NOTES-DEMO: plate %d now has %d note(s)\n",
           plateIdx, (int)rev->notes.size());
    fflush(stdout);   // the process may be killed before stdio drains
    syncPlateNotesImpl();
}

bool noteDrawingArmed() { return g_noteDrawArmed; }
void setNoteDrawingArmed(bool armed) {
    g_noteDrawArmed = armed;
    if (!armed) noteDrawCancel();
}
bool noteDrawInProgress() { return g_drawNote != nullptr; }

void noteDrawCancel() {
    g_drawNote.reset();
    g_drawPoints.clear();
    g_drawPlate = -1;
}

bool noteDrawBegin(int xFb, int yFb, int plateIdx) {
    noteDrawCancel();
    if (!g_noteDrawArmed || plateIdx < 0) return false;

    gfcReview* review = reviewForPlate(plateIdx);
    if (!review) return false;
    // Refuse before the first point rather than at commit: letting someone
    // draw a whole stroke and then silently dropping it is worse than not
    // letting the stroke start.
    gfcRevision* open = review->openRevision();
    if (open && open->locked) return false;

    float nx = 0.0f, ny = 0.0f;
    if (!plateManager.cursorToNormalisedImage(xFb, yFb, plateIdx, nx, ny)) return false;

    switch (activeNoteTool()) {
        case NOTETOOL_ARROW: g_drawNote = std::make_unique<gfcNoteArrow>(); break;
        case NOTETOOL_BOX:   g_drawNote = std::make_unique<gfcNoteBox>();   break;
        case NOTETOOL_TEXT:  g_drawNote = std::make_unique<gfcNoteText>();  break;
        default:             g_drawNote = std::make_unique<gfcNoteStroke>(); break;
    }
    g_drawPlate = plateIdx;
    g_drawPoints.assign(1, gfcNotePoint{nx, ny});
    // A text note is placed where it is clicked. Without this its anchor stayed
    // at the default (0,0), so every text note landed in the image's top-left
    // corner no matter where it was put.
    if (auto* text = dynamic_cast<gfcNoteText*>(g_drawNote.get()))
        text->anchor = gfcNotePoint{nx, ny};

    g_drawNote->quadID = plateIdx;
    g_drawNote->author = localAuthorName();
    // A note drawn on a paused frame belongs to that frame. from==to is the
    // spec's default and the reviewer can widen it in the dock afterwards.
    const int f = getCurrentFrame();
    g_drawNote->from = f;
    g_drawNote->to   = f;
    getActiveNoteColor(g_drawNote->colorR, g_drawNote->colorG, g_drawNote->colorB);
    g_drawNote->size = activeNoteSize();
    return true;
}

void noteDrawAppend(int xFb, int yFb) {
    if (!g_drawNote || g_drawPlate < 0) return;
    float nx = 0.0f, ny = 0.0f;
    // A drag that leaves the image keeps the last point inside it rather than
    // ending the note — people overshoot constantly and losing the stroke for
    // it would be maddening.
    if (!plateManager.cursorToNormalisedImage(xFb, yFb, g_drawPlate, nx, ny)) return;
    g_drawPoints.push_back(gfcNotePoint{nx, ny});
    // Live feedback: the in-progress note is pushed into the plate each move
    // so the reviewer sees the line as they draw it, not on release.
    if (auto* stroke = dynamic_cast<gfcNoteStroke*>(g_drawNote.get()))
        stroke->pts = g_drawPoints;
    else if (auto* arrow = dynamic_cast<gfcNoteArrow*>(g_drawNote.get()))
        { arrow->tail = g_drawPoints.front(); arrow->head = g_drawPoints.back(); }
    else if (auto* box = dynamic_cast<gfcNoteBox*>(g_drawNote.get()))
        { box->a = g_drawPoints.front(); box->b = g_drawPoints.back(); }
    else if (auto* text = dynamic_cast<gfcNoteText*>(g_drawNote.get()))
        text->anchor = g_drawPoints.back();   // dragging a text note repositions it
}

bool noteDrawIsText() {
    return g_drawNote && g_drawNote->noteType() == GFCNOTE_TEXT;
}

void noteDrawSetText(const std::string& text) {
    if (auto* t = dynamic_cast<gfcNoteText*>(g_drawNote.get()))
        t->text = text;
}

bool noteDrawEnd() {
    if (!g_drawNote || g_drawPlate < 0) { noteDrawCancel(); return false; }

    // A click with no drag is a degenerate arrow or box — zero-area, invisible,
    // and pure clutter in the dock. A single-point freehand is a legitimate
    // dot, so only the two-point tools are rejected.
    const bool needsDrag = g_drawNote->noteType() == GFCNOTE_ARROW ||
                           g_drawNote->noteType() == GFCNOTE_BOX;
    if (needsDrag && g_drawPoints.size() < 2) { noteDrawCancel(); return false; }

    // The same reasoning for text: a text note with no words renders nothing,
    // so it is refused here as well as in the viewport's prompt -- a second
    // caller that forgets to set the text must not commit an invisible note.
    if (auto* t = dynamic_cast<gfcNoteText*>(g_drawNote.get())) {
        if (t->text.empty()) { noteDrawCancel(); return false; }
    }

    gfcReview* review = reviewForPlate(g_drawPlate);
    if (!review) { noteDrawCancel(); return false; }
    gfcRevision* open = review->openRevision();
    if (!open) open = &review->beginRevision(localAuthorName());

    const gfcNote& sent = *g_drawNote;
    networkManager.broadcastNoteAdd(sent);      // before the move steals it
    const bool added = open->addNote(std::move(g_drawNote));
    if (added) {
        gfcNoteStore::save(*review);
        syncPlateNotesImpl();
    }
    g_drawPoints.clear();
    g_drawPlate = -1;
    g_drawNote.reset();
    return added;
}
void toggleNotesVisible() { g_notesVisible = !g_notesVisible; }

}  // namespace jefe::qt

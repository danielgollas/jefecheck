// SPDX-License-Identifier: GPL-2.0-or-later
// Implementation lives in a Qt-free TU so it can include the manager headers
// (which pull glad) without colliding with QOpenGLWidget's system-OpenGL include.
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
#include "../gfcReviewSummary.h"
#include "../gfcnote.h"
#include "../gfcNoteStore.h"
#include "../gfcMediaKinds.h"
#include "../gfcNoteOverlay.h"
#include "../gfcNoteStamp.h"
#include "../gfcpickmanager.h"
#include "../xmlParser.h"
#include "../gfcSequence.h"
#include "../gfcMovieReader.h"
#include "../gfcimageloadermovie.h"
#include "../gfcYuvConverter.h"
#include "../gfcTimecode.h"
#include "../gfcsequencegui.h"
#include "../gfcTextRenderer.h"
#include "../ui/IApplication.h"
#include "gfcUploadPool.h"
#include "gfcIOSurface.h"
#include "gfcplategui_qt.h"
#include "gfcsequencegui_qt.h"
#include "AudioOutput_qt.h"
#include "../gfcAudioEngine.h"
#include "../gfcFrameCache.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
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

// Defined in gfcSequence.cpp; not declared in any header.
void findSequence(std::vector<std::string>& refFiles, std::string inputFilename,
                  std::string& label, int& startNum, int& endNum);

namespace jefe::qt {

// Set while a cloud connect owns the networkManager on a worker thread (~5s):
// pumpNetwork() skips update(), remote getters return empty, the overlay draws nothing.
static std::atomic<bool> gCloudConnectInFlight{false};

// Set once the app is tearing down (aboutToQuit); a detached cloud-connect
// worker checks it before touching networkManager during static destruction.
static std::atomic<bool> gBridgeShuttingDown{false};

bool cloudConnectInFlight() {
    return gCloudConnectInFlight.load(std::memory_order_acquire);
}

// The app's audio device (Qt Multimedia lives in AudioOutput_qt.cpp).
static gfcAudioOutput* gAudioOutput = nullptr;
static bool gAudioOutputEnabled = true;

void setAudioOutputEnabled(bool enabled) { gAudioOutputEnabled = enabled; }

// Stops the audio thread before globals tear down (it renders from
// audioEngine, and must not run while the process unmaps on exit).
static void shutdownAudioOutput() {
    if (!gAudioOutput) return;
    audioEngine.setOutput(nullptr);
    jefe::qt::audio::destroyOutput(gAudioOutput);
    gAudioOutput = nullptr;
}

void beginBridgeShutdown() {
    gBridgeShuttingDown.store(true, std::memory_order_release);
    shutdownAudioOutput();
}

void shutdownTracks() {
    trackManager.shutdown();
}

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
    // Set unconditionally: every supported GL stack has both extensions, and
    // without these gfcFX::load returns early with "FXs not Supported".
    sett.glsl = true;
    sett.fbo = true;

    plateManager.initializeWidgets();
    trackManager.initializeWidgets();
    playbackManager.initializeWidgets();

    // The rolling frame cache over the four tracks' frame tables. JEFECHECK_CACHE_BUDGET_MB
    // overrides the derived byte budget.
    for (int i = 0; i < 4; ++i)
        frameCache.attachTrack(i, &trackManager.getSequence(i)->frameTable());
    if (const char* mb = std::getenv("JEFECHECK_CACHE_BUDGET_MB")) {
        const long long v = std::atoll(mb);
        if (v > 0) frameCache.setTestBudget((int64_t)v << 20);
    }

    // Default LUT path; saved preferences override it. The LUT autoload itself waits
    // for a current GL context (MainWindow_Qt calls initializeInstallLUTs after the first paintGL).
    if (sett.lutPath.empty()) {
        sett.lutPath = ::getApplicationDataPath() + "FX/";
    }

    // Wire the color-pick subsystem; without it nothing registers with
    // pickManager and in-viewport overlays (histogram) never get click/drag.
    pickManager.registerDrawee(&plateManager);
    pickManager.registerNotifee(&plateManager);
    plateManager.registerPlatesAsPickNotifees();

    // Movie sound. The sink opens asynchronously; until it does (or with no device) the engine
    // can't lead and playback keeps FPS pacing.
    if (!gAudioOutput && gAudioOutputEnabled) {
        gAudioOutput = jefe::qt::audio::createOutput();
        audioEngine.setOutput(gAudioOutput);
        if (!gAudioOutput->open(&audioEngine))
            std::printf("Audio: no output device; movies play silently\n");
        // Paths that std::exit() without aboutToQuit (headless tests) still stop
        // the thread first; registered after the globals, so it runs before them.
        std::atexit(shutdownAudioOutput);
    }
}

namespace {
// Build the gfcPickFlags bitfield doPicking expects: BUTTON1 always set
// (left-button picks only); ctrl/alt/shift map to GFC_PICK_MODIFIER_*.
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
    // Path resolution mirrors the FX/LUT autoload: the bundled path, then the
    // dev-mode `./fonts/` symlink.
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

    // DPI scale drives the atlas bake size (fontSize * dpiScale texels) so
    // glyphs stay crisp on Retina. The shadow offset is in physical pixels.
    textRenderer().setDPIScale(dpiScale);
    textRenderer().setShadowEnabled(true);
    textRenderer().setShadowOffset(dpiScale, -dpiScale);
    textRenderer().setShadowColor(0, 0, 0, 0.5f);
    textRenderer().setShadowBlur(0);
}

void initializeInstallLUTs() {
    // LUT autoload path, first match wins (logged): sett.lutPath, then
    // <bundle Resources>/FX/ (install), then ./FX/ (dev symlink).
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
    // Sequences with decoded-but-not-uploaded frames need a tick so
    // trackManager.generateTextures() can drain them (hasPendingRawFrames() is O(1)).
    for (int i = 0; i < 4; ++i) {
        gfcSequence* seq = trackManager.getSequence(i);
        if (seq && seq->hasPendingRawFrames()) return true;
    }
    // A fading feedback message or settling flip/flop rotation needs the tick to
    // keep animating while playback is stopped.
    if (plateManager.hasActiveAnimations()) return true;
    // The remote chat/status overlay fading out (or chat entry) needs the tick
    // to keep repainting while playback is stopped, same as plate animations.
    if (networkManager.overlayAnimating()) return true;
    return false;
}

// Any time-based animation that needs a repaint every tick even when stopped:
// settling flip/flop, fading remote-pointer trails, the fading chat/status overlay.
bool hasActiveViewportAnimation() {
    return plateManager.hasActiveAnimations() || networkManager.overlayAnimating();
}

bool tickPlayback() {
    // playbackManager.update() advances currentFrame at target FPS and calls
    // plateManager.setChanged() when a new frame should display.
    playbackManager.update();

    // Drives the per-plate flip/flop rotations; without it gfcPlate::updateRot
    // never runs and flip/flop look like no-ops.
    plateManager.updateAnimations();

    // Drain one frame per sequence's rawFrames queue into a GL texture. Caller
    // MUST make the viewport's GL context current (uploads run on this thread).
    trackManager.generateTextures();

    // Pushes per-track widget state into the gfcSequenceGUI for each sequence
    // (mostly stubs in the Qt build).
    trackManager.updateTrackWidgets();

    // Drain the dirty flag so callers know when to redraw; any setChanged()
    // (drop, keys, mouse, frame advance) is picked up here.
    const bool dirty = plateManager.getChanged();
    return dirty;
}

namespace {
// Auto-advance edge state: ONCE mode clamps at endLimit but stays "playing", so
// "reached end" is prevFrame != endLimit -> currentFrame == endLimit going forward.
int  gPrevPlaybackFrame = -1;
bool gPlaylistAdvanceLatch = false;
// True only when the playing content came from a playlist entry; cleared by
// quick-load, drag-drop and clearPlaylist so auto-advance can't hijack playback.
bool gCurrentContentFromPlaylist = false;

// Mirror of gfcPlaybackManager::getEndLimit() (private) using public
// accessors; keep in sync with gfcplaybackmanager.cpp.
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
    // No-GL half of tickPlayback(): update() advances currentFrame off the wall
    // clock, so a high rate (e.g. 250 Hz) keeps the measured FPS at the target.
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
    // Same probe as needsPlaybackTick()'s second clause, so the timer enters the
    // GL context only when a decoded frame is waiting.
    for (int i = 0; i < 4; ++i) {
        gfcSequence* seq = trackManager.getSequence(i);
        if (seq && seq->hasPendingRawFrames()) return true;
    }
    return false;
}

bool consumePlateChanged() {
    // Destructively reads plateManager's dirty flag (any local or remote edit) so a
    // bare setChanged() while stopped forces one repaint.
    return plateManager.getChanged();
}

void uploadPendingTextures() {
    // GL half of tickPlayback(): drain one frame per sequence and upload it.
    // Caller MUST make the viewport's GL context current first.
    trackManager.generateTextures();
}

bool uploadPoolsNeedTrim() {
    static auto lastCheck = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now - lastCheck < std::chrono::seconds(1)) return false;
    lastCheck = now;
    const gfcUpload::Stats up = gfcUpload::stats();
    return up.stagingBytes > 0 || up.pooledTextureBytes > 0 || gfcSurface::poolStats().pooledBytes > 0;
}

void trimUploadPools() {
    // Bridges a cleared sequence and the next load (or a playlist advance); short enough that an
    // idle player gives the memory back.
    constexpr double kIdleSeconds = 2.0;
    gfcUpload::trimIdle(kIdleSeconds);
    gfcSurface::trimIdle(kIdleSeconds);
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
    // FLTK's Alt+I: set the in point at the current frame and load from there. The rolling cache
    // already loads around the playhead; with it off, the load refills forward from this frame.
    const int frame = playbackManager.getCurrentFrame();
    playbackManager.setInPoint(frame);
    if (!playbackManager.rollingCache()) playbackManager.loadFromHere(frame);
}

bool getRollingCache() { return playbackManager.rollingCache(); }

void setRollingCache(bool on) { playbackManager.setRollingCache(on); }

void loadAllFromFrame(int frame) {
    // FLTK's Alt+click on the timeline: every track loads from this timeline frame.
    trackManager.startLoadingAllAt(frame - 1);
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

// The Qt loop-mode combo uses 0/1/2 but the playback manager matches CBArgs
// values (LOOPMODEONCE_ID=22 ... BOUNCE=24); translate here or playback freezes.
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

void getTargetRate(int& num, int& den) {
    playbackManager.getTargetRate(num, den);
}

namespace {
// Copies what the status bar needs out of the active plate's movie. movieInfo()
// points into a reader this sequence does not own, so nothing is kept.
struct ActiveMovie { bool valid=false; int num=0, den=1; bool vfr=false; int64_t frameCount=0; std::string startTc; int offset=0; };
ActiveMovie activeMovie() {
    ActiveMovie m;
    const int q = plateManager.getActiveQuad();
    if (q < 0) return m;
    const int track = plateManager.getTrackOnPlate(q);
    if (track < 0) return m;
    gfcSequence* seq = trackManager.getSequence(track);
    if (!seq || !seq->isMovie()) return m;
    const gfcMovieInfo* info = seq->movieInfo();
    if (!info || info->rateNum <= 0 || info->rateDen <= 0) return m;
    m.valid = true; m.num = info->rateNum; m.den = info->rateDen; m.vfr = info->variableRate;
    m.frameCount = info->frameCount; m.startTc = info->startTimecode; m.offset = seq->getOffset();
    return m;
}
}  // namespace

std::string activeTrackSourceTimecode() {
    const ActiveMovie m = activeMovie();
    if (!m.valid) return {};
    const int64_t idx = (int64_t)playbackManager.getCurrentFrame() - 1 - m.offset;
    if (idx < 0 || idx >= m.frameCount) return {};
    gfcTimecode start;
    int64_t startFrames = 0;
    bool drop = false;
    if (!m.startTc.empty() && gfcParseTimecode(m.startTc, start)) {
        startFrames = gfcTimecodeToFrames(start, m.num, m.den);
        drop = start.drop;
    }
    return gfcFormatTimecode(gfcFramesToTimecode(startFrames + idx, m.num, m.den, drop));
}

bool activeTrackIsVariableRate() {
    const ActiveMovie m = activeMovie();
    return m.valid && m.vfr;
}

float activeTrackMovieAverageFPS() {
    const ActiveMovie m = activeMovie();
    return m.valid ? (float)m.num / (float)m.den : 0.0f;
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

float getMeasuredFPS() {
    return playbackManager.getCurrentFPS();
}

void autoloadLUTs(const std::string& path) {
    // std::filesystem rather than FLTK's fl_filename_list to enumerate the directory.
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

// Walk `path` and load every .jfx file (alphabetical); a GL context must be
// current. Yields the event loop between loads or the shader compiles block AX/WDA.
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
        // lutManager.loadLUT prints a generic "Error loading LUT" with no filename;
        // tag it so the user can fix or remove the file.
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
    // Reuses sett.lutPath: LUTs and FXs ship from the same FX/ directory.
    // initializeInstallLUTs sets it; scan it again for .jfx files.
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
    // fxManager::getFX returns a copy and gfcFXStack::addFX stores its own. An
    // out-of-range index returns a default gfcFX (no shaders, pass-through).
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
    // gfcFXStack lacks a single-FX remove: clear the stack and re-add everything
    // except the removed entry (stacks are small, addFX is a vector push).
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
    // getMenuNames() walks fxManager's fxArray in order, as getFX(i) does, so
    // the returned indices are valid fxIndex args for addFXToActivePlate.
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
    // 3D LUTs for an FX cube param. `.first` is the GLOBAL lutManager index (read
    // by gfcFX::bind()); `.second` is the display name.
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
    // Routes through the plate manager so the edit is applied and broadcast to
    // remote peers; setFXWidgetValue calls setChanged() and clears the histogram cache.
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
    // CreateRenderFilename concatenates path+prefix, so the output directory
    // needs a trailing separator (QFileDialog returns one without it).
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

// Push a .jpl path onto the recent-playlists list (dedup, newest last, capped).
// Every playlist open/save funnels through loadPlaylistFile/savePlaylistFile.
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
        // scale is a float: a fraction (1.0 == 100%) or already a percent. Upsampling
        // isn't supported, so anything > 1.5 is treated as a percent.
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

void connectAsCloudHost(const RemoteCloudHostParams& params) {
    if (gBridgeShuttingDown.load(std::memory_order_acquire)) return;
    if (networkManager.getConnected()) return;
    gfcServerParams sp;
    // The host's name reaches participants as its nickname
    // (gfcnetworkmanager.cpp: clientParams.nickname = server.getName()).
    const std::string hostName =
        params.hostName.empty() ? std::string("Host") : params.hostName;
    std::snprintf(sp.serverName, sizeof(sp.serverName), "%s", hostName.c_str());
    // No password: the coordinator protocol has none (see RemoteCloudHostParams).
    sp.password[0]     = '\0';
    sp.port            = 0;              // ignored in coordinator mode
    sp.coordinatorMode = true;
    sp.coordinatorUrl  = params.coordinatorUrl;
    sp.authToken       = params.authToken;
    sp.policy.requireKnock       = params.requireKnock;
    sp.policy.password           = params.sessionPassword;
    sp.policy.idleTimeoutMinutes = params.idleTimeoutMinutes;
    sp.policy.maxParticipants    = params.maxParticipants;
    // BLOCKS up to ~5s for the coordinator-assigned code; callers run this off
    // the GUI thread. Hold the in-flight guard so the GUI pump doesn't race it.
    gCloudConnectInFlight.store(true, std::memory_order_release);
    networkManager.startServer(&sp);
    gCloudConnectInFlight.store(false, std::memory_order_release);
}

void connectAsCloudClient(const RemoteCloudJoinParams& params) {
    if (gBridgeShuttingDown.load(std::memory_order_acquire)) return;
    if (networkManager.getConnected()) return;
    gfcConnectionParams cp;
    cp.nickname        = params.clientName;
    cp.serverIP        = "";            // ignored in coordinator mode
    cp.port            = 0;
    cp.password        = "";            // coordinator protocol has no password
    cp.coordinatorMode = true;
    cp.coordinatorUrl  = params.coordinatorUrl;
    cp.sessionCode     = params.sessionCode;
    cp.authToken       = params.authToken;
    gCloudConnectInFlight.store(true, std::memory_order_release);
    networkManager.startConnection(&cp);
    gCloudConnectInFlight.store(false, std::memory_order_release);
}

std::string remoteSessionCode() {
    // A cloud connect worker owns the manager — don't read mid-mutation.
    if (gCloudConnectInFlight.load(std::memory_order_acquire)) return {};
    return networkManager.getAssignedSessionCode();
}

void disconnectRemote() {
    if (!networkManager.getConnected()) return;
    if (networkManager.getIsServer()) networkManager.stopServer();
    else                              networkManager.stopConnection();
}

bool isRemoteConnected() {
    if (gCloudConnectInFlight.load(std::memory_order_acquire)) return false;
    return networkManager.getConnected();
}

bool isRemoteServer() {
    if (gCloudConnectInFlight.load(std::memory_order_acquire)) return false;
    return networkManager.getIsServer();
}

RemoteUiState remoteUiState() {
    RemoteUiState s;

    // A connect worker owns the manager while this is set and every field is
    // mid-mutation, so report Connecting rather than a torn mix.
    if (gCloudConnectInFlight.load(std::memory_order_acquire)) {
        s.phase = RemotePhase::Connecting;
        s.statusText = "Connecting…";
        return s;
    }

    if (!networkManager.getConnected()) {
        // Knocking is NOT offline: nothing is connected yet but a human is
        // deciding; showing the connect forms again reads as a failed join.
        if (networkManager.isAwaitingAdmission()) {
            s.phase = RemotePhase::Knocking;
            s.statusText = "Waiting for the host to let you in…";
            return s;
        }
        // Admitted but still completing the P2P handshake: reporting Offline would
        // flash the connect forms back up right after the host said yes.
        if (networkManager.isAttemptingConnection()) {
            s.phase = RemotePhase::Connecting;
            s.statusText = networkManager.connectionStatusText();
            if (s.statusText.empty()) s.statusText = "Connecting…";
            return s;
        }
        s.phase = RemotePhase::Offline;
        s.statusText = networkManager.connectionStatusText();
        if (s.statusText.empty()) s.statusText = "Not connected";
        return s;
    }

    s.inSession = true;
    s.isHost = networkManager.getIsServer();

    // A cloud host is a host WITH a coordinator-assigned code; resolved once here.
    const std::string code = s.isHost ? networkManager.getAssignedSessionCode()
                                      : std::string();
    if (s.isHost) {
        s.phase = code.empty() ? RemotePhase::HostingLan : RemotePhase::HostingCloud;
        s.sessionCode = code;
        s.showCredits = !code.empty();
        // The lobby exists only where the coordinator does; a LAN host has no
        // admission step.
        if (!code.empty()) {
            for (const auto& p : networkManager.pendingJoiners()) {
                RemotePendingJoiner r;
                r.joinerId = p.joinerId;
                r.displayName = p.displayName;
                r.email = p.email;
                r.verified = p.verified;
                s.pending.push_back(std::move(r));
            }
        }
    } else {
        s.phase = RemotePhase::Joined;
    }

    s.statusText = networkManager.connectionStatusText();
    if (s.statusText.empty()) {
        s.statusText = s.isHost ? "Hosting" : "Connected";
    }
    return s;
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
        // Walk groups in insertion order: gfcFX stores widgetsOrder per group, but
        // std::map iterates group names alphabetically.
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
    // The Qt panel uses row 0 = "(No LUT)", row 1+ = lutManager entry + 1;
    // gfcPlate::setLUT takes the raw index (-1 = no LUT, an empty CubeLUT).
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

    // 3D: build a structured working grid (with adjacency, for faces / lattice /
    // dots), subsampling large cubes to a bounded edge.
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
// plateManager to sum onto the current field rather than overwrite.
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
    // setPlateShowPreview only writes the Qt GUI side; use updatePlatesFromGUI, not
    // updateAllFromGUI, which also resets layout and active quad from stale fields.
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

void clearTextModeAll() {
    plateManager.clearTextModeAll();
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
    // filenameGeneric holds the source path. Empty = nothing loaded yet.
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
    // getPreviewFrame() returns a shallow copy (gfcFrame has no destructor). An
    // unloaded frame defaults to the 15x15 sentinel with loaded==false; gate on both.
    gfcFrame f = seq->getPreviewFrame();
    if (!f.loaded || f.sizeX <= 0 || f.sizeY <= 0) return {};
    const double ratio =
        static_cast<double>(f.sizeX) / static_cast<double>(f.sizeY);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f:1", ratio);
    return std::string(buf);
}

bool runAspectSelfTest() {
    // Exercise the plate card's path. calculatePolySizesCropEtc treats the aspect
    // as content HEIGHT/WIDTH, so a landscape "W:H" preset MUST map to (0,1).
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
    // Fit uses the drawn size: 1920x1080 at 2.39:1 draws 1920x803, so a 2000x500 viewport
    // fits it at 500/803, not 500/1080.
    struct FitCase { const char* name; float aspect; bool crop; float vpW, vpH, expected; };
    const FitCase fits[] = {
        {"2.39:1 wide viewport", 1.0f / 2.39f, false, 2000, 500, 500.0f / 803.0f},
        {"original",             -1.0f,        false, 2000, 500, 500.0f / 1080.0f},
        {"2.39:1 cropped",       1.0f / 2.39f, true,  2000, 500, 500.0f / 1080.0f},
        {"smaller than viewport", 1.0f / 2.39f, false, 4000, 4000, 1.0f},
    };
    for (const auto& c : fits) {
        float w = 0, h = 0;
        gfcPlate::displaySize(1920, 1080, 1920, 1080, c.aspect, c.crop, w, h);
        const float got = gfcPlate::fitZoom(c.vpW, c.vpH, w, h);
        const bool ok = std::fabs(got - c.expected) < 1e-3f;
        if (!ok) pass = false;
        std::printf("  fit %s: drawn=%gx%g zoom=%.4f expected=%.4f ok=%d\n",
                    c.name, w, h, got, c.expected, ok ? 1 : 0);
    }
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

    // Order matters: setChannel(name) must land before loadPreview(), which picks
    // the layer from params.channelName; otherwise it re-decodes the old layer.
    seq->myGUI->setChannel(layerName);

    // Re-decode the preview so the texture on screen reflects the choice.
    seq->loadPreview();

    // The async loader caches the channel in each frame's load params at
    // startLoadingSequence time, so a layer switch needs a full re-decode.
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

    // Apply the global default bit depth before loadPreview reads it
    // (sett.defaultTextureFormat holds the GFC_*BPC / GFC_*HALF enum directly).
    seq->myGUI->setCompression(sett.defaultTextureFormat);

    // Translate the 0..1 scale factor to the percentage string the Choice widget
    // convention expects ("100", "50", "25"). Clamp to (0, 1]: the loader can't upsample.
    if (scale <= 0.0f) scale = 1.0f;
    if (scale > 1.0f) scale = 1.0f;
    int pct = int(scale * 100.0f + 0.5f);
    // Sub-1% inputs round to 0, which the loader doesn't define.
    if (pct < 1)   pct = 100;
    if (pct > 100) pct = 100;
    char scaleBuf[8];
    std::snprintf(scaleBuf, sizeof(scaleBuf), "%d", pct);
    seq->myGUI->setScale(scaleBuf);

    const std::string loaded = seq->loadPreview();
    if (loaded.empty()) {
        return false;
    }

    // updateValuesFromGUI copies showPreview, scale, track and channel masks from
    // myGUI; the Qt build never calls it at startup, so call it here.
    plateManager.updateAllFromGUI();

    // If loadPreview() found > 1 frame, start the async loader via trackManager
    // (not seq->startLoading), which also sets playbackManager's from/to.
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

namespace { bool gSessionRatePending = false; }  // a restored session's rate must survive the track loads that follow it

int startLoadingAllTracks() {
    // Loading tracks directly (Load Window "Load All", Open Session) is not a
    // playlist load — disarm auto-advance so it can't hijack this content.
    gCurrentContentFromPlaylist = false;
    int started = 0;
    playbackManager.setKeepRateOnLoad(gSessionRatePending);
    gSessionRatePending = false;
    for (int i = 0; i < 4; ++i) {
        auto* seq = trackManager.getSequence(i);
        if (!seq || !seq->myGUI) continue;
        if (seq->myGUI->getFilename().empty()) continue;
        trackManager.stopLoadingSequence(i);
        seq->stopLoading();
        trackManager.startLoadingSequence(i);
        ++started;
    }
    playbackManager.setKeepRateOnLoad(false);
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
    // gfcSequence ranges are 0-based but the timeline and playback limits are
    // 1-based (getEndLimit/getStartLimit add +1); convert so the last slice fills.
    s.rangeStart = seq->getRangeStart() + 1;   // offset already folded in
    s.rangeEnd   = seq->getRangeEnd() + 1;
    s.numFrames  = seq->getNumFrames();
    s.loadedCount = seq->residentCount();
    s.loadedVersion = seq->loadedVersion();
    const int firstResident = seq->frameTable().firstResident();
    s.firstLoadedFrame = firstResident >= 0 ? firstResident + 1 + s.offset : s.rangeStart;
    s.hasAudio = seq->hasAudio();
    s.muted = seq->audioMuted();
    s.audioPeaksVersion = s.hasAudio ? seq->audioPeaksVersion() : 0;
    if (seq->myGUI && !seq->myGUI->getFilename().empty()) {
        namespace fs = std::filesystem;
        s.label = fs::path(seq->filenameGeneric.empty()
                               ? seq->myGUI->getFilename()
                               : seq->filenameGeneric).filename().string();
    }
    return s;
}

std::vector<std::pair<int, int>> getTrackLoadedSegments(int trackIdx) {
    std::vector<std::pair<int, int>> segs;
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq) return segs;
    seq->loadedSegments(segs);
    const int shift = 1 + seq->getOffset();   // 0-based source index -> 1-based timeline
    for (auto& r : segs) { r.first += shift; r.second += shift; }
    return segs;
}

TrackAudioState getTrackAudioState(int trackIdx) {
    TrackAudioState a;
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq) return a;
    a.hasAudio = seq->hasAudio();
    a.muted = seq->audioMuted();
    return a;
}

void setTrackMuted(int trackIdx, bool muted) {
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq) return;
    seq->setAudioMuted(muted);
    plateManager.setChanged();
}

WaveformPeaks getTrackWaveform(int trackIdx) {
    WaveformPeaks w;
    auto* seq = trackManager.getSequence(trackIdx);
    if (!seq || !seq->hasAudio()) return w;
    gfcAudioPeaks snap = seq->audioPeaksSnapshot();
    const gfcMovieInfo* info = seq->movieInfo();   // raw pointer into the reader: copy now
    if (!info || info->rateNum <= 0 || info->rateDen <= 0) return w;
    w.rateNum = info->rateNum; w.rateDen = info->rateDen;
    w.present = true;
    w.version = snap.version;
    w.binsPerSecond = snap.binsPerSecond;
    w.firstSample = snap.firstSample;
    w.offset = seq->getOffset();
    w.peaks = std::move(snap.peaks);
    return w;
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
    // The OIIO loader picks the EXR layer from params.channelName, not the int
    // index: resolve idx -> name via the channel-options cache and write it too.
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
    gSessionRatePending = true;
    sessionManager.loadSession(path);   // caller made GL context current
    plateManager.setChanged();
    return true;
}

bool getHasRecoverableSession() { return sessionManager.checkCrashedSession(); }

bool loadRecoverySession() {
    if (!sessionManager.checkCrashedSession()) return false;
    gSessionRatePending = true;
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

std::string recoverySessionPath() { return sessionManager.crashSessionPath(); }

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

// All of these read gfcNetworkManager state: while a cloud connect worker owns
// it (gCloudConnectInFlight) return empty/cached instead of reading mid-mutation.
std::vector<std::string> remoteParticipants() {
    if (gCloudConnectInFlight.load(std::memory_order_acquire)) return {};
    return networkManager.participantNames();
}

// Per-peer health view: maps the active transport's stats to RemotePeerStat
// with the running byte total (kbps derives from deltas). Honors gCloudConnectInFlight.
std::vector<RemotePeerStat> remotePeerStats() {
    if (gCloudConnectInFlight.load(std::memory_order_acquire)) return {};
    std::vector<RemotePeerStat> out;
    for (const auto& ps : networkManager.peerStats()) {
        RemotePeerStat r;
        std::string nick = networkManager.peerNickname(ps.peer);
        r.name = !nick.empty() ? nick : ("peer " + std::to_string(ps.peer));
        r.rttMs = ps.rttMs;
        r.bytes = static_cast<unsigned long long>(ps.bytesSent) +
                  static_cast<unsigned long long>(ps.bytesReceived);
        switch (ps.path) {
            case jefe::net::PeerStats::Path::Direct: r.path = "direct"; break;
            case jefe::net::PeerStats::Path::Relay:  r.path = "relay";  break;
            default:                                 r.path = "n/a";    break;
        }
        r.connected = ps.connected;
        out.push_back(std::move(r));
    }
    return out;
}
std::string remoteCoordinatorErrorCode() {
    if (gCloudConnectInFlight.load(std::memory_order_acquire)) return {};
    std::string code, msg;
    if (!networkManager.lastCoordinatorError(code, msg)) return {};
    return code;
}

std::string remoteCoordinatorErrorMessage() {
    if (gCloudConnectInFlight.load(std::memory_order_acquire)) return {};
    std::string code, msg;
    if (!networkManager.lastCoordinatorError(code, msg)) return {};
    return msg;
}

void remoteDecideJoiner(const std::string& joinerId, bool admit) {
    // A decision during a cloud connect would reach a manager a worker thread
    // owns; the other manager writers honor the same flag.
    if (gCloudConnectInFlight.load(std::memory_order_acquire)) return;
    networkManager.decideJoiner(joinerId, admit);
}

void showViewportMessage(const std::string& text) {
    plateManager.setFeedbackMessage(text);
}

std::string remoteStatusText() {
    if (gCloudConnectInFlight.load(std::memory_order_acquire)) return {};
    return networkManager.connectionStatusText();
}
std::vector<std::string> remoteChatLog() {
    if (gCloudConnectInFlight.load(std::memory_order_acquire)) return {};
    return networkManager.chatLogLines();
}
std::vector<std::string> remoteErrors() {
    if (gCloudConnectInFlight.load(std::memory_order_acquire)) return {};
    return networkManager.drainErrors();
}
std::vector<std::string> remoteNetworkLog() {
    if (gCloudConnectInFlight.load(std::memory_order_acquire)) return {};
    return networkManager.networkLogLines();
}

std::vector<ChatEntry> remoteChatEntries() {
    if (gCloudConnectInFlight.load(std::memory_order_acquire)) return {};
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

// Forward-declared so pumpNetwork() can drain inbound note sync events;
// defined with the Notes dock support near the end of this file.
namespace { bool applyInboundNoteSyncEvents(); void syncPlateNotesImpl(); }

bool pumpNetwork() {
    static bool        prevConnected = false;
    static size_t      prevPeers     = 0;
    static size_t      prevChat      = 0;
    static size_t      prevPending   = 0;
    static std::string prevStatus;
    // A worker is inside startServer/startConnection (cloud connect): skip this
    // tick. See gCloudConnectInFlight.
    if (gCloudConnectInFlight.load(std::memory_order_acquire)) return false;
    networkManager.update();
    const bool        nowConnected = networkManager.getConnected();
    const size_t      nowPeers     = networkManager.participantNames().size();
    const size_t      nowChat      = networkManager.chatLogLines().size();
    const std::string nowStatus    = networkManager.connectionStatusText();
    // A knock arrives on the coordinator's own socket and builds no peer or
    // transport event, so none of the signals above move; poll for it.
    const size_t      nowPending   = networkManager.pendingJoiners().size();
    // Repaint on any inbound packet: client.Update() applies mirrored state, but
    // QOpenGLWidget only repaints on local input.
    const bool gotInbound = networkManager.consumeGotMessages();
    // Apply inbound note add/remove/lock events into the in-memory gfcReview store;
    // folded into the same "changed" signal so the tick refreshes the Notes dock.
    const bool notesApplied = applyInboundNoteSyncEvents();
    // A note that arrived from a peer changed the store; push it through to
    // the plates or it stays invisible until something else happens to sync.
    if (notesApplied) syncPlateNotesImpl();
    const bool changed = (nowConnected != prevConnected) ||
                         (nowPeers != prevPeers) || (nowChat != prevChat) ||
                         (nowStatus != prevStatus) ||
                         (nowPending != prevPending) || gotInbound || notesApplied;
    prevConnected = nowConnected; prevPeers = nowPeers; prevChat = nowChat;
    prevPending = nowPending; prevStatus = nowStatus;
    return changed;
}

// Child/client role: connect, pump until connected (or timeout), optionally
// start playback, then hold. Both halves print timestamped milestones.
namespace {
long long remoteTestMsSince(const std::chrono::steady_clock::time_point& t0) {
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0).count();
}
}  // namespace

void remoteTestPeerConnect(const std::string& ip, int port, int holdMs, bool play,
                           int connectTimeoutMs) {
    const auto t0 = std::chrono::steady_clock::now();
    RemoteClientParams cp; cp.clientName = "peer"; cp.serverIP = ip; cp.port = port; cp.password = "";
    connectAsClient(cp);
    printf("REMOTE-PEER: +%lldms connectAsClient returned\n", remoteTestMsSince(t0));
    fflush(stdout);
    // Wait until connected before toggling play (the message is sent ONCE, so an
    // earlier toggle is dropped); WebRTC needs the caller's larger timeout.
    for (int t = 0; t < connectTimeoutMs && !isRemoteConnected(); t += 10) {
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

// Split-phase orchestrator for the WebRTC harness: bring the host's loopback
// fully up BEFORE the peer spawns, or the peer's one-shot play is lost.

// Start the host and pump until the loopback client has registered (its channel
// is open both ways). Returns true once the loopback is a live participant.
bool remoteTestServerStart(int port, int loopbackTimeoutMs) {
    RemoteServerParams sp; sp.serverName = "jefe-remote-test"; sp.port = port; sp.password = "";
    connectAsServer(sp);
    for (int t = 0; t < loopbackTimeoutMs; t += 10) {
        pumpNetwork();
        // The loopback client registering its own nickname means its WebRTC
        // channel is open both directions and it is ready to receive forwards.
        if (!networkManager.participantNames().empty()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

// Pump for up to settleMs, reporting whether a mirrored play arrived. Assumes
// the server was already started via remoteTestServerStart.
bool remoteTestServerSettleForPlay(int settleMs) {
    for (int t = 0; t < settleMs; t += 10) {
        pumpNetwork();
        if (isPlaying()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

// ── --asset-test late-join LUT/FX transfer harness ──────────────────────────
// Proves a peer joining AFTER the host loaded a LUT/FX receives it over the wire.

// First hash in `after` not in `before` (the asset loadLUT/loadFX just
// added); "" if the load failed or produced no valid content hash.
static std::string firstNewHash(const std::vector<std::string>& before,
                                const std::vector<std::string>& after) {
    for (const auto& h : after) {
        bool seen = false;
        for (const auto& b : before) if (b == h) { seen = true; break; }
        if (!seen && !h.empty()) return h;
    }
    return "";
}

// Host role: load a fixture LUT and return its content hash ("" on failure).
std::string assetTestLoadLUT(const std::string& path) {
    auto before = lutManager.getHashes();
    lutManager.loadLUT(path);
    return firstNewHash(before, lutManager.getHashes());
}

// Host role: load a fixture FX. Headless (no GL) gfcFX sets no content hash,
// so "" means "FX not applicable".
std::string assetTestLoadFX(const std::string& path) {
    auto before = fxManager.getHashes();
    fxManager.loadFX(path);
    return firstNewHash(before, fxManager.getHashes());
}

// Host role: bring up the RakNet server (instant connect). The FX/LUT sync
// fires automatically when the peer joins.
bool assetTestServerStart(int port) {
    RemoteServerParams sp; sp.serverName = "jefe-asset-test"; sp.port = port; sp.password = "";
    connectAsServer(sp);
    return true;
}

// Host role: pump the server for `ms` while the peer connects + runs the sync
// handshake (the server must Update() to process each handshake round-trip).
void assetTestServerPump(int ms) {
    for (int t = 0; t < ms; t += 10) {
        pumpNetwork();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// TU-safe getters for the peer's post-sync manager state.
bool remoteHasLUTHash(const std::string& hash) {
    if (hash.empty()) return false;
    return lutManager.getHashMap().count(hash) > 0;
}
int remoteLUTCount() { return (int)lutManager.getHashMap().size(); }
bool remoteHasFXHash(const std::string& hash) {
    if (hash.empty()) return false;
    return fxManager.getHashMap().count(hash) > 0;
}
int remoteFXCount() { return (int)fxManager.getHashMap().size(); }

// ── --coord-test cloud-coordinator E2E harness ──────────────────────────────
// Like the WebRTC LAN harness, via a coordinator; startServer waits for the code.

// Start the host in coordinator mode and bring its loopback client fully up;
// true once it is a live participant. `coordUrl` is a ws:// URL.
bool coordTestHostStart(const std::string& coordUrl, int loopbackTimeoutMs) {
    if (networkManager.getConnected()) return false;
    gfcServerParams sp;
    std::snprintf(sp.serverName, sizeof(sp.serverName), "%s", "jefe-coord-host");
    sp.password[0] = '\0';
    sp.port = 0;                       // ignored in coordinator mode
    sp.coordinatorMode = true;
    sp.coordinatorUrl  = coordUrl;
    // startServer create-session's, waits for the coordinator-assigned code,
    // then connects the loopback client by that code (all inside startServer).
    networkManager.startServer(&sp);
    if (networkManager.getAssignedSessionCode().empty()) return false;
    // Pump until the loopback client has registered its nickname on the server
    // (its WebRTC channel is open both ways).
    for (int t = 0; t < loopbackTimeoutMs; t += 10) {
        pumpNetwork();
        if (!networkManager.participantNames().empty()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

bool coordLiveHostStart(const std::string& coordUrl, const std::string& authToken,
                        int loopbackTimeoutMs) {
    if (networkManager.getConnected()) return false;
    gfcServerParams sp;
    std::snprintf(sp.serverName, sizeof(sp.serverName), "%s", "jefe-live-host");
    sp.password[0] = '\0';
    sp.port = 0;
    sp.coordinatorMode = true;
    sp.coordinatorUrl  = coordUrl;
    sp.authToken       = authToken;
    // DEFAULT policy on purpose: overriding requireKnock would test a
    // configuration no real host starts in.
    networkManager.startServer(&sp);
    if (networkManager.getAssignedSessionCode().empty()) return false;
    for (int t = 0; t < loopbackTimeoutMs; t += 10) {
        pumpNetwork();
        if (!networkManager.participantNames().empty()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

int coordLiveAwaitAndAdmit(int timeoutMs) {
    // JEFE_LIVE_DENY=1 refuses instead, so the harness can check the other
    // branch: a denied joiner must be told no and never join the roster.
    const char* denyEnv = std::getenv("JEFE_LIVE_DENY");
    const bool admitThem = !(denyEnv && denyEnv[0] == '1');
    // JEFE_LIVE_ADMIT_DELAY_MS holds a knock before deciding, standing in for a
    // real host; without it the joiner's "waiting" state is never observed.
    int holdMs = 0;
    if (const char* d = std::getenv("JEFE_LIVE_ADMIT_DELAY_MS")) holdMs = std::atoi(d);

    int decided = 0;
    for (int t = 0; t < timeoutMs; t += 10) {
        pumpNetwork();
        // Read through remoteUiState(), not the manager, to exercise the path the
        // panel renders from.
        const RemoteUiState st = remoteUiState();
        for (const auto& p : st.pending) {
            std::printf("COORD-LIVE-TEST: knock name=[%s] email=[%s] verified=%d\n",
                        p.displayName.c_str(), p.email.c_str(), p.verified ? 1 : 0);
            std::fflush(stdout);
            for (int h = 0; h < holdMs; h += 10) {
                pumpNetwork();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            remoteDecideJoiner(p.joinerId, admitThem);
            ++decided;
        }
        // Keep pumping after the decision: an admitted joiner still has a WebRTC
        // handshake to finish.
        if (decided > 0 && st.pending.empty() && !admitThem) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return decided;
}

void coordLivePeerJoin(const std::string& coordUrl, const std::string& code,
                       int timeoutMs) {
    RemoteCloudJoinParams p;
    p.clientName     = "live-peer";
    p.coordinatorUrl = coordUrl;
    p.sessionCode    = code;
    connectAsCloudClient(p);

    auto phaseName = [](RemotePhase ph) {
        switch (ph) {
            case RemotePhase::Offline:      return "Offline";
            case RemotePhase::Connecting:   return "Connecting";
            case RemotePhase::Knocking:     return "Knocking";
            case RemotePhase::HostingLan:   return "HostingLan";
            case RemotePhase::HostingCloud: return "HostingCloud";
            case RemotePhase::Joined:       return "Joined";
        }
        return "?";
    };

    std::string last;
    for (int t = 0; t < timeoutMs; t += 20) {
        pumpNetwork();
        const RemoteUiState st = remoteUiState();
        const std::string now = phaseName(st.phase);
        if (now != last) {
            last = now;
            std::printf("COORD-LIVE-PEER: phase=%s status=[%s]\n", now.c_str(),
                        st.statusText.c_str());
            std::fflush(stdout);
        }
        if (st.phase == RemotePhase::Joined) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

// The coordinator-assigned session code (empty until the host session is up).
std::string coordTestGetCode() {
    return networkManager.getAssignedSessionCode();
}

// Peer/child role: join the coordinator by code in coordinator mode, wait until
// connected, optionally toggle play (mirrored to the host over P2P), then hold.
void coordTestPeerJoin(const std::string& coordUrl, const std::string& code,
                       int holdMs, bool play, int connectTimeoutMs) {
    if (networkManager.getConnected()) return;
    gfcConnectionParams cp;
    cp.nickname        = "coord-peer";
    cp.serverIP        = "";           // ignored in coordinator mode
    cp.port            = 0;
    cp.password        = "";
    cp.coordinatorMode = true;
    cp.coordinatorUrl  = coordUrl;
    cp.sessionCode     = code;
    networkManager.startConnection(&cp);
    // Wait until connected before toggling play (the message is sent ONCE);
    // Coordinator + ICE + DTLS needs the larger caller-supplied timeout.
    for (int t = 0; t < connectTimeoutMs && !isRemoteConnected(); t += 10) {
        pumpNetwork();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (play) togglePlayFwd();          // sends a play/pause message to the host
    for (int t = 0; t < holdMs; t += 10) {
        pumpNetwork();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// Orchestrator: pump for up to settleMs, reporting whether the peer's mirrored
// play reached this host via the loopback. Assumes coordTestHostStart ran.
bool coordTestSettleForPlay(int settleMs) {
    return remoteTestServerSettleForPlay(settleMs);
}

// --- Chat overlay + keyboard chat entry ------------------------------------

void drawNetworkOverlay(int w, int h) {
    // networkManager.draw() reads connected/allReady/client: skip it while a
    // cloud connect worker owns the manager.
    if (gCloudConnectInFlight.load(std::memory_order_acquire)) return;
    networkManager.draw(w, h);
}

// --- Remote pointer broadcast -----------------------------------------------

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
    // Send the cursor in the plate's IMAGE space (quadID, image-x, image-y, scale;
    // getCursorPositionIn2DSpace packs scale into z) so the receiver can transform it.
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

// Notes dock: one gfcReview per piece of footage (mediaPath is the identity),
// not per plate; gfcNote::quadID picks out which plate's markup a note is.
namespace {

// gfcReview has a destructor (no implicit move) and unique_ptr notes, so it is
// neither copyable nor movable and std::vector<gfcReview> can't grow.
std::vector<std::unique_ptr<gfcReview>> g_noteReviews;

// Finds the in-memory review for `normalisedPath`, loading its sidecar on first
// touch; one with no sidecar still has the right identity and zero revisions.
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
    // Gate on the MEDIA PATH, not a loaded preview frame (a Load-Sequence-Manager
    // concept; Quick Load, drag-drop and --open-file plates may have none).
    if (seq->filenameGeneric.empty()) return nullptr;
    return &reviewForPath(gfcNoteStore::normalisePath(seq->filenameGeneric));
}

// Store -> renderer. gfcPlate::setNotes takes BORROWED pointers, which point
// into g_noteReviews (lives for the session, only mutated here, never mid-frame).

// Mouse-drawing state, declared here rather than beside the drawing code
// because the list published below includes the stroke being drawn.
bool g_noteDrawArmed = false;
int  g_drawPlate = -1;
std::unique_ptr<gfcNote> g_drawNote;

/** Points collected so far; for arrow/box only the first and last matter. */
std::vector<gfcNotePoint> g_drawPoints;

/** Copies published by setPlateNotesToRound(), keyed by plate index. Kept per
    plate: gfcPlate::setNotes() borrows pointers until THAT plate is
    republished, so freeing another plate's copies would leave it dangling. */
std::map<int, std::vector<std::unique_ptr<gfcNote>>> g_roundNoteCopies;

void syncPlateNotesImpl() {
    for (int i = 0; i < plateManager.plateCount(); ++i) {
        std::vector<const gfcNote*> out;
        if (gfcReview* review = reviewForPlate(i)) {
            // Every revision, locked or not: a locked round stays VISIBLE.
            for (const auto& revision : review->revisions)
                for (const auto& n : revision.notes)
                    if (n) out.push_back(n.get());
        }
        // The stroke being drawn right now (live feedback). Borrowed like the rest:
        // every path that destroys g_drawNote re-syncs before returning.
        if (g_drawNote && g_drawPlate == i) out.push_back(g_drawNote.get());
        plateManager.setPlateNotes(i, out);
    }
    // Every plate now borrows the real notes again, so the round copies a
    // summary thumbnail was drawing can go.
    g_roundNoteCopies.clear();
}

bool applyInboundNoteSyncEvents() {
    bool applied = false;
    for (auto& ev : networkManager.drainNoteSyncEvents()) {
        using Kind = jefe::net::NoteSyncEvent::Kind;
        if (ev.kind == Kind::Add && ev.note) {
            gfcReview* review = reviewForPlate(ev.note->quadID);
            if (!review) continue;

            // Idempotent by note id: the server relays note-adds to ALL peers, including
            // the drawer, so it would be stored twice. An existing note adopts the server-stamped author.
            gfcNote* existing = nullptr;
            for (auto& revision : review->revisions) {
                for (auto& n : revision.notes) {
                    if (n && n->id == ev.note->id) { existing = n.get(); break; }
                }
                if (existing) break;
            }
            if (existing) {
                if (existing->author != ev.note->author) {
                    existing->author = ev.note->author;
                    gfcNoteStore::save(*review);
                    applied = true;
                }
                continue;
            }

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

// Drawing-tool selection state, plain file-scope state so a viewport hook has
// one accessor to read instead of reaching into gfcPlate.
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
            // This review may hold notes for a sibling plate showing the same footage
            // (gfcNote::quadID); only this plate's markup belongs in its dock.
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

std::vector<SessionMedia> getSessionMediaSet() {
    std::vector<SessionMedia> out;
    auto add = [&out](const std::string& key, const std::string& framePath,
                      int track, int item, int itemTrack) {
        if (key.empty()) return;
        const std::string mediaPath = gfcNoteStore::normalisePath(key);
        for (const SessionMedia& m : out) {
            if (m.mediaPath == mediaPath) return;
        }
        SessionMedia m;
        m.mediaPath = mediaPath;
        m.anyFramePath = framePath.empty() ? key : framePath;
        m.track = track;
        m.playlistItem = item;
        m.playlistTrack = itemTrack;
        out.push_back(m);
    };
    for (int t = 0; t < GFC_MAX_SEQUENCES; ++t) {
        gfcSequence* seq = trackManager.getSequence(t);
        if (!seq || !seq->myGUI) continue;
        const std::string gui = seq->myGUI->getFilename();
        // Prefer the generic pattern; fall back to the one real frame path when the
        // sequence isn't scanned yet (filenameGeneric empty); normalisePath() maps both to one key.
        add(seq->filenameGeneric.empty() ? gui : seq->filenameGeneric, gui, t, -1, -1);
    }
    if (auto* entries = playlistManager.getPlaylist()) {
        for (int i = 0; i < (int)entries->size(); ++i) {
            const gfcPlaylistItem& item = (*entries)[i];
            for (size_t k = 0; k < item.loadParams.size(); ++k) {
                add(item.loadParams[k].fileName, item.loadParams[k].fileName, -1, i, (int)k);
            }
        }
    }
    return out;
}

gfcReviewSummary::Doc buildReviewSummary(const std::vector<SessionMedia>& media,
                                         const std::string& title) {
    gfcReviewSummary::Doc doc;
    doc.title = title;
    doc.exportedAt = time(nullptr);
    doc.appVersion = JEFE_VERSION;
    for (const SessionMedia& m : media) {
        gfcReview& review = reviewForPath(m.mediaPath);
        gfcReviewSummary::Media entry = gfcReviewSummary::fromReview(review);
        if (review.revisions.empty()) {
            // No rounds: either there is no sidecar, or it exists and did not parse.
            gfcReview probe;
            probe.mediaPath = m.mediaPath;
            std::error_code ec;
            if (!gfcNoteStore::load(m.mediaPath, probe) &&
                std::filesystem::exists(gfcNoteStore::sidecarPathFor(m.mediaPath), ec)) {
                entry.notesReadable = false;
            }
        }
        doc.media.push_back(std::move(entry));
    }
    return doc;
}

int plateShowingTrack(int track) {
    if (track < 0) return -1;
    for (int i = 0; i < plateManager.plateCount(); ++i) {
        if (plateManager.getTrackOnPlate(i) == track) return i;
    }
    return -1;
}

namespace {
// A note's copy drawn on `plateIdx`. The overlay only draws notes whose quadID
// matches the plate, and a round's notes may have been drawn on another plate.
std::unique_ptr<gfcNote> copyNoteForPlate(const gfcNote& n, int plateIdx) {
    std::unique_ptr<gfcNote> c;
    if (const auto* s = dynamic_cast<const gfcNoteStroke*>(&n)) {
        auto x = std::make_unique<gfcNoteStroke>();
        x->pts = s->pts;
        c = std::move(x);
    } else if (const auto* a = dynamic_cast<const gfcNoteArrow*>(&n)) {
        auto x = std::make_unique<gfcNoteArrow>();
        x->tail = a->tail;
        x->head = a->head;
        c = std::move(x);
    } else if (const auto* b = dynamic_cast<const gfcNoteBox*>(&n)) {
        auto x = std::make_unique<gfcNoteBox>();
        x->a = b->a;
        x->b = b->b;
        c = std::move(x);
    } else if (const auto* t = dynamic_cast<const gfcNoteText*>(&n)) {
        auto x = std::make_unique<gfcNoteText>();
        x->anchor = t->anchor;
        x->text = t->text;
        c = std::move(x);
    } else {
        return nullptr;
    }
    c->id = n.id;
    c->author = n.author;
    c->name = n.name;
    c->quadID = plateIdx;
    c->from = n.from;
    c->to = n.to;
    c->always = n.always;
    c->colorR = n.colorR;
    c->colorG = n.colorG;
    c->colorB = n.colorB;
    c->size = n.size;
    return c;
}
}  // namespace

bool setPlateNotesToRound(int plateIdx, const std::string& mediaPath, int roundIndex) {
    if (plateIdx < 0 || plateIdx >= plateManager.plateCount()) return false;
    gfcReview& review = reviewForPath(mediaPath);
    if (roundIndex < 0 || roundIndex >= (int)review.revisions.size()) return false;
    std::vector<std::unique_ptr<gfcNote>> copies;
    std::vector<const gfcNote*> borrowed;
    for (const auto& n : review.revisions[roundIndex].notes) {
        if (!n) continue;
        if (auto c = copyNoteForPlate(*n, plateIdx)) {
            borrowed.push_back(c.get());
            copies.push_back(std::move(c));
        }
    }
    // Publish the new list before freeing this plate's previous copies, and only
    // this plate's entry (a sibling's borrowed copies stay).
    plateManager.setPlateNotes(plateIdx, borrowed);
    g_roundNoteCopies[plateIdx] = std::move(copies);
    return true;
}

bool prepareTrackForRender(int track) {
    gfcSequence* seq = trackManager.getSequence(track);
    if (!seq) return false;
    if (seq->getNumFrames() <= 0) trackManager.startLoadingSequence(track);
    return seq->getNumFrames() > 0;
}

bool isTrackFrameReady(int track, int frame) {
    if (track < 0 || track >= GFC_MAX_SEQUENCES) return false;
    gfcSequence* seq = trackManager.getSequence(track);
    return seq && seq->frameReadyForRender(frame);
}

bool restartTrackLoadAtFrame(int track, int frame, bool allowAnnounce) {
    if (track < 0 || track >= GFC_MAX_SEQUENCES) return false;
    gfcSequence* seq = trackManager.getSequence(track);
    // Every frame of a loaded track can be force-loaded, so nothing restarts; a frame outside
    // the track never can, so the caller doesn't wait for it.
    (void)allowAnnounce;
    return seq && !seq->isEmpty() && seq->frameReadyForRender(frame);
}

int evictTrackFrames(int track) {
    if (track < 0 || track >= GFC_MAX_SEQUENCES) return 0;
    gfcSequence* seq = trackManager.getSequence(track);
    if (!seq) return 0;
    int freed = 0;
    for (int i = 0; i < seq->frameTable().size(); ++i) {
        if (seq->frameTable().state(i) != gfcFrameState::Resident) continue;
        seq->evictFrame(i);
        ++freed;
    }
    return freed;
}

void restorePlaylistSelection(int index, bool contentFromPlaylist) {
    // gfcPlaylistManager::setSelectedItem would broadcast the selection, so
    // set the same fields it does, directly.
    auto* entries = playlistManager.getPlaylist();
    const int count = entries ? (int)entries->size() : 0;
    const int selected = (index >= 0 && index < count) ? index : -1;
    for (int i = 0; i < count; ++i) (*entries)[i].selected = (i == selected) ? 1 : 0;
    playlistManager.selectedItem = selected;
    gCurrentContentFromPlaylist = contentFromPlaylist;
}

void setRemoteBroadcastsMuted(bool muted) {
    networkManager.setTakeNotifications(!muted);
}

std::vector<std::string> listSequenceFrames(const std::string& anyFramePath) {
    std::vector<std::string> files;
    std::string label;
    int startNum = 0;
    int endNum = 0;
    ::findSequence(files, anyFramePath, label, startNum, endNum);
    // A movie is one file; findSequence pads it to frameCount copies.
    if (gfcIsMovieFile(anyFramePath) && files.size() > 1) files.resize(1);
    return files;
}

std::string reviewXmlForMedia(const std::string& mediaPath) {
    return gfcNoteStore::toXmlString(reviewForPath(mediaPath));
}

std::string reviewFingerprint(const std::string& mediaPath) {
    return reviewForPath(mediaPath).fingerprint;
}

bool setReviewFingerprint(const std::string& mediaPath, const std::string& fingerprint) {
    gfcReview& review = reviewForPath(mediaPath);
    review.fingerprint = fingerprint;
    return gfcNoteStore::save(review);
}

std::string lutSourcePath(const std::string& lutName) {
    const int index = lutManager.getLutIndexByName(lutName);
    if (index < 0) return {};
    return std::string(lutManager.getLUT(index).filename);
}

LutLoadOutcome loadLUTFileReportingConflict(const std::string& path) {
    if (path.empty()) return LutLoadOutcome::Failed;
    // gfcLUTManager::loadLUT keeps the first LUT of a given file name and
    // silently ignores the rest, so a clash has to be found before calling it.
    const std::string loadedSource = lutSourcePath(std::filesystem::path(path).filename().string());
    if (!loadedSource.empty()) {
        auto readAll = [](const std::string& file, std::string& out) {
            std::ifstream in(file, std::ios::binary);
            if (!in) return false;
            out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return !in.bad();
        };
        std::string loadedBytes, newBytes;
        if (!readAll(loadedSource, loadedBytes)) return LutLoadOutcome::DifferentAlreadyLoaded;
        if (!readAll(path, newBytes)) return LutLoadOutcome::Failed;
        return loadedBytes == newBytes ? LutLoadOutcome::SameAlreadyLoaded : LutLoadOutcome::DifferentAlreadyLoaded;
    }
    const size_t before = lutManager.getAllNames().size();
    lutManager.loadLUT(path);
    return lutManager.getAllNames().size() > before ? LutLoadOutcome::Loaded : LutLoadOutcome::Failed;
}

bool isInstallLutPath(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string file = fs::weakly_canonical(fs::path(path), ec).string();
    if (ec) return false;
    std::vector<std::string> dirs;
    if (!sett.lutPath.empty()) dirs.push_back(sett.lutPath);
    dirs.push_back(::getApplicationDataPath() + "FX/");
    dirs.push_back("FX/");
    for (const std::string& d : dirs) {
        std::error_code dec;
        std::string dir = fs::weakly_canonical(fs::path(d), dec).string();
        if (dec || dir.empty()) continue;
        if (dir.back() != '/') dir += '/';
        if (file.compare(0, dir.size(), dir) == 0) return true;
    }
    return false;
}

void reloadReviewFromDisk(const std::string& mediaPath) {
    // Plates borrow pointers into the review: move it out, republish (reloads the
    // sidecar for any plate showing this media), then let the old copy go.
    std::vector<std::unique_ptr<gfcReview>> stale;
    for (auto it = g_noteReviews.begin(); it != g_noteReviews.end();) {
        if ((*it)->mediaPath == mediaPath) {
            stale.push_back(std::move(*it));
            it = g_noteReviews.erase(it);
        } else {
            ++it;
        }
    }
    syncPlateNotesImpl();
}

std::vector<std::string> getSearchPaths() { return sett.searchPaths; }

bool getSearchPathsRecursive() { return sett.searchPathsRecursive; }

void setSearchPaths(const std::vector<std::string>& paths, bool recursive, bool enabled) {
    sett.searchPaths = paths;
    sett.searchPathsRecursive = recursive;
    sett.useSearchPaths = enabled;
}

bool getRelinkByFingerprint() { return sett.relinkByFingerprint; }

void setRelinkByFingerprint(bool on) { sett.relinkByFingerprint = on; }

bool isFxLoaded(const std::string& fxName) {
    return fxManager.getFXIndexByName(fxName) >= 0;
}

std::string appVersion() { return JEFE_VERSION; }

bool decodeMovieFrameRGB(const std::string& path, int64_t index, int& w, int& h,
                         std::vector<float>& rgb, std::string* err) {
    auto fail = [&](const std::string& why) { if (err) *err = why; return false; };
    gfcLoadParams p;
    p.fileName = path;
    p.movieFrameIndex = index;
    p.scale = 100;
    p.crop = false;
    p.compressed = GFC_16BPC;   // planes at full precision, like a 16-bit track
    gfcImageLoaderMovie loader;
    if (loader.load(p) != 0) return fail(loader.loadErrorString);
    const gfcGLFrameInfo info = loader.getFrameInfo();
    w = loader.sizeX;
    h = loader.sizeY;
    const size_t px = size_t(w) * size_t(h);
    rgb.resize(px * 3);
    if (info.planarYuv) {
        std::vector<float> rgba;
        if (!gfcYuvConvertToRGBAF(info, w, h, rgba) || rgba.size() < px * 4) return fail("colour conversion failed");
        for (size_t i = 0; i < px; ++i) {
            rgb[i * 3 + 0] = rgba[i * 4 + 0];
            rgb[i * 3 + 1] = rgba[i * 4 + 1];
            rgb[i * 3 + 2] = rgba[i * 4 + 2];
        }
        return true;
    }
    // Semi-planar/packed layouts are uploaded from this BGRA64 copy, so the
    // stamp takes the same pixels.
    const uint16_t* src16 = static_cast<const uint16_t*>(loader.getPixelPointer());
    if (!src16 || px == 0) return fail("no pixels decoded");
    for (size_t i = 0; i < px; ++i) {
        rgb[i * 3 + 0] = src16[i * 4 + 2] / 65535.0f;   // BGRA -> RGB
        rgb[i * 3 + 1] = src16[i * 4 + 1] / 65535.0f;
        rgb[i * 3 + 2] = src16[i * 4 + 0] / 65535.0f;
    }
    return true;
}

bool stampNotesIntoExr(int plateIdx, const std::string& outExr,
                       bool writeHeader, bool writeLayer,
                       NoteStampResult& result) {
    result = NoteStampResult{};
    auto fail = [&](const std::string& why) {
        result.error = why;
        return false;
    };

    // The header attribute and named layer only mean something in EXR; refuse
    // anything else rather than write a PNG that dropped both.
    std::string ext = outExr.size() >= 4 ? outExr.substr(outExr.size() - 4) : std::string();
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    if (ext != ".exr") return fail("the output must be an .exr file");

    gfcSequence* seq = sequenceForPlate(plateIdx);
    gfcReview* review = reviewForPlate(plateIdx);
    if (!seq || !review) return fail("no media is loaded on this plate");

    // Resolve the file the plate is DISPLAYING: framePathAt() (not getFilenameatFrame(),
    // which appends " - NOT IN RAM"), else the preview frame's path, else filenameGeneric.
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

    // A movie frame is decoded and stamped from pixels (the CPU twin of the GPU upload pass), so
    // the stamp carries what a 16-bit track shows.
    const bool movie = seq->isMovie();
    std::vector<float> movieRgb;
    if (movie) {
        const int64_t index = int64_t(frame) - 1 - int64_t(seq->frameOffset);
        std::string merr;
        if (!decodeMovieFrameRGB(result.sourcePath, index, result.width, result.height, movieRgb, &merr)) {
            return fail("cannot decode frame " + std::to_string(frame) + " of " + result.sourcePath +
                        (merr.empty() ? "" : ": " + merr));
        }
    } else if (!gfcNoteStamp::imageSize(result.sourcePath, &result.width, &result.height)) {
        return fail("cannot read " + result.sourcePath);
    }

    gfcNoteStamp::Options opt;
    opt.writeHeader = writeHeader;
    std::string err;
    if (writeLayer) {
        // Rasterised at the SOURCE image's size; notes are normalised to the source,
        // so plate pan, zoom and crop are not involved.
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

    const bool stamped = movie
        ? gfcNoteStamp::stampPixels(result.width, result.height, movieRgb.data(), outExr, *review, opt, &err)
        : gfcNoteStamp::stamp(result.sourcePath, outExr, *review, opt, &err);
    if (!stamped) {
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
    const bool hadStroke = g_drawNote != nullptr;
    g_drawNote.reset();
    g_drawPoints.clear();
    g_drawPlate = -1;
    // The plate may be holding a borrowed pointer to the stroke just
    // destroyed; republish before anything can paint.
    if (hadStroke) syncPlateNotesImpl();
}

namespace {
// Writes g_drawPoints into the in-progress note's geometry; shared by the
// pencil (noteDrawAppend) and the scripting hook (demoNoteAppend).
void applyDrawPoints() {
    if (!g_drawNote || g_drawPoints.empty()) return;
    if (auto* stroke = dynamic_cast<gfcNoteStroke*>(g_drawNote.get()))
        stroke->pts = g_drawPoints;
    else if (auto* arrow = dynamic_cast<gfcNoteArrow*>(g_drawNote.get()))
        { arrow->tail = g_drawPoints.front(); arrow->head = g_drawPoints.back(); }
    else if (auto* box = dynamic_cast<gfcNoteBox*>(g_drawNote.get()))
        { box->a = g_drawPoints.front(); box->b = g_drawPoints.back(); }
    else if (auto* text = dynamic_cast<gfcNoteText*>(g_drawNote.get()))
        text->anchor = g_drawPoints.back();   // dragging a text note repositions it
}
}  // namespace

bool noteDrawBegin(int xFb, int yFb, int plateIdx) {
    noteDrawCancel();
    if (!g_noteDrawArmed || plateIdx < 0) return false;

    gfcReview* review = reviewForPlate(plateIdx);
    if (!review) return false;
    // Refuse before the first point rather than at commit: dropping a whole
    // drawn stroke silently is worse.
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
    // A text note is placed where it is clicked (otherwise its anchor stays at
    // the default (0,0), the top-left corner).
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
    syncPlateNotesImpl();   // show the first point immediately
    return true;
}

void noteDrawAppend(int xFb, int yFb) {
    if (!g_drawNote || g_drawPlate < 0) return;
    float nx = 0.0f, ny = 0.0f;
    // A drag that leaves the image keeps the last point inside it rather than
    // ending the note.
    if (!plateManager.cursorToNormalisedImage(xFb, yFb, g_drawPlate, nx, ny)) return;
    g_drawPoints.push_back(gfcNotePoint{nx, ny});
    applyDrawPoints();
    // Live feedback, for real now: republish so the plate draws the stroke as
    // it grows. The note list is a handful of pointers, so this is cheap.
    syncPlateNotesImpl();
}

bool noteDrawIsText() {
    return g_drawNote && g_drawNote->noteType() == GFCNOTE_TEXT;
}

void noteDrawSetText(const std::string& text) {
    if (auto* t = dynamic_cast<gfcNoteText*>(g_drawNote.get()))
        t->text = text;
}

bool demoNoteBegin(int plateIdx, int tool, float nx, float ny,
                   float r, float g, float b, int size) {
    noteDrawCancel();
    if (plateIdx < 0) return false;

    gfcReview* review = reviewForPlate(plateIdx);
    if (!review) return false;
    gfcRevision* open = review->openRevision();
    if (open && open->locked) return false;

    switch (tool) {
        case NOTETOOL_ARROW: g_drawNote = std::make_unique<gfcNoteArrow>(); break;
        case NOTETOOL_BOX:   g_drawNote = std::make_unique<gfcNoteBox>();   break;
        case NOTETOOL_TEXT:  g_drawNote = std::make_unique<gfcNoteText>();  break;
        default:             g_drawNote = std::make_unique<gfcNoteStroke>(); break;
    }
    g_drawPlate = plateIdx;
    g_drawPoints.assign(1, gfcNotePoint{nx, ny});
    applyDrawPoints();

    g_drawNote->quadID = plateIdx;
    g_drawNote->author = localAuthorName();
    const int f = getCurrentFrame();
    g_drawNote->from = f;
    g_drawNote->to   = f;
    g_drawNote->colorR = r;
    g_drawNote->colorG = g;
    g_drawNote->colorB = b;
    g_drawNote->size = size;
    syncPlateNotesImpl();
    return true;
}

void demoNoteAppend(float nx, float ny) {
    if (!g_drawNote || g_drawPlate < 0) return;
    g_drawPoints.push_back(gfcNotePoint{nx, ny});
    applyDrawPoints();
    syncPlateNotesImpl();
}

bool noteDrawEnd() {
    if (!g_drawNote || g_drawPlate < 0) { noteDrawCancel(); return false; }

    // A click with no drag is a zero-area arrow or box; reject the two-point
    // tools (a single-point freehand is a legitimate dot).
    const bool needsDrag = g_drawNote->noteType() == GFCNOTE_ARROW ||
                           g_drawNote->noteType() == GFCNOTE_BOX;
    if (needsDrag && g_drawPoints.size() < 2) { noteDrawCancel(); return false; }

    // A text note with no words renders nothing: refuse it here as well as in
    // the viewport's prompt.
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
    }
    g_drawPoints.clear();
    g_drawPlate = -1;
    g_drawNote.reset();
    // Unconditionally: the plate borrows this stroke's pointer, and a refused
    // commit destroyed the note inside addNote(), so republish on failure too.
    syncPlateNotesImpl();
    return added;
}
void toggleNotesVisible() { g_notesVisible = !g_notesVisible; }

int runUploadBench(const std::string& dir) {
    namespace fs = std::filesystem;
    using clock = std::chrono::steady_clock;
    std::vector<std::string> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.is_regular_file()) files.push_back(e.path().string());
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::printf("UPLOAD-BENCH FAIL no files in %s\n", dir.c_str());
        return 2;
    }

    // IOSurface textures (macOS): check the layouts round-trip, then report per mode which path the
    // frames took. JEFECHECK_NO_IOSURFACE=1 times the glTexImage2D path instead.
    const int surfaceLayouts = gfcSurface::selfTest();

    struct Mode { const char* name; char value; };
    const Mode modes[] = {{"8bit", GFC_8BPC}, {"16bit", GFC_16BPC}, {"half", GFC_16HALF}};
    float halfMax = -1.0f;
    // Per mode, the first frame's mean red and blue from texture and thumbnail; every depth must
    // agree with 8-bit, which catches a swapped R/B order.
    struct Means { double texR = 0, texB = 0, thumbR = 0, thumbB = 0; };
    std::vector<std::pair<const char*, Means>> means;
    bool lastFramesOk = true;
    long handedOff = 0, doubleHeld = 0;   // frames waiting with pixels in two places
    for (const Mode& mode : modes) {
        std::vector<double> decodeMs, uploadMs;
        double bytes = 0;
        const long boundBefore = gfcSurface::boundCount();
        const gfcSurface::PoolStats poolBefore = gfcSurface::poolStats();
        const gfcUpload::Stats uploadBefore = gfcUpload::stats();
        for (size_t i = 0; i < files.size(); ++i) {
            gfcFrame frame;
            gfcLoadParams params;
            params.fileName = files[i];
            params.compressed = mode.value;
            const auto t0 = clock::now();
            if (frame.loadFrame(params) != 0) {
                std::printf("UPLOAD-BENCH FAIL cannot load %s\n", files[i].c_str());
                return 2;
            }
            const auto t1 = clock::now();
            {
                const gfcGLFrameInfo fi = frame.theImageLoader->getFrameInfo();
                if (fi.ioSurface || fi.staging) {
                    ++handedOff;
                    if (frame.theImageLoader->getPixelPointer()) ++doubleHeld;
                }
            }
            glFinish();
            const auto t2 = clock::now();
            frame.generateTexture(i == 0);   // thumbnail on the first frame only
            glFinish();   // the upload is only done when the GPU has the texture
            const auto t3 = clock::now();
            decodeMs.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            uploadMs.push_back(std::chrono::duration<double, std::milli>(t3 - t2).count());
            bytes += double(frame.sizeX) * frame.sizeY * 4 * (mode.value == GFC_8BPC ? 1 : 2);

            // Read the texture back as float (what shaders see). A mode's first frame gets a fresh texture
            // and its last comes through the pools, so both are checked.
            auto readTexture = [&frame](float& maxV, double& r, double& b) {
                std::vector<float> px(size_t(frame.sizeX) * frame.sizeY * 4);
                if (frame.ioSurface) {
                    gfcSurface::readBack(frame.textureID, frame.sizeX, frame.sizeY, px.data());
                } else {
                    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, frame.textureID);
                    glGetTexImage(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, GL_FLOAT, px.data());
                }
                maxV = 0;
                r = b = 0;
                for (size_t k = 0; k < px.size(); k += 4) {
                    maxV = std::max({maxV, px[k], px[k + 1], px[k + 2]});
                    r += std::clamp(px[k], 0.0f, 1.0f);
                    b += std::clamp(px[k + 2], 0.0f, 1.0f);
                }
                r /= double(px.size() / 4);
                b /= double(px.size() / 4);
            };
            if (i == 0) {
                float maxV = 0;
                Means m;
                readTexture(maxV, m.texR, m.texB);
                const auto& th = frame.thumbnail.rgba;
                for (size_t k = 0; k + 3 < th.size(); k += 4) {
                    m.thumbR += th[k] / 255.0;
                    m.thumbB += th[k + 2] / 255.0;
                }
                if (!th.empty()) {
                    m.thumbR /= double(th.size() / 4);
                    m.thumbB /= double(th.size() / 4);
                }
                means.push_back({mode.name, m});
                std::printf("UPLOAD-BENCH %-5s %dx%d texture max=%.3f\n", mode.name, frame.sizeX, frame.sizeY,
                            maxV);
                if (mode.value == GFC_16HALF) halfMax = maxV;
            } else if (i + 1 == files.size()) {
                float maxV = 0;
                double r = 0, b = 0;
                readTexture(maxV, r, b);
                const Means& first = means.back().second;
                const bool same = std::abs(r - first.texR) < 0.01 && std::abs(b - first.texB) < 0.01
                                  && (mode.value != GFC_16HALF || maxV > 1.0f);
                std::printf("UPLOAD-BENCH %s %-5s last frame matches the first (R=%.3f B=%.3f max=%.3f)\n",
                            same ? "ok  " : "FAIL", mode.name, r, b, maxV);
                lastFramesOk = lastFramesOk && same;
            }
            frame.deleteTexture();
        }
        auto median = [](std::vector<double> v) {
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        double uploadTotal = 0;
        for (double ms : uploadMs) uploadTotal += ms;
        const long bound = gfcSurface::boundCount() - boundBefore;
        const gfcSurface::PoolStats pool = gfcSurface::poolStats();
        std::printf("UPLOAD-BENCH %-5s frames=%zu decode median=%.1fms upload median=%.1fms upload=%.0f MB/s"
                    " iosurface=%ld/%zu surfaces new=%ld reused=%ld\n",
                    mode.name, files.size(), median(decodeMs), median(uploadMs),
                    uploadTotal > 0 ? bytes / 1e6 / (uploadTotal / 1000.0) : 0.0, bound, files.size(),
                    pool.created - poolBefore.created, pool.reused - poolBefore.reused);
        const gfcUpload::Stats up = gfcUpload::stats();
        std::printf("UPLOAD-BENCH %-5s staging=%s staged=%ld/%zu misses=%ld textures reused=%ld new=%ld\n",
                    mode.name, up.level, up.stagedUploads - uploadBefore.stagedUploads, files.size(),
                    up.stagingMisses - uploadBefore.stagingMisses, up.texturesReused - uploadBefore.texturesReused,
                    up.texturesCreated - uploadBefore.texturesCreated);
    }
    bool orderOk = !means.empty();
    for (const auto& [name, m] : means) {
        const Means& ref = means.front().second;   // 8-bit
        const bool ok = std::abs(m.texR - ref.texR) < 0.02 && std::abs(m.texB - ref.texB) < 0.02 &&
                        std::abs(m.thumbR - ref.thumbR) < 0.03 && std::abs(m.thumbB - ref.thumbB) < 0.03;
        std::printf("UPLOAD-BENCH %s %-5s matches 8-bit colour (texture R=%.3f B=%.3f, thumbnail R=%.3f B=%.3f)\n",
                    ok ? "ok  " : "FAIL", name, m.texR, m.texB, m.thumbR, m.thumbB);
        orderOk = orderOk && ok;
    }
    const bool singleCopy = doubleHeld == 0;
    std::printf("UPLOAD-BENCH %s frames handed to a surface or staging buffer keep no CPU copy (%ld of %ld did)\n",
                singleCopy ? "ok  " : "FAIL", doubleHeld, handedOff);

    // A ticket is good for one use: abandoning a stale one must not free the
    // buffer it once named after that buffer went to another acquire().
    bool ticketsOk = true;
    if (std::strcmp(gfcUpload::stats().level, "off") != 0) {
        constexpr size_t kProbeBytes = 4096 * 3;   // no frame uses this size
        void* data = nullptr;
        gfcUpload::Ticket first = gfcUpload::acquire(kProbeBytes, &data);
        if (!first) { gfcUpload::service(); first = gfcUpload::acquire(kProbeBytes, &data); }
        gfcUpload::abandon(first);
        const gfcUpload::Ticket second = gfcUpload::acquire(kProbeBytes, &data);
        gfcUpload::abandon(first);   // stale: must leave `second` holding the buffer
        const gfcUpload::Ticket third = gfcUpload::acquire(kProbeBytes, &data);
        ticketsOk = first && second && second != first && !third;
        gfcUpload::abandon(third);
        gfcUpload::abandon(second);
        std::printf("UPLOAD-BENCH %s a stale staging ticket cannot release a buffer someone else holds\n",
                    ticketsOk ? "ok  " : "FAIL");
    }

    // Idle pools give their memory back.
    gfcUpload::trimIdle(0.0);
    gfcSurface::trimIdle(0.0);
    const gfcUpload::Stats afterTrim = gfcUpload::stats();
    const size_t surfaceBytes = gfcSurface::poolStats().pooledBytes;
    const bool trimOk = afterTrim.stagingBytes == 0 && afterTrim.pooledTextureBytes == 0 && surfaceBytes == 0;
    std::printf("UPLOAD-BENCH %s idle pools are freed (staging %zu, textures %zu, surfaces %zu bytes left)\n",
                trimOk ? "ok  " : "FAIL", afterTrim.stagingBytes, afterTrim.pooledTextureBytes, surfaceBytes);

    const bool surfacesOk = !gfcSurface::available() || surfaceLayouts == 3;
    std::printf("UPLOAD-BENCH %s IOSurface layouts working: %d/3%s\n", surfacesOk ? "ok  " : "FAIL",
                surfaceLayouts, gfcSurface::available() ? "" : " (disabled)");
    std::printf("UPLOAD-BENCH %s half-float textures keep values above 1.0 (max=%.3f)\n",
                halfMax > 1.0f ? "ok  " : "FAIL", halfMax);
    const bool pass = halfMax > 1.0f && orderOk && surfacesOk && lastFramesOk
                      && singleCopy && ticketsOk && trimOk;
    std::printf("UPLOAD-BENCH: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 2;
}

}  // namespace jefe::qt

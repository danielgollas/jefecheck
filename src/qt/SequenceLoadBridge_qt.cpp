// Implementation lives in a Qt-free TU so it can pull the manager
// headers (which include glad) without colliding with Qt's QOpenGLWidget
// system-OpenGL include path.
#include "SequenceLoadBridge_qt.h"

#include "../gfcplatemanager.h"
#include "../gfcplaybackmanager.h"
#include "../gfctrackmanager.h"
#include "../gfclutmanager.h"
#include "../gfcStructures.h"
#include "../gfcSequence.h"
#include "../gfcsequencegui.h"
#include "gfcplategui_qt.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

extern gfcPlateManager plateManager;
extern gfcPlaybackManager playbackManager;
extern gfcTrackManager trackManager;
extern gfcLUTManager lutManager;
extern gfcSettings sett;

namespace jefe::qt {

void initializeRenderingChain() {
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
    }
    fprintf(stderr, "[jefecheck] Loaded %zu LUT file(s) from %s\n",
            files.size(), path.c_str());
    fflush(stderr);
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

bool loadFileIntoPlate(const std::string& path,
                       int whichSequence,
                       bool kickOffSequenceLoad) {
    auto* seq = trackManager.getSequence(whichSequence);
    if (!seq || !seq->myGUI || path.empty()) {
        return false;
    }
    seq->myGUI->setFilename(path);

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

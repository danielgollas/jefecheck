// Implementation lives in a Qt-free TU so it can pull the manager
// headers (which include glad) without colliding with Qt's QOpenGLWidget
// system-OpenGL include path.
#include "SequenceLoadBridge_qt.h"

#include "../gfcplatemanager.h"
#include "../gfcplaybackmanager.h"
#include "../gfctrackmanager.h"
#include "../gfcSequence.h"
#include "../gfcsequencegui.h"
#include "gfcplategui_qt.h"

extern gfcPlateManager plateManager;
extern gfcPlaybackManager playbackManager;
extern gfcTrackManager trackManager;

namespace jefe::qt {

void initializeRenderingChain() {
    plateManager.initializeWidgets();
    trackManager.initializeWidgets();
    playbackManager.initializeWidgets();
}

bool tickPlayback() {
    // playbackManager.update() is the heart of the playback engine —
    // advances currentFrame at target FPS, calls plateManager.setChanged()
    // when a new frame should display.
    playbackManager.update();

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

void panActivePlate(float dx, float dy) {
    const int q = plateManager.getActiveQuad();
    if (q < 0) return;
    plateManager.panPlate(q, dx, dy);
    plateManager.setChanged();
}

void zoomActivePlate(float zoomDelta) {
    const int q = plateManager.getActiveQuad();
    if (q < 0) return;
    plateManager.zoomPlate(q, zoomDelta);
    plateManager.setChanged();
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

bool loadFileIntoPlate(const std::string& path, int whichSequence) {
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
    return true;
}

}  // namespace jefe::qt

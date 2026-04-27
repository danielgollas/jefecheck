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

// Walks the install-time LUT path (sett.lutPath, falling back to
// <Resources>/FX/, then ./FX/) and loads every .lut/.cube/.cub/.tga
// via lutManager. Each load may call glGenTextures, so the caller
// MUST make the GL context current before invoking. Called from
// MainWindow_Qt once the QOpenGLWidget is alive.
void initializeInstallLUTs();

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
bool loadFileIntoPlate(const std::string& path,
                       int whichSequence,
                       bool kickOffSequenceLoad = true);

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

// Playback control. Only pause is wired today; oneFrameFwd / Rev
// are no-ops until the Qt build runs the playback manager loop, but
// stubbing them here lets the keyboard handler stay symmetric with
// the FLTK reference.
void pausePlayback();
void stepFrame(int direction);  // -1 reverse, +1 forward

}  // namespace jefe::qt

#endif

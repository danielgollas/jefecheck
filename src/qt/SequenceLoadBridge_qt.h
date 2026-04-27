// Tiny shim between the Qt UI and the rendering chain's manager
// singletons. Lives in its own TU because the manager headers pull glad,
// and Qt's QOpenGLWidget headers pull system OpenGL — the two refuse to
// share a translation unit on macOS. This file pulls glad; the .cpp pulls
// the chain headers and stays Qt-free.
#ifndef JEFECHECK_QT_SEQUENCE_LOAD_BRIDGE_H
#define JEFECHECK_QT_SEQUENCE_LOAD_BRIDGE_H

#include <string>

namespace jefe::qt {

// Wires up gfcPlateGUI_Qt and gfcSequenceGUI_Qt instances on every
// plate / sequence and constructs the gfcPlateManagerGUI_Qt. Safe to
// call once at app startup; calling again would leak the previous GUIs.
void initializeRenderingChain();

// Loads the file into sequence `whichSequence` as a preview frame and
// flips the matching plate's GUI into showPreview mode so the rendering
// chain picks up the new frame on the next draw call. Caller is
// responsible for making the GL context current — generateTexture()
// uploads in the calling thread. Returns true on success.
bool loadFileIntoPlate(const std::string& path, int whichSequence);

// Pan / zoom hooks called from GlViewport_Qt's mouse handlers. They
// drive the active plate's transform through plateManager. dx/dy are
// the per-event delta in pixels; zoomDelta is the wheel scroll amount
// (positive zooms in). Both call plateManager.setChanged() so the next
// paintGL pass picks up the change.
void panActivePlate(float dx, float dy);
void zoomActivePlate(float zoomDelta);

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

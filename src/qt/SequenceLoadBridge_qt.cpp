// Implementation lives in a Qt-free TU so it can pull the manager
// headers (which include glad) without colliding with Qt's QOpenGLWidget
// system-OpenGL include path.
#include "SequenceLoadBridge_qt.h"

#include "../gfcplatemanager.h"
#include "../gfctrackmanager.h"
#include "../gfcSequence.h"
#include "../gfcsequencegui.h"

extern gfcPlateManager plateManager;
extern gfcTrackManager trackManager;

namespace jefe::qt {

void initializeRenderingChain() {
    plateManager.initializeWidgets();
    trackManager.initializeWidgets();
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

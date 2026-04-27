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
    return true;
}

}  // namespace jefe::qt

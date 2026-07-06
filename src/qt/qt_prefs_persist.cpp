// Implementation of the QSettings <-> gfcSettings `sett` bridge declared in
// qt_prefs_persist.h. Centralizes preference keys that used to be scattered
// across MainWindow_qt.cpp and PreferencesWindow_qt.cpp. Later tasks append
// their sections' keys to both functions as those sections come on-line.
#include "qt_prefs_persist.h"
#include "../gfcStructures.h"
#include <QSettings>
#include <string>

extern gfcSettings sett;

namespace jefe { namespace qt {

void loadPreferences() {
    QSettings s;
    // Engine (already persisted in MainWindow_qt today — centralize here).
    sett.defaultDecodeFilter  = s.value("Engine/defaultDecodeFilter",  sett.defaultDecodeFilter).toInt();
    sett.defaultTextureFormat = s.value("Engine/defaultTextureFormat", sett.defaultTextureFormat).toInt();
    // Session behavior.
    sett.startupSessionBehavior = s.value("Session/startupBehavior", sett.startupSessionBehavior).toInt();
    // NOTE: later tasks append their sections' keys here (General/*, Formats/*, ...).
}

void writePreferences() {
    QSettings s;
    s.setValue("Engine/defaultDecodeFilter",  sett.defaultDecodeFilter);
    s.setValue("Engine/defaultTextureFormat", sett.defaultTextureFormat);
    s.setValue("Session/startupBehavior",     sett.startupSessionBehavior);
    // NOTE: later tasks append their sections' keys here.
}

} }  // namespace jefe::qt

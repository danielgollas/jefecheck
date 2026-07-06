// Implementation of the QSettings <-> gfcSettings `sett` bridge declared in
// qt_prefs_persist.h. Centralizes preference keys that used to be scattered
// across MainWindow_qt.cpp and PreferencesWindow_qt.cpp. Later tasks append
// their sections' keys to both functions as those sections come on-line.
#include "qt_prefs_persist.h"
#include "../gfcStructures.h"
#include <QSettings>
#include <QString>

extern gfcSettings sett;

namespace jefe { namespace qt {

void loadPreferences() {
    QSettings s;
    // Engine (already persisted in MainWindow_qt today — centralize here).
    sett.defaultDecodeFilter  = s.value("Engine/defaultDecodeFilter",  sett.defaultDecodeFilter).toInt();
    sett.defaultTextureFormat = s.value("Engine/defaultTextureFormat", sett.defaultTextureFormat).toInt();
    // Session behavior.
    sett.startupSessionBehavior = s.value("Session/startupBehavior", sett.startupSessionBehavior).toInt();

    // General (JEF-16 Task 1).
    sett.bgColor        = s.value("General/bgColor", sett.bgColor).toFloat();
    sett.bgCheckerboard = s.value("General/bgCheckerboard", sett.bgCheckerboard).toInt();
    sett.defaultBrowsePath = s.value("General/defaultBrowsePath",
                                     QString::fromStdString(sett.defaultBrowsePath)).toString().toStdString();
    sett.startFullscreen = s.value("General/startFullscreen", sett.startFullscreen).toInt();
    sett.openLoadWindowAtStartup = s.value("General/openLoadWindowAtStartup", sett.openLoadWindowAtStartup).toInt();
    sett.showThumbnails  = s.value("General/showThumbnails", sett.showThumbnails).toBool();
    sett.feedbackMessageSize = s.value("General/feedbackMessageSize", sett.feedbackMessageSize).toInt();
    sett.feedbackMessageFadeDelay = s.value("General/feedbackMessageFadeDelay", sett.feedbackMessageFadeDelay).toFloat();
    // NOTE: later tasks append their sections' keys here (Formats/*, ...).
}

void writePreferences() {
    QSettings s;
    s.setValue("Engine/defaultDecodeFilter",  sett.defaultDecodeFilter);
    s.setValue("Engine/defaultTextureFormat", sett.defaultTextureFormat);
    s.setValue("Session/startupBehavior",     sett.startupSessionBehavior);

    // General (JEF-16 Task 1).
    s.setValue("General/bgColor",        sett.bgColor);
    s.setValue("General/bgCheckerboard", sett.bgCheckerboard);
    s.setValue("General/defaultBrowsePath",
               QString::fromStdString(sett.defaultBrowsePath));
    s.setValue("General/startFullscreen",           sett.startFullscreen);
    s.setValue("General/openLoadWindowAtStartup",   sett.openLoadWindowAtStartup);
    s.setValue("General/showThumbnails",            sett.showThumbnails);
    s.setValue("General/feedbackMessageSize",       sett.feedbackMessageSize);
    s.setValue("General/feedbackMessageFadeDelay",  sett.feedbackMessageFadeDelay);
    // NOTE: later tasks append their sections' keys here.
}

} }  // namespace jefe::qt

// Implementation of the QSettings <-> gfcSettings `sett` bridge declared in
// qt_prefs_persist.h. Centralizes preference keys that used to be scattered
// across MainWindow_qt.cpp and PreferencesWindow_qt.cpp. Later tasks append
// their sections' keys to both functions as those sections come on-line.
#include "qt_prefs_persist.h"
#include "../gfcStructures.h"
#include "../gfcTextRenderer.h"
#include <QSettings>
#include <QString>
#include <QStringList>

extern gfcSettings sett;

namespace jefe { namespace qt {

void loadPreferences() {
    QSettings s;
    // Engine (already persisted in MainWindow_qt today — centralize here).
    sett.defaultDecodeFilter  = s.value("Engine/defaultDecodeFilter",  sett.defaultDecodeFilter).toInt();
    sett.defaultTextureFormat = s.value("Engine/defaultTextureFormat", sett.defaultTextureFormat).toInt();
    // Engine (JEF-16 Task 2 — Playback & Engine section).
    sett.vsync                 = s.value("Engine/vsync",                 sett.vsync).toInt();
    sett.maximumFramesInQueue  = s.value("Engine/maximumFramesInQueue",  sett.maximumFramesInQueue).toInt();
    sett.numOfPartitions       = s.value("Engine/numOfPartitions",       sett.numOfPartitions).toInt();
    sett.balanceReads          = s.value("Engine/balanceReads",          sett.balanceReads).toInt();
    sett.forcePBO              = s.value("Engine/forcePBO",              sett.forcePBO).toInt();
    sett.renderingEngine       = s.value("Engine/renderingEngine",       sett.renderingEngine).toInt();
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

    // Formats (JEF-16 Task 3).
    sett.exrIgnoreDisplayWindow      = s.value("Formats/exrIgnoreDisplayWindow",      sett.exrIgnoreDisplayWindow).toInt();
    sett.exrIgnoreHeadersAspectRatio = s.value("Formats/exrIgnoreHeadersAspectRatio", sett.exrIgnoreHeadersAspectRatio).toInt();

    // Search Paths (JEF-16 Task 4).
    sett.useSearchPaths       = s.value("Search/useSearchPaths", sett.useSearchPaths).toBool();
    sett.searchPathsRecursive = s.value("Search/recursive",      sett.searchPathsRecursive).toBool();
    {
        QStringList defaultPaths;
        for (const auto& p : sett.searchPaths) defaultPaths << QString::fromStdString(p);
        const QStringList paths = s.value("Search/paths", defaultPaths).toStringList();
        sett.searchPaths.clear();
        for (const QString& p : paths) sett.searchPaths.push_back(p.toStdString());
    }
    // Remote (JEF-16 Task 5).
    sett.nickName = s.value("Remote/nickName",
                             QString::fromStdString(sett.nickName)).toString().toStdString();
    sett.chatFadeDelay            = s.value("Remote/chatFadeDelay",            sett.chatFadeDelay).toFloat();
    sett.chatAutoFade             = s.value("Remote/chatAutoFade",             sett.chatAutoFade).toInt();
    sett.chatTextBG               = s.value("Remote/chatTextBG",               sett.chatTextBG).toInt();
    sett.chatFontSize             = s.value("Remote/chatFontSize",             sett.chatFontSize).toInt();
    sett.chatOpacity              = s.value("Remote/chatOpacity",              sett.chatOpacity).toFloat();
    sett.chatDisplayLines         = s.value("Remote/chatDisplayLines",         sett.chatDisplayLines).toInt();
    sett.remotePointerFadeDelay   = s.value("Remote/remotePointerFadeDelay",   sett.remotePointerFadeDelay).toFloat();
    sett.remotePointerColor       = s.value("Remote/remotePointerColor",       sett.remotePointerColor).toInt();
    sett.sendRemoteLoadRequests   = s.value("Remote/sendRemoteLoadRequests",   sett.sendRemoteLoadRequests).toInt();
    sett.autoAcceptRemoteLoadRequests = s.value("Remote/autoAcceptRemoteLoadRequests",
                                                 sett.autoAcceptRemoteLoadRequests).toInt();

    // Text (JEF-16 Task 6) — GfcTextRenderer is a separate singleton, not
    // part of `sett`; applied via its own setters rather than assigned here.
    applyTextPrefs();

    // NOTE: later tasks append their sections' keys here.
}

void applyTextPrefs() {
    QSettings s;
    GfcTextRenderer& tr = textRenderer();

    // Fallbacks below mirror GfcTextRenderer's constructor defaults
    // (gfcTextRenderer.cpp) exactly, so a first run (no Text/* keys yet)
    // causes zero visual change.
    tr.setSize(s.value("Text/size", 14.0f).toFloat());
    tr.setColor(s.value("Text/colorR", 1.0f).toFloat(),
                s.value("Text/colorG", 1.0f).toFloat(),
                s.value("Text/colorB", 1.0f).toFloat(),
                s.value("Text/colorA", 1.0f).toFloat());
    tr.setShadowEnabled(s.value("Text/shadowEnabled", true).toBool());
    tr.setShadowOffset(s.value("Text/shadowOffX", 1.0f).toFloat(),
                        s.value("Text/shadowOffY", -1.0f).toFloat());
    tr.setShadowColor(s.value("Text/shadowColorR", 0.0f).toFloat(),
                       s.value("Text/shadowColorG", 0.0f).toFloat(),
                       s.value("Text/shadowColorB", 0.0f).toFloat(),
                       s.value("Text/shadowColorA", 0.5f).toFloat());
    tr.setShadowBlur(s.value("Text/shadowBlur", 0.0f).toFloat());
    tr.setHintMode(static_cast<GfcTextRenderer::HintMode>(
        s.value("Text/hintMode", int(GfcTextRenderer::HINT_LIGHT)).toInt()));
    tr.setFilterNearest(s.value("Text/filterNearest", true).toBool());
    tr.setGamma(s.value("Text/gamma", 0.65f).toFloat());
}

void writePreferences() {
    QSettings s;
    s.setValue("Engine/defaultDecodeFilter",  sett.defaultDecodeFilter);
    s.setValue("Engine/defaultTextureFormat", sett.defaultTextureFormat);
    s.setValue("Engine/vsync",                sett.vsync);
    s.setValue("Engine/maximumFramesInQueue", sett.maximumFramesInQueue);
    s.setValue("Engine/numOfPartitions",      sett.numOfPartitions);
    s.setValue("Engine/balanceReads",         sett.balanceReads);
    s.setValue("Engine/forcePBO",             sett.forcePBO);
    s.setValue("Engine/renderingEngine",      sett.renderingEngine);
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

    // Formats (JEF-16 Task 3).
    s.setValue("Formats/exrIgnoreDisplayWindow",      sett.exrIgnoreDisplayWindow);
    s.setValue("Formats/exrIgnoreHeadersAspectRatio", sett.exrIgnoreHeadersAspectRatio);

    // Search Paths (JEF-16 Task 4).
    s.setValue("Search/useSearchPaths", sett.useSearchPaths);
    s.setValue("Search/recursive",      sett.searchPathsRecursive);
    {
        QStringList paths;
        for (const auto& p : sett.searchPaths) paths << QString::fromStdString(p);
        s.setValue("Search/paths", paths);
    }
    // Remote (JEF-16 Task 5).
    s.setValue("Remote/nickName", QString::fromStdString(sett.nickName));
    s.setValue("Remote/chatFadeDelay",            sett.chatFadeDelay);
    s.setValue("Remote/chatAutoFade",             sett.chatAutoFade);
    s.setValue("Remote/chatTextBG",               sett.chatTextBG);
    s.setValue("Remote/chatFontSize",             sett.chatFontSize);
    s.setValue("Remote/chatOpacity",              sett.chatOpacity);
    s.setValue("Remote/chatDisplayLines",         sett.chatDisplayLines);
    s.setValue("Remote/remotePointerFadeDelay",   sett.remotePointerFadeDelay);
    s.setValue("Remote/remotePointerColor",       sett.remotePointerColor);
    s.setValue("Remote/sendRemoteLoadRequests",   sett.sendRemoteLoadRequests);
    s.setValue("Remote/autoAcceptRemoteLoadRequests", sett.autoAcceptRemoteLoadRequests);

    // NOTE: later tasks append their sections' keys here.
}

} }  // namespace jefe::qt

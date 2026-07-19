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

// Defined in gfcimageloaderoiio.cpp — sets OIIO's global thread pool. Forward-
// declared here so this TU doesn't need OIIO headers.
void gfcSetOIIOThreadCount(int n);

namespace jefe { namespace qt {

void loadPreferences() {
    QSettings s;
    // Engine (already persisted in MainWindow_qt today — centralize here).
    sett.defaultDecodeFilter  = s.value("Engine/defaultDecodeFilter",  sett.defaultDecodeFilter).toInt();
    sett.defaultTextureFormat = s.value("Engine/defaultTextureFormat", sett.defaultTextureFormat).toInt();
    // Engine (JEF-16 Task 2 — Playback & Engine section).
    // Only maximumFramesInQueue has a live effect and a UI control; the other
    // engine settings were hidden (JEF-16 audit: inert) or removed (balanceReads),
    // so they are no longer persisted.
    sett.maximumFramesInQueue  = s.value("Engine/maximumFramesInQueue",  sett.maximumFramesInQueue).toInt();
    sett.oiioThreads           = s.value("Engine/oiioThreads",           sett.oiioThreads).toInt();
    gfcSetOIIOThreadCount(sett.oiioThreads);   // apply at startup
    // Session behavior.
    sett.startupSessionBehavior = s.value("Session/startupBehavior", sett.startupSessionBehavior).toInt();

    // General (JEF-16 Task 1).
    // RGB background. Migrate from the legacy grayscale General/bgColor: if the
    // RGB keys are absent, each channel falls back to the old gray value.
    sett.bgColor        = s.value("General/bgColor", sett.bgColor).toFloat();
    sett.bgColorR       = s.value("General/bgColorR", sett.bgColor).toFloat();
    sett.bgColorG       = s.value("General/bgColorG", sett.bgColor).toFloat();
    sett.bgColorB       = s.value("General/bgColorB", sett.bgColor).toFloat();
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
    sett.oiioUnassociatedAlpha       = s.value("Formats/oiioUnassociatedAlpha",       sett.oiioUnassociatedAlpha).toInt();
    sett.applyExifOrientation        = s.value("Formats/applyExifOrientation",        sett.applyExifOrientation).toInt();

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

    // Text (JEF-16 Task 6) — GfcTextRenderer is a separate singleton, not
    // part of `sett`; applied via its own setters rather than assigned here.
    applyTextPrefs();

    // NOTE: later tasks append their sections' keys here.
}

void applyTextPrefs() {
    QSettings s;
    GfcTextRenderer& tr = textRenderer();

    // Fallbacks come from GfcTextDefaults (gfcTextRenderer.h), the single source
    // shared with buildTextPage and the renderer ctor, so a first run (no Text/*
    // keys) is a visual no-op and the sites can't drift.
    using namespace GfcTextDefaults;
    // Size + color drive the on-plate label overlay (gfcPlate::drawText) via the
    // renderer's label style — the visible knobs. (setSize/setColor here are
    // transient and get overwritten per-draw; the label setters are what stick.)
    const float textSize = s.value("Text/size", kLabelSize).toFloat();
    tr.setSize(textSize);
    tr.setLabelSize(textSize);
    const float cr = s.value("Text/colorR", kColorR).toFloat();
    const float cg = s.value("Text/colorG", kColorG).toFloat();
    const float cb = s.value("Text/colorB", kColorB).toFloat();
    tr.setColor(cr, cg, cb, 1.0f);   // label is opaque; text color has no alpha
    tr.setLabelColor(cr, cg, cb);
    tr.setShadowEnabled(s.value("Text/shadowEnabled", kShadowEnabled).toBool());
    tr.setShadowOffset(s.value("Text/shadowOffX", kShadowOffX).toFloat(),
                        s.value("Text/shadowOffY", kShadowOffY).toFloat());
    tr.setShadowColor(s.value("Text/shadowColorR", kShadowColorR).toFloat(),
                       s.value("Text/shadowColorG", kShadowColorG).toFloat(),
                       s.value("Text/shadowColorB", kShadowColorB).toFloat(),
                       s.value("Text/shadowColorA", kShadowColorA).toFloat());
    tr.setShadowBlur(s.value("Text/shadowBlur", kShadowBlur).toFloat());
    tr.setHintMode(static_cast<GfcTextRenderer::HintMode>(
        s.value("Text/hintMode", kHintMode).toInt()));
    tr.setFilterNearest(s.value("Text/filterNearest", kFilterNearest).toBool());
    tr.setGamma(s.value("Text/gamma", kGamma).toFloat());
}

void writePreferences() {
    QSettings s;
    s.setValue("Engine/defaultDecodeFilter",  sett.defaultDecodeFilter);
    s.setValue("Engine/defaultTextureFormat", sett.defaultTextureFormat);
    s.setValue("Engine/maximumFramesInQueue", sett.maximumFramesInQueue);
    s.setValue("Engine/oiioThreads",          sett.oiioThreads);
    gfcSetOIIOThreadCount(sett.oiioThreads);   // apply on Done
    s.setValue("Session/startupBehavior",     sett.startupSessionBehavior);

    // General (JEF-16 Task 1).
    s.setValue("General/bgColor",        sett.bgColor);   // luminance, for legacy readers
    s.setValue("General/bgColorR",       sett.bgColorR);
    s.setValue("General/bgColorG",       sett.bgColorG);
    s.setValue("General/bgColorB",       sett.bgColorB);
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
    s.setValue("Formats/oiioUnassociatedAlpha",       sett.oiioUnassociatedAlpha);
    s.setValue("Formats/applyExifOrientation",        sett.applyExifOrientation);

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

    // NOTE: later tasks append their sections' keys here.
}

} }  // namespace jefe::qt

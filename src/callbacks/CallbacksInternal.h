// Internal preamble shared by callback domain modules under src/callbacks/.
// Contains the includes and `extern` declarations that the original
// UICallbacks.cpp had at the top of the file, minus the global definitions
// (those still live in UICallbacks.cpp).
//
// Each domain module (PlaybackCallbacks.cpp, MenuCallbacks.cpp, etc.) starts
// with `#include "CallbacksInternal.h"` plus any module-specific extras.
#ifndef CALLBACKS_INTERNAL_H
#define CALLBACKS_INTERNAL_H

#include <glad/glad.h>
#include "UICallbacks.h"
#include "UIConstants.h"

#include "ui/IEventSystem.h"
#include "ui/IApplication.h"

#include <FL/Fl.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Color_Chooser.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Pixmap.H>
#include <FL/Fl_Menu.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Text_Buffer.H>

#include <stdio.h>
#include <math.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <string>
#include <vector>
#include <map>

#include "mainWindow.h"
#include "loadWindow.h"
#include "lutWindow.h"
#include "fxWindow.h"
#include "fxcontrolwindow.h"
#include "playlistwindow.h"
#include "preferencesWindow.h"
#include "renderWindow.h"
#include "remoteWindow.h"
#include "drawingToolsWindow.h"
#include "aboutWindow.h"
#include "exrWindow.h"
#include "moreOptionWindow.h"
#include "gammaWindow.h"
#include "minSpecsWindow.h"

#include "gfcSequence.h"
#include "gfcTextRenderer.h"
#include "gfcreview.h"
#include "gfcfx.h"
#include "trilerp.h"
#include "xmlParser.h"
#include "gfcfilechooser.h"
#include "gfcStructures.h"

#include "gfcplatemanager.h"
#include "gfcpickmanager.h"
#include "gfcplaybackmanager.h"
#include "gfcplaylistmanager.h"
#include "gfcfxmanager.h"
#include "gfcnetworkmanager.h"
#include "gfcmemorymanager.h"
#include "gfcsessionmanager.h"
#include "gfctrackmanager.h"

extern MainWindow mw;
extern LoadWindow lw;
extern LutWindow lutw;
extern FXWindow fxw;
extern FXControlWindow fxControlWindow1, fxControlWindow2, fxControlWindow3, fxControlWindow4;
extern PreferencesWindow pw;
extern RenderWindow rw;
extern RemoteWindow rmw;
extern PlaylistWindow plw;
extern DrawingToolsWindow dtw;
extern ExrWindow ew;
extern GammaWindow gw;
extern AboutWindow aw;
extern Fl_File_Browser fb;
extern moreOptionsPopup moPopup;
extern MinSpecsWindow reqW;

extern NativeFileChooser *fc;

extern gfcPlateManager plateManager;
extern gfcPickManager pickManager;
extern gfcPlaybackManager playbackManager;
extern gfcPlaylistManager playlistManager;
extern gfcFXManager fxManager;
extern gfcNetworkManager networkManager;
extern gfcMemoryManager memoryManager;
extern gfcSessionManager sessionManager;
extern gfcTrackManager trackManager;
extern gfcReview gReview;
extern gfcSettings sett;

extern std::vector<gfcFX> fxArray;
extern std::vector<int> fxArrayActiveCount;
extern std::vector<gfcFX> fxApplied[4];
extern std::map<std::string, int> fxHashMap;
extern std::map<std::string, int> lutHashMap;
extern int numberOfActiveEffects[4];

extern char gFilename[2048];
extern float gSavedGamma;
extern bool originalGammaExists;
extern int fullscreenActive;
extern int fsX, fsY, fsW, fsH;
extern GLint gFilteringModeMin;
extern GLint gFilteringModeMag;

extern bool rotateActive;
extern bool gLoadCanceled;
extern bool quitNow;
extern bool npotTextures;
extern bool dragging;
extern bool zooming;
extern bool gConnected;
extern bool gIsServer;
extern std::string gChatTextString;

extern int TEST_GLOBAL_loaderToUse;

extern void startLoadingThreadA();

namespace { inline jefe::ui::IEventSystem& evt() { return jefe::ui::IEventSystem::instance(); } }
namespace { inline jefe::ui::IApplication& app() { return jefe::ui::IApplication::instance(); } }

gfcPlate* getPlateFromWidget(Fl_Widget* o, void* v);
void save_input_file(Fl_File_Chooser* w, void* userdata);
void lutCBFillLoadedScroll();
void fxCBFillLoadedScroll();
void toggleFullscreen();
bool isItemInMenu(const char* text, Fl_Choice* menu);
void updateRenderParamsAndSampleFrames(gfcRenderParams& params, Fl_Text_Buffer& textBuffer);
void Render(gfcRenderParams params);
void updateReviewToolsWindowReview();
void updateReviewToolsWindowRevision(gfcRevision* revision);
void updateReviewToolsWindowNote(gfcNote* note);

#endif

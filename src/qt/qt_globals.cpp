// Qt-build globals.
//
// In the FLTK build these instances live at the bottom of main.cpp.
// The Qt build hasn't (yet) reached the point where main_qt.cpp owns the
// full app shell, so we define them in a dedicated TU. As main_qt.cpp
// grows into a real shell that wires events, playback, sessions, etc.,
// these definitions can move there.

#include <glad/glad.h>

#include "../gfcStructures.h"
#include "../gfcplatemanager.h"
#include "../gfcplaybackmanager.h"
#include "../gfctrackmanager.h"
#include "../gfclutmanager.h"
#include "../gfcsessionmanager.h"
#include "../gfcnetworkmanager.h"
#include "../gfcfxmanager.h"
#include "../gfcplaylistmanager.h"
#include "../gfcpickmanager.h"

// Plain data globals.
gfcSettings sett;
GLuint defaultTexture = 0;
bool dragging = false;
bool gResizeTrigger = false;

// Manager singletons. Default-constructed and intentionally inert in
// the Qt build for now — the rendering pipeline reads/writes them but
// none of the FLTK-driven update paths run.
//
// Some managers (lutManager, fxManager, networkManager, sessionManager,
// networkLog, memoryManager) are already defined as global instances at
// the top of their own .cpp files, so they're not declared here to
// avoid duplicate-symbol errors.
gfcTrackManager     trackManager;
gfcPlateManager     plateManager;
gfcPlaybackManager  playbackManager;
gfcPlaylistManager  playlistManager;
gfcPickManager      pickManager;

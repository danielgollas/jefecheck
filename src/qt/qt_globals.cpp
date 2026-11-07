// Qt-build globals, defined in a dedicated TU (the app shell does not own
// them).

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

// Manager singletons, inert in the Qt build. lutManager, fxManager, networkManager,
// sessionManager, networkLog and memoryManager live in their own .cpp files.
gfcTrackManager     trackManager;
gfcPlateManager     plateManager;
gfcPlaybackManager  playbackManager;
gfcPlaylistManager  playlistManager;
gfcPickManager      pickManager;

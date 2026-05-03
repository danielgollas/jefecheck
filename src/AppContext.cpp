#include "AppContext.h"

#include "gfcfxmanager.h"
#include "gfclutmanager.h"
#include "gfcmemorymanager.h"
#include "gfcnetworkmanager.h"
#include "gfcpickmanager.h"
#include "gfcplatemanager.h"
#include "gfcplaybackmanager.h"
#include "gfcplaylistmanager.h"
#include "gfcsessionmanager.h"
#include "gfctrackmanager.h"
#include "gfcStructures.h"

// File-scope singletons defined in:
//   - src/main.cpp                (FLTK build): pickManager, plateManager,
//                                  playbackManager, playlistManager,
//                                  trackManager, sett
//   - src/qt/qt_globals.cpp       (Qt build): same six
//   - src/gfcfxmanager.cpp        (both):     fxManager
//   - src/gfclutmanager.cpp       (both):     lutManager
//   - src/gfcmemorymanager.cpp    (both):     memoryManager
//   - src/gfcnetworkmanager.cpp   (both):     networkManager
//   - src/gfcsessionmanager.cpp   (both):     sessionManager
extern gfcFXManager        fxManager;
extern gfcLUTManager       lutManager;
extern gfcMemoryManager    memoryManager;
extern gfcNetworkManager   networkManager;
extern gfcPickManager      pickManager;
extern gfcPlateManager     plateManager;
extern gfcPlaybackManager  playbackManager;
extern gfcPlaylistManager  playlistManager;
extern gfcSessionManager   sessionManager;
extern gfcTrackManager     trackManager;
extern gfcSettings         sett;

AppContext& AppContext::instance() {
    static AppContext s_instance;
    return s_instance;
}

gfcFXManager&       AppContext::fx()        { return fxManager;       }
gfcLUTManager&      AppContext::luts()      { return lutManager;      }
gfcMemoryManager&   AppContext::memory()    { return memoryManager;   }
gfcNetworkManager&  AppContext::network()   { return networkManager;  }
gfcPickManager&     AppContext::pick()      { return pickManager;     }
gfcPlateManager&    AppContext::plates()    { return plateManager;    }
gfcPlaybackManager& AppContext::playback()  { return playbackManager; }
gfcPlaylistManager& AppContext::playlist()  { return playlistManager; }
gfcSessionManager&  AppContext::session()   { return sessionManager;  }
gfcTrackManager&    AppContext::tracks()    { return trackManager;    }
gfcSettings&        AppContext::settings()  { return sett;            }

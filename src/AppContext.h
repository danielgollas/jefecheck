// Single accessor surface for the application-wide singleton managers.
//
// Before this, every consumer file declared `extern gfcPlateManager
// plateManager;` (and ten siblings) and referenced the globals
// directly. That worked but spread the dependency surface across
// 35+ files and obscured ownership. Phase 1B of the FLTK→Qt migration
// plan funnels those references through one explicit interface.
//
// The managers themselves are still file-scope statics in `main.cpp`
// (FLTK build) or `src/qt/qt_globals.cpp` + the per-manager TUs
// (Qt build). AppContext does not own or register them — its
// implementation just `extern`s them in one place and re-exposes
// references. Consumers include this header and call
// `AppContext::instance().plates()` instead of spelling out the
// extern themselves.
//
// FLTK-only window globals (`MainWindow mw`, `LoadWindow lw`, etc.)
// are intentionally NOT in AppContext — they die in Phase 4 cleanup
// once the Qt port is complete. Plain-data flags (`gFilename`,
// `dragging`, etc.) are also out of scope.
#ifndef JEFECHECK_APP_CONTEXT_H
#define JEFECHECK_APP_CONTEXT_H

class gfcFXManager;
class gfcLUTManager;
class gfcMemoryManager;
class gfcNetworkManager;
class gfcPickManager;
class gfcPlateManager;
class gfcPlaybackManager;
class gfcPlaylistManager;
class gfcSessionManager;
class gfcTrackManager;
struct gfcSettings;

class AppContext {
public:
    static AppContext& instance();

    gfcFXManager&        fx();
    gfcLUTManager&       luts();
    gfcMemoryManager&    memory();
    gfcNetworkManager&   network();
    gfcPickManager&      pick();
    gfcPlateManager&     plates();
    gfcPlaybackManager&  playback();
    gfcPlaylistManager&  playlist();
    gfcSessionManager&   session();
    gfcTrackManager&     tracks();
    gfcSettings&         settings();

private:
    AppContext() = default;
};

#endif

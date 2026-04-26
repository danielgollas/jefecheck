// Owner of the global state shared across the GUI callback domain modules
// in src/callbacks/. The actual callback bodies live there now; this file
// keeps the canonical definitions of the cross-cutting globals plus a
// couple of small shared helpers (`save_input_file`, the no-op
// `gammaCB` lives in MenuCallbacks.cpp).
#include "callbacks/CallbacksInternal.h"

ExrWindow ew(100, 100, 200, 200, "ExrWindow");
GammaWindow gw(0, 0, 100, 100, "Gamma Window");
moreOptionsPopup moPopup(0, 0, 0, 0, "More");
Fl_File_Browser fb(100, 100, 100, 100, "Choose a Directory");
AboutWindow aw(0, 0, 100, 100, "About JefeCheck");

std::vector<gfcFX> fxArray;
std::vector<int> fxArrayActiveCount;
std::vector<gfcFX> fxApplied[4];
std::map<std::string, int> fxHashMap;
std::map<std::string, int> lutHashMap;
int numberOfActiveEffects[4] = {0, 0, 0, 0};

gfcReview gReview;

int fullscreenActive = 0;
int fsX, fsY, fsW, fsH;

GLint gFilteringModeMin = GL_NEAREST;
GLint gFilteringModeMag = GL_NEAREST;

#ifdef WIN32
WORD m_RampSaved[256 * 3];
#endif

char gFilename[2048] = " ";
float gSavedGamma = 1;
bool originalGammaExists = false;

void save_input_file(Fl_File_Chooser* w, void* userdata) {
    strcpy(gFilename, w->value());
}

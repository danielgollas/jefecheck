// Backend-agnostic state held by the main viewport. Both backends
// (GlViewport for FLTK, GlViewport_Qt) inherit this class so the same
// rendering state — plates, preview frames, transforms, framing mode,
// playback flags — lives in one place. Pure data + accessors today; the
// goal in subsequent PRs is to grow this into the home of the viewport's
// rendering and event-routing logic so gfcPlate.cpp can be compiled into
// either build with no FLTK dependency.
//
// Why we don't pull in Fl_Gl_Window: this header has to be includable
// from the Qt-only TUs that don't have FLTK on their search path.
// IGLViewport stays the abstract surface for callers that need
// width/height/redraw etc.; GlViewportCore is the *state* surface for
// callers that need the actual data members (gfcPlate's vp pointer
// dereferences these directly today).
#ifndef JEFECHECK_GL_VIEWPORT_CORE_H
#define JEFECHECK_GL_VIEWPORT_CORE_H

#include "gfcframe.h"
#include "gfcPlate.h"

class GlViewportCore {
public:
    GlViewportCore()
        : ID(0)
        , framingMode(0)
        , targetFPS(24)
        , currentFrame(0)
        , loopMode(0)
        , play(false)
        , transX(0)
        , transY(0)
        , prevX(0)
        , prevY(0)
        , scale(1.0f)
        , startQuad(0)
        , resized(false)
        , showChat(1)
    {
        bgColorR[0] = bgColorG[0] = bgColorB[0] = 0.0f;
        bgColorR[1] = bgColorG[1] = bgColorB[1] = 0.5f;
    }

    // Identity / framing
    int ID;
    int framingMode;

    // Playback state
    int targetFPS;
    int currentFrame;
    int loopMode;
    bool play;

    // Transform state (panning + zoom of the viewport itself)
    int transX;
    int transY;
    int prevX;
    int prevY;
    float scale;
    int startQuad;
    bool resized;

    // Display flags
    int showChat;

    // Background colors (two-color gradient)
    float bgColorR[2];
    float bgColorG[2];
    float bgColorB[2];

    // The four real plate quadrants the viewport renders.
    gfcPlate q1;
    gfcPlate q2;
    gfcPlate q3;
    gfcPlate q4;

    // Test/preview state used while loading.
    gfcFrame tf;            // test frame
    RawFrame trf;           // test raw frame (for preview)
    gfcPlate tp;            // test plate (for preview)
    gfcFrame previewFrameA;
    gfcFrame previewFrameB;
    gfcFrame previewFrameC;
    gfcFrame previewFrameD;
};

#endif

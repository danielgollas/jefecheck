#ifndef GFCRENDERPARAMS_H
#define GFCRENDERPARAMS_H

#include <string>

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/

enum gfcRenderFormats{GFC_RENDER_JPEG,GFC_RENDER_EXR,GFC_RENDER_TIFF, GFC_RENDER_TGA, GFC_RENDER_BMP,GFC_RENDER_PNG};
enum gfcRenderEXRDepths{GFC_HALF, GFC_FLOAT};

class gfcRenderParams {
public:
    gfcRenderParams();

    ~gfcRenderParams();

    int from;
    int to;
    int sizeX;
    int sizeY;
    // Target output resolution for render (0 = use the source frame size).
    // gfcPlate sizes the forRender FBO to this, scaling the source into it.
    int outWidth;
    int outHeight;
    float scale;
    std::string prefix;
	std::string postfix;
    int padding;
    int format;
    std::string formatString;
    std::string path;
    int quadrant;
    int frame;
    int jpegQuality;
    bool jpegProgressive;
    bool jpegOptimized;
    int jpegSubsampling;   // 0 = 4:4:4, 1 = 4:2:2, 2 = 4:2:0
    int pngQuality;
    int tiffCompression;
    int videoCodec;
    int videoVBR;
    int videoBitrateKbps;  // 0 = constant-quality (CRF); >0 = target bitrate
    int videoPreset;       // x264/x265 preset index (0 ultrafast … 8 veryslow)
    std::string filename;
    int createMovie;
    int deleteFramesAfter;
    int frameRate;
    
    int exrCompression;
    int exrFormat;

    // Bits per channel for LDR stills (8 or 16). 16 requires the render FBO
    // to be float so the readback carries real >8-bit precision.
    int bitsPerChannel;

    // When true, bake the plate's aspect/crop letterbox bars into the rendered
    // frame. Off by default (render the full frame without the bars).
    bool bakeCropBars;

    // When true, burn the plate's notes into the rendered frame. Off by
    // default, and deliberately so: burn-in is the point of the feature, but a
    // note accidentally baked into a delivery render is far worse than one the
    // user has to tick a box to get. See
    // docs/superpowers/specs/2026-09-07-annotations-design.md ("Rendering —
    // export").
    //
    // A default member initialiser rather than an entry in the constructor's
    // init list: gfcrenderparams.cpp is outside this change's file ownership,
    // and this guarantees `false` however the object is built.
    bool burnInNotes = false;

};

#endif

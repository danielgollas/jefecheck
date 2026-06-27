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
    int pngQuality;
    int tiffCompression;
    int videoCodec;
    int videoVBR;
    std::string filename;
    int createMovie;
    int deleteFramesAfter;
    int frameRate;
    
    int exrCompression;
    int exrFormat;

};

#endif

#include "gfcimagesaver.h"

// TODO: implement OIIO-based saving

#include <glad/glad.h>

gfcImageSaver* getImageSaverInstance(gfcRenderParams params) {

    gfcImageSaver* ptr=NULL;
    switch ( params.format ) {
    case GFC_RENDER_TIFF:
    case GFC_RENDER_TGA:
    case GFC_RENDER_BMP:
    case GFC_RENDER_PNG:
    case GFC_RENDER_JPEG:
    case GFC_RENDER_EXR:
        // TODO: implement saving via OIIO
        break;
    }
    
    ptr->setRenderParams(params);
    return ptr;
}

gfcImageSaver::gfcImageSaver() {
    pixelFormat=GL_UNSIGNED_BYTE;
    requestFormat=GL_RGBA;
}


gfcImageSaver::~gfcImageSaver() {
}

std::string gfcImageSaver::getErrorString() {
    return errorString;
}

gfcImageSaver::gfcImageSaver(gfcRenderParams pparams) {
    params=pparams;
    pixelFormat=GL_UNSIGNED_BYTE;
    requestFormat=GL_RGBA;
}

int gfcImageSaver::getGLPixelFormat() {
    return pixelFormat;
}

int gfcImageSaver::getGLFormat() {
    return requestFormat;
}

void gfcImageSaver::setRenderParams(gfcRenderParams pparams)
{
	params=pparams;
}



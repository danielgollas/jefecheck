#include "gfcimagesaver.h"

#include "gfcimagesaver_gfl.h"
#include "gfcimagesaver_exr.h"

#include "glew.h"

gfcImageSaver* getImageSaverInstance(gfcRenderParams params) {

    gfcImageSaver* ptr=NULL;
    switch ( params.format ) {
    case GFC_RENDER_TIFF:
    case GFC_RENDER_TGA:
    case GFC_RENDER_BMP:
    case GFC_RENDER_PNG:
    case GFC_RENDER_JPEG:
        {
        ptr=new gfcImageSaver_GFL;        
        }
        break;

    case GFC_RENDER_EXR:
        {
        ptr=new gfcImageSaver_EXR;
        }
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



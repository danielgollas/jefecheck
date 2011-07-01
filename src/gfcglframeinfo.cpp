#include "gfcglframeinfo.h"

gfcGLFrameInfo::gfcGLFrameInfo() {
    format=GL_BGRA;
    internalFormat=GL_RGBA;
    target=GL_TEXTURE_RECTANGLE_ARB;
    dataType=GL_UNSIGNED_BYTE;
    dataPointer=0;
}


gfcGLFrameInfo::~gfcGLFrameInfo() {
}



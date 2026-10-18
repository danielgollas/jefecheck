#ifndef GFCGLFRAMEINFO_H
#define GFCGLFRAMEINFO_H

#include <glad/glad.h>
#include "gfcIOSurface.h"

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcGLFrameInfo {
public:
    gfcGLFrameInfo();

    ~gfcGLFrameInfo();

    GLuint format;
    GLuint internalFormat;
    GLuint target;
    GLuint dataType; 
    GLvoid* dataPointer;
    // macOS: an IOSurface holding the same pixels (owned by the loader), or
    // null. When set, generateTexture binds it instead of uploading.
    void* ioSurface;
    gfcSurface::Layout ioSurfaceLayout;
};

#endif

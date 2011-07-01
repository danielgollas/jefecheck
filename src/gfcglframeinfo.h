#ifndef GFCGLFRAMEINFO_H
#define GFCGLFRAMEINFO_H

#include "glew.h"

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
};

#endif

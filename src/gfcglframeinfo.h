#ifndef GFCGLFRAMEINFO_H
#define GFCGLFRAMEINFO_H

#include <glad/glad.h>

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

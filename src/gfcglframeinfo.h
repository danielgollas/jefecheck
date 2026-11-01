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

    // Planar YUV frames (movies): dataPointer stays null and the planes are
    // converted to RGB on the GPU at upload (gfcYuvConverter). The plane
    // pointers belong to the loader and are valid until its releaseMemory().
    // matrix/range/siting are the gfcYuvMatrix/gfcYuvRange/gfcChromaSiting
    // enums (gfcMovieReader.h) cast to int.
    bool planarYuv = false;
    int planeCount = 0;
    struct Plane { const void* data = nullptr; int width = 0, height = 0, strideBytes = 0; } planes[4];
    int bitDepth = 8;
    int subX = 1, subY = 1;
    int matrix = 0;
    int range = 0;
    int siting = 0;
    bool hasAlpha = false;
    // The rectangle of plane 0 (luma pixels) that the output picture covers:
    // scale and crop are applied by the conversion pass. srcW/srcH 0 = whole plane.
    int srcX = 0, srcY = 0, srcW = 0, srcH = 0;
};

#endif

// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef GFCGLFRAMEINFO_H
#define GFCGLFRAMEINFO_H

#include <glad/glad.h>
#include "gfcIOSurface.h"
#include "gfcUploadPool.h"

#include <cstddef>
#include <vector>

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
    // macOS: an IOSurface holding the same pixels (owned by the loader), or
    // null. When set, generateTexture binds it instead of uploading.
    void* ioSurface;
    gfcSurface::Layout ioSurfaceLayout;
    // Otherwise, a mapped staging buffer (PBO) holding the same pixels, tightly
    // packed (owned by the loader until uploaded), or 0.
    gfcUpload::Ticket staging;
    // Once the pixels are in a surface or staging buffer the loader frees its
    // own copy (dataPointer becomes null), so the filmstrip thumbnail is made
    // beforehand, on the decode thread. Empty when there is none.
    std::vector<unsigned char> thumbnailRGBA;
    int thumbnailW = 0, thumbnailH = 0;
};

/** Downsamples a 4-component image to the filmstrip thumbnail (RGBA8, 48 px
    high, at most 96 wide, nearest). @a format is GL_BGRA or GL_RGBA, @a type
    GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT or GL_HALF_FLOAT_ARB (clamped to the
    display range). Returns false, leaving @a rgba untouched, for any other
    layout or an empty image. */
bool gfcMakeThumbnail(const void* pixels, int width, int height, size_t bytesPerLine,
                      GLenum format, GLenum type,
                      std::vector<unsigned char>& rgba, int& thumbW, int& thumbH);

#endif

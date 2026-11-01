// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef GFCYUVCONVERTER_H
#define GFCYUVCONVERTER_H

// Planar YUV -> RGB with the file's own matrix and range, done once at upload
// so every downstream consumer (super-shader, FX, render read-back,
// thumbnails) sees the same RGB. A glad translation unit: no Qt GL headers
// (developer_notes.md section 1).
#include <vector>

#include "gfcglframeinfo.h"

// Converts info's planes into a new GL_TEXTURE_RECTANGLE_ARB texture of
// dstW x dstH with the given internal format (GL_RGBA or GL_RGBA16F_ARB),
// covering the luma rectangle (srcX, srcY, srcW, srcH) of plane 0 (srcW or
// srcH 0 = the whole plane). Scaling is bilinear. A GL context must be
// current; the bound framebuffer, program, viewport and texture state are
// restored. Returns 0 on failure (the caller falls back to the CPU path).
GLuint gfcYuvConvertToTexture(const gfcGLFrameInfo& info, int srcX, int srcY, int srcW, int srcH,
                              int dstW, int dstH, GLuint internalFormat);

// The same maths on the CPU: nearest luma, bilinear chroma at the same
// siting, clamped and rounded to RGBA8 (dstW x dstH over info.src*). Used for
// filmstrip thumbnails and as the upload fallback. rgba is resized.
bool gfcYuvConvertToRGBA8(const gfcGLFrameInfo& info, int dstW, int dstH, std::vector<unsigned char>& rgba);

// The same maths again, as unclamped float RGBA (what an RGBA16F texture
// holds before its half-float rounding). Used by Stamp Notes on movies so the
// stamped EXR carries the picture the plate shows. rgba is resized.
bool gfcYuvConvertToRGBAF(const gfcGLFrameInfo& info, int dstW, int dstH, std::vector<float>& rgba);

// Frees the plane textures, framebuffer and programs. Call with the context
// they were created in current. They are created in whichever context first
// converts a frame; the app converts in the viewport context only.
void gfcYuvConverterRelease();

#endif

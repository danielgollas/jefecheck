// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfcglframeinfo.h"

#include <cmath>

gfcGLFrameInfo::gfcGLFrameInfo() {
    format=GL_BGRA;
    internalFormat=GL_RGBA;
    target=GL_TEXTURE_RECTANGLE_ARB;
    dataType=GL_UNSIGNED_BYTE;
    dataPointer=0;
    ioSurface=nullptr;
    ioSurfaceLayout=gfcSurface::Layout::BGRA8;
    staging=0;
}


gfcGLFrameInfo::~gfcGLFrameInfo() {
}



namespace {
// IEEE 754 binary16 to float, for the half-float thumbnail path.
float halfToFloat(unsigned short h)
{
	const unsigned sign = (h >> 15) & 1u, exp = (h >> 10) & 0x1Fu, mant = h & 0x3FFu;
	float v;
	if (exp == 0)       v = std::ldexp((float)mant, -24);                  // subnormal
	else if (exp == 31) v = mant ? NAN : INFINITY;
	else                v = std::ldexp((float)(mant | 0x400u), (int)exp - 25);
	return sign ? -v : v;
}
}

bool gfcMakeThumbnail(const void* pixels, int width, int height, size_t bytesPerLine,
                      GLenum format, GLenum type,
                      std::vector<unsigned char>& rgba, int& thumbW, int& thumbH)
{
	if (!pixels || width <= 0 || height <= 0) return false;
	if (type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_HALF_FLOAT_ARB) return false;
	if (format != GL_BGRA && format != GL_RGBA) return false;
	const int dstH = 48;
	int dstW = (int)((double)dstH * width / height + 0.5);
	if (dstW < 1) dstW = 1;
	if (dstW > 96) dstW = 96;
	thumbW = dstW;
	thumbH = dstH;
	rgba.assign((size_t)dstW * dstH * 4, 0);
	const bool bgra = (format == GL_BGRA);
	const unsigned char* base = (const unsigned char*)pixels;
	auto to8 = [&](const unsigned char* row, size_t i) -> unsigned char {
		if (type == GL_UNSIGNED_BYTE) return row[i];
		const unsigned short c = ((const unsigned short*)row)[i];
		if (type == GL_UNSIGNED_SHORT) return (unsigned char)(c >> 8);
		const float v = halfToFloat(c);   // display range only: clamp to 0..1
		return (unsigned char)(v <= 0.0f ? 0 : v >= 1.0f ? 255 : (int)(v * 255.0f + 0.5f));
	};
	for (int y = 0; y < dstH; ++y) {
		const unsigned char* row = base + (size_t)(y * height / dstH) * bytesPerLine;
		for (int x = 0; x < dstW; ++x) {
			const size_t si = (size_t)(x * width / dstW) * 4; // 4 comps
			const size_t di = ((size_t)y * dstW + x) * 4;
			rgba[di+0] = to8(row, si + (bgra ? 2 : 0));
			rgba[di+1] = to8(row, si + 1);
			rgba[di+2] = to8(row, si + (bgra ? 0 : 2));
			rgba[di+3] = to8(row, si + 3);
		}
	}
	return true;
}

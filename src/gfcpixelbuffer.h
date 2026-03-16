#ifndef GFCPIXELBUFFER_H
#define GFCPIXELBUFFER_H

#include <cstdlib>
#include <cstring>
#include <algorithm>

// Simple pixel buffer to replace GFL_BITMAP
// Stores interleaved BGRA pixel data (matching GFL's GFL_BGRA format)
struct GFL_BITMAP {
    int Width;
    int Height;
    int BytesPerLine;
    int BitsPerComponent;
    int ComponentsPerPixel;
    unsigned char *Data;

    GFL_BITMAP() : Width(0), Height(0), BytesPerLine(0),
                   BitsPerComponent(8), ComponentsPerPixel(4), Data(nullptr) {}
    ~GFL_BITMAP() { free(Data); }
};

// Color struct to replace GFL_COLOR
struct GFL_COLOR {
    unsigned short Red;
    unsigned short Green;
    unsigned short Blue;
    unsigned short Alpha;
};

// Rect struct to replace GFL_RECT
struct GFL_RECT {
    int x, y, w, h;
};

// Error type
typedef int GFL_ERROR;
#define GFL_NO_ERROR 0

// Color model enum
#define GFL_BGRA 0
#define GFL_RGB 1

// Load params stub
struct GFL_LOAD_PARAMS {
    int ColorModel;
    int Flags;
};

// Flags
#define GFL_LOAD_METADATA 0x1
#define GFL_LOAD_ORIGINAL_DEPTH 0x2

// File info stub
struct GFL_FILE_INFORMATION {
    int dummy;
};

// Resize modes
#define GFL_RESIZE_QUICK 0
#define GFL_RESIZE_BILINEAR 1
#define GFL_CANVASRESIZE_TOPLEFT 0

// Allocate a bitmap
inline GFL_BITMAP* gflAllockBitmapEx(int colorModel, int width, int height,
                                      int bitsPerComponent, int components, void*) {
    GFL_BITMAP* bmp = new GFL_BITMAP();
    bmp->Width = width;
    bmp->Height = height;
    bmp->BitsPerComponent = bitsPerComponent;
    bmp->ComponentsPerPixel = components;
    bmp->BytesPerLine = width * components * (bitsPerComponent / 8);
    bmp->Data = (unsigned char*)calloc(1, (size_t)bmp->BytesPerLine * height);
    return bmp;
}

// Get color at pixel
inline void gflGetColorAt(GFL_BITMAP* bmp, int y, int x, GFL_COLOR* color) {
    if (!bmp || !bmp->Data) return;
    int bytesPerPixel = bmp->ComponentsPerPixel * (bmp->BitsPerComponent / 8);
    unsigned char* pixel = bmp->Data + (size_t)y * bmp->BytesPerLine + (size_t)x * bytesPerPixel;
    if (bmp->BitsPerComponent == 16) {
        unsigned short* p = (unsigned short*)pixel;
        color->Blue = p[0]; color->Green = p[1]; color->Red = p[2];
        color->Alpha = (bmp->ComponentsPerPixel > 3) ? p[3] : 0xFFFF;
    } else {
        color->Blue = pixel[0]; color->Green = pixel[1]; color->Red = pixel[2];
        color->Alpha = (bmp->ComponentsPerPixel > 3) ? pixel[3] : 0xFF;
    }
}

// Set color at pixel
inline void gflSetColorAt(GFL_BITMAP* bmp, int y, int x, GFL_COLOR* color) {
    if (!bmp || !bmp->Data) return;
    int bytesPerPixel = bmp->ComponentsPerPixel * (bmp->BitsPerComponent / 8);
    unsigned char* pixel = bmp->Data + (size_t)y * bmp->BytesPerLine + (size_t)x * bytesPerPixel;
    if (bmp->BitsPerComponent == 16) {
        unsigned short* p = (unsigned short*)pixel;
        p[0] = color->Blue; p[1] = color->Green; p[2] = color->Red;
        if (bmp->ComponentsPerPixel > 3) p[3] = color->Alpha;
    } else {
        pixel[0] = color->Blue; pixel[1] = color->Green; pixel[2] = color->Red;
        if (bmp->ComponentsPerPixel > 3) pixel[3] = color->Alpha;
    }
}

// Simple nearest-neighbor resize
inline void gflResize(GFL_BITMAP* bmp, void*, int newW, int newH, int, int) {
    if (!bmp || !bmp->Data || newW <= 0 || newH <= 0) return;
    int bytesPerPixel = bmp->ComponentsPerPixel * (bmp->BitsPerComponent / 8);
    int newBytesPerLine = newW * bytesPerPixel;
    unsigned char* newData = (unsigned char*)calloc(1, (size_t)newBytesPerLine * newH);
    for (int y = 0; y < newH; y++) {
        int srcY = y * bmp->Height / newH;
        for (int x = 0; x < newW; x++) {
            int srcX = x * bmp->Width / newW;
            memcpy(newData + (size_t)y * newBytesPerLine + (size_t)x * bytesPerPixel,
                   bmp->Data + (size_t)srcY * bmp->BytesPerLine + (size_t)srcX * bytesPerPixel,
                   bytesPerPixel);
        }
    }
    free(bmp->Data);
    bmp->Data = newData;
    bmp->Width = newW;
    bmp->Height = newH;
    bmp->BytesPerLine = newBytesPerLine;
}

// Crop bitmap in-place
inline void gflCrop(GFL_BITMAP* bmp, void*, GFL_RECT* rect) {
    if (!bmp || !bmp->Data || !rect) return;
    int bytesPerPixel = bmp->ComponentsPerPixel * (bmp->BitsPerComponent / 8);
    int newBytesPerLine = rect->w * bytesPerPixel;
    unsigned char* newData = (unsigned char*)calloc(1, (size_t)newBytesPerLine * rect->h);
    for (int y = 0; y < rect->h; y++) {
        memcpy(newData + (size_t)y * newBytesPerLine,
               bmp->Data + (size_t)(y + rect->y) * bmp->BytesPerLine + (size_t)rect->x * bytesPerPixel,
               newBytesPerLine);
    }
    free(bmp->Data);
    bmp->Data = newData;
    bmp->Width = rect->w;
    bmp->Height = rect->h;
    bmp->BytesPerLine = newBytesPerLine;
}

// Resize canvas (pad to new size)
inline GFL_ERROR gflResizeCanvas(GFL_BITMAP* bmp, void*, int newW, int newH, int, GFL_COLOR*) {
    if (!bmp || !bmp->Data) return -1;
    int bytesPerPixel = bmp->ComponentsPerPixel * (bmp->BitsPerComponent / 8);
    int newBytesPerLine = newW * bytesPerPixel;
    unsigned char* newData = (unsigned char*)calloc(1, (size_t)newBytesPerLine * newH);
    int copyW = std::min(bmp->Width, newW);
    int copyH = std::min(bmp->Height, newH);
    for (int y = 0; y < copyH; y++) {
        memcpy(newData + (size_t)y * newBytesPerLine,
               bmp->Data + (size_t)y * bmp->BytesPerLine,
               (size_t)copyW * bytesPerPixel);
    }
    free(bmp->Data);
    bmp->Data = newData;
    bmp->Width = newW;
    bmp->Height = newH;
    bmp->BytesPerLine = newBytesPerLine;
    return GFL_NO_ERROR;
}

// Stub for GFLC_LIBRARY
namespace GFLC_LIBRARY {
    inline void initialise() {}
}

// Stub types used by trilerp.cpp
struct GFLC_BITMAP {
    int dummy;
    GFLC_BITMAP(int, int, int) {}
    GFLC_BITMAP() {}
};
struct GFLC_SAVE_PARAMS { int dummy; };
struct GFLC_LOAD_PARAMS { int dummy; };
struct GFLC_COLOR {
    float r, g, b;
    GFLC_COLOR() : r(0), g(0), b(0) {}
    GFLC_COLOR(float rr, float gg, float bb) : r(rr), g(gg), b(bb) {}
};

#endif

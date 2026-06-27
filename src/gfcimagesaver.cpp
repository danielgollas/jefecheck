#include "gfcimagesaver.h"

#include <glad/glad.h>

#include <cstdint>
#include <vector>

// X11 defines None as a macro (0L) which conflicts with OIIO's enum values.
#ifdef None
#undef None
#endif

#include <OpenImageIO/imageio.h>

// ---------------------------------------------------------------------------
// OIIO-backed image saver.
//
// The render path (gfcPlate::draw with forRender=true) does:
//   1. saver = getImageSaverInstance(renderParams)
//   2. void* px = saver->getPixelPointer()           <- allocates our buffer
//   3. glGetTexImage(..., saver->getGLFormat(),
//                         saver->getGLPixelFormat(), px)
//   4. saver->save()                                  <- writes via OIIO
//   5. saver->freeResources(); delete saver
//
// We always read back RGBA. LDR formats read 8-bit unsigned; EXR reads
// 32-bit float (OIIO converts to half on write when requested). OpenGL
// hands back rows bottom-to-top, so save() flips vertically to OIIO's
// top-left origin and strips alpha for formats that don't carry it.
// ---------------------------------------------------------------------------

namespace {

class gfcImageSaverOIIO : public gfcImageSaver {
public:
    explicit gfcImageSaverOIIO(gfcRenderParams pparams) : gfcImageSaver(pparams) {
        isFloat_ = (params.format == GFC_RENDER_EXR);
        // We always read back 4-channel RGBA from the FBO texture.
        requestFormat = GL_RGBA;
        pixelFormat   = isFloat_ ? GL_FLOAT : GL_UNSIGNED_BYTE;
    }

    void* getPixelPointer() override {
        width_  = params.sizeX;
        height_ = params.sizeY;
        if (width_ <= 0 || height_ <= 0) {
            errorString = "Invalid render dimensions";
            return nullptr;
        }
        const size_t texels = size_t(width_) * size_t(height_) * 4;
        if (isFloat_) {
            floatBuf_.assign(texels, 0.0f);
            return floatBuf_.data();
        }
        byteBuf_.assign(texels, 0);
        return byteBuf_.data();
    }

    int save(std::string filename = "") override {
        const std::string outPath = filename.empty() ? params.filename : filename;
        if (outPath.empty()) {
            errorString = "Empty output filename";
            return -1;
        }
        if (width_ <= 0 || height_ <= 0) {
            errorString = "Nothing to save (no pixels)";
            return -1;
        }

        // JPEG and BMP don't carry alpha; everything else keeps RGBA.
        const int outChannels =
            (params.format == GFC_RENDER_JPEG || params.format == GFC_RENDER_BMP)
                ? 3 : 4;

        auto out = OIIO::ImageOutput::create(outPath);
        if (!out) {
            errorString = "Cannot create writer for " + outPath + ": " +
                          OIIO::geterror();
            return -1;
        }

        const OIIO::TypeDesc writeType =
            isFloat_
                ? (params.exrFormat == GFC_FLOAT ? OIIO::TypeDesc::FLOAT
                                                 : OIIO::TypeDesc::HALF)
                : OIIO::TypeDesc::UINT8;

        OIIO::ImageSpec spec(width_, height_, outChannels, writeType);
        applyFormatAttributes(spec);

        if (!out->open(outPath, spec)) {
            errorString = "Cannot open " + outPath + ": " + out->geterror();
            return -1;
        }

        // Flip vertically (GL bottom-up -> OIIO top-down) and strip alpha.
        bool ok;
        if (isFloat_) {
            std::vector<float> flipped(size_t(width_) * height_ * outChannels);
            flipAndPack(floatBuf_.data(), flipped.data(), outChannels);
            ok = out->write_image(OIIO::TypeDesc::FLOAT, flipped.data());
        } else {
            std::vector<uint8_t> flipped(size_t(width_) * height_ * outChannels);
            flipAndPack(byteBuf_.data(), flipped.data(), outChannels);
            ok = out->write_image(OIIO::TypeDesc::UINT8, flipped.data());
        }

        if (!ok) {
            errorString = "Write failed for " + outPath + ": " + out->geterror();
            out->close();
            return -1;
        }
        out->close();
        return 0;
    }

    void freeResources() override {
        byteBuf_.clear();
        byteBuf_.shrink_to_fit();
        floatBuf_.clear();
        floatBuf_.shrink_to_fit();
    }

private:
    // Copy `src` (width*height*4, bottom-up) into `dst`
    // (width*height*outChannels, top-down), dropping channels past
    // outChannels.
    template <typename T>
    void flipAndPack(const T* src, T* dst, int outChannels) const {
        for (int y = 0; y < height_; ++y) {
            const T* srcRow = src + size_t(height_ - 1 - y) * width_ * 4;
            T* dstRow       = dst + size_t(y) * width_ * outChannels;
            for (int x = 0; x < width_; ++x) {
                for (int c = 0; c < outChannels; ++c) {
                    dstRow[x * outChannels + c] = srcRow[x * 4 + c];
                }
            }
        }
    }

    void applyFormatAttributes(OIIO::ImageSpec& spec) const {
        switch (params.format) {
            case GFC_RENDER_JPEG: {
                spec.attribute("CompressionQuality", params.jpegQuality);
                if (params.jpegProgressive)
                    spec.attribute("jpeg:progressive", 1);
                // 0 = 4:4:4 (none), 1 = 4:2:2, 2 = 4:2:0.
                const char* sub = params.jpegSubsampling == 1 ? "4:2:2"
                                : params.jpegSubsampling == 2 ? "4:2:0"
                                                              : "4:4:4";
                spec.attribute("jpeg:subsampling", sub);
                break;
            }
            case GFC_RENDER_PNG: {
                int level = params.pngQuality;
                if (level < 0) level = 0;
                if (level > 9) level = 9;
                spec.attribute("png:compressionLevel", level);
                break;
            }
            case GFC_RENDER_TIFF:
                // 0 = LZW, 1 = none, 2 = deflate/zip.
                spec.attribute("compression",
                               params.tiffCompression == 1 ? "none"
                               : params.tiffCompression == 2 ? "zip"
                                                             : "lzw");
                break;
            case GFC_RENDER_EXR: {
                // Index matches the dialog's EXR compression combo.
                static const char* kExrComp[] = {
                    "none", "rle", "zips", "zip", "piz",
                    "pxr24", "b44", "b44a", "dwaa", "dwab"
                };
                const int n = sizeof(kExrComp) / sizeof(kExrComp[0]);
                const int idx = (params.exrCompression >= 0 &&
                                 params.exrCompression < n)
                                    ? params.exrCompression : 3;  // default zip
                spec.attribute("compression", kExrComp[idx]);
                break;
            }
            default:
                break;
        }
    }

    bool isFloat_ = false;
    int  width_   = 0;
    int  height_  = 0;
    std::vector<uint8_t> byteBuf_;
    std::vector<float>   floatBuf_;
};

}  // namespace

gfcImageSaver* getImageSaverInstance(gfcRenderParams params) {
    switch (params.format) {
        case GFC_RENDER_TIFF:
        case GFC_RENDER_TGA:
        case GFC_RENDER_BMP:
        case GFC_RENDER_PNG:
        case GFC_RENDER_JPEG:
        case GFC_RENDER_EXR:
            return new gfcImageSaverOIIO(params);
    }
    return nullptr;
}

gfcImageSaver::gfcImageSaver() {
    pixelFormat = GL_UNSIGNED_BYTE;
    requestFormat = GL_RGBA;
}

gfcImageSaver::~gfcImageSaver() {
}

std::string gfcImageSaver::getErrorString() {
    return errorString;
}

gfcImageSaver::gfcImageSaver(gfcRenderParams pparams) {
    params = pparams;
    pixelFormat = GL_UNSIGNED_BYTE;
    requestFormat = GL_RGBA;
}

int gfcImageSaver::getGLPixelFormat() {
    return pixelFormat;
}

int gfcImageSaver::getGLFormat() {
    return requestFormat;
}

void gfcImageSaver::setRenderParams(gfcRenderParams pparams) {
    params = pparams;
}

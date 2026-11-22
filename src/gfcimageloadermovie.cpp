// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfcimageloadermovie.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "UIConstants.h"

gfcImageLoaderMovie::gfcImageLoaderMovie() {
    sizeX = sizeY = 0;
    bitDepth = originalBitDepth = 8;
    numOfComponents = originalNumOfComponents = 4;
    quadSizeX = quadSizeY = 0;
    texCoords.set(0, 0, 0, 0);
    frameInfo.format = GL_BGRA;
    frameInfo.internalFormat = GL_RGBA;
    frameInfo.target = GL_TEXTURE_RECTANGLE_ARB;
    frameInfo.dataType = GL_UNSIGNED_BYTE;
    frameInfo.dataPointer = nullptr;
    channelNames = {"RGB"};
}

gfcImageLoaderMovie::~gfcImageLoaderMovie() {
    releaseMemory();
}

int gfcImageLoaderMovie::load(gfcLoadParams params) {
    releaseMemory();
    frame_ = gfcMovieFrame();
    std::string err;

    std::shared_ptr<gfcMovieReader> reader = params.movieReader;
    if (!reader) {
        reader = std::make_shared<gfcMovieReader>();
        if (!reader->open(params.fileName, &err)) {
            loadErrorString = "Movie: cannot open " + params.fileName + (err.empty() ? "" : ": " + err);
            std::printf("%s\n", loadErrorString.c_str());
            return -1;
        }
    }
    const gfcMovieInfo& info = reader->info();

    const int64_t index = params.movieFrameIndex >= 0 ? params.movieFrameIndex
                                                      : std::max(params.fromFrame - 1, 0);
    gfcMovieFrame decoded;
    if (!reader->readFrame(index, decoded, &err)) {
        loadErrorString = "Movie: cannot decode frame " + std::to_string(index + 1) + " of " +
                          params.fileName + (err.empty() ? "" : ": " + err);
        std::printf("%s\n", loadErrorString.c_str());
        return -1;
    }

    const int bits = (params.compressed == GFC_16BPC || params.compressed == GFC_16HALF) ? 16 : 8;
    const float scale = (params.scale > 0) ? params.scale : 100.0f;
    const int outW = std::max(1, (int)std::lround(decoded.width * scale / 100.0f));
    const int outH = std::max(1, (int)std::lround(decoded.height * scale / 100.0f));

    // The crop AOI is in scaled pixels, clamped to the image (gflCrop does not).
    int cx = 0, cy = 0, w = outW, h = outH;
    if (params.crop && params.aoi.w > 0 && params.aoi.h > 0) {
        const int x0 = std::clamp(params.aoi.x, 0, outW - 1);
        const int y0 = std::clamp(params.aoi.y, 0, outH - 1);
        const int cw = std::min(params.aoi.w, outW - x0);
        const int ch = std::min(params.aoi.h, outH - y0);
        if (cw > 0 && ch > 0) { cx = x0; cy = y0; w = cw; h = ch; }
    }

    // Planar little-endian YUV goes to the GPU as planes, converted at upload (the frame is kept, not
    // copied); anything else, and 4-bit/S3TC tracks, is converted to BGRA here.
    const bool bigEndian = info.pixFmt.size() > 2 && info.pixFmt.compare(info.pixFmt.size() - 2, 2, "be") == 0;
    const bool gpuDepth = params.compressed == GFC_8BPC || params.compressed == GFC_16BPC ||
                          params.compressed == GFC_16HALF;
    const bool planesOk = decoded.chromaInterleaved ? decoded.planeCount == 2
                                                    : (decoded.planeCount == 3 || decoded.planeCount == 4);
    planar_ = decoded.planarYuv && planesOk && !bigEndian &&
              gpuDepth && decoded.bitDepth >= 8 && decoded.bitDepth <= 16;

    const int bytesPerPixel = 4 * (bits / 8);
    if (planar_) {
        frame_ = decoded;
    } else {
        if (!reader->convertToBGRA(decoded, bits, outW, outH, pixels_, &err)) {
            loadErrorString = "Movie: cannot convert frame " + std::to_string(index + 1) + ": " + err;
            std::printf("%s\n", loadErrorString.c_str());
            pixels_.clear();
            return -1;
        }
        // Keep the frame's description but let the decoder's buffer go: the
        // BGRA copy is what gets uploaded in this colour path.
        frame_ = decoded;
        frame_.owner.reset();
        for (gfcMoviePlane& pl : frame_.plane) pl = gfcMoviePlane();
        frame_.planeCount = 0;

        if (w != outW || h != outH) {
            std::vector<uint8_t> cropped((size_t)w * h * bytesPerPixel);
            for (int y = 0; y < h; ++y)
                std::memcpy(cropped.data() + (size_t)y * w * bytesPerPixel,
                            pixels_.data() + ((size_t)(y + cy) * outW + cx) * bytesPerPixel,
                            (size_t)w * bytesPerPixel);
            pixels_.swap(cropped);
        }
    }

    sizeX = w;
    sizeY = h;
    bitDepth = bits;
    originalBitDepth = info.bitDepth;
    numOfComponents = 4;
    originalNumOfComponents = info.hasAlpha ? 4 : 3;
    texCoords.x = 0;
    texCoords.y = 0;
    texCoords.w = (float)w;
    texCoords.h = (float)h;
    quadSizeX = w;
    quadSizeY = h;

    format = info.pixFmt;
    formatDescription = info.codecName;
    compressionDescription = info.codecName;
    metaData.clear();
    metaData.insert({"Codec", info.codecName});
    metaData.insert({"PixelFormat", info.pixFmt});
    metaData.insert({"FrameRate", std::to_string(info.rateNum) + "/" + std::to_string(info.rateDen)});
    metaData.insert({"FrameCount", std::to_string(info.frameCount)});
    if (!info.startTimecode.empty()) metaData.insert({"StartTimecode", info.startTimecode});
    metaData.insert({"Decoder", info.hardwareDecoder.empty() ? std::string("software") : info.hardwareDecoder});

    frameInfo = gfcGLFrameInfo();
    frameInfo.target = GL_TEXTURE_RECTANGLE_ARB;
    frameInfo.format = GL_BGRA;
    frameInfo.dataPointer = planar_ ? nullptr : pixels_.data();
    if (planar_) {
        frameInfo.planarYuv = true;
        frameInfo.chromaInterleaved = frame_.chromaInterleaved;
        frameInfo.planeCount = frame_.planeCount;
        for (int p = 0; p < frame_.planeCount; ++p) {
            frameInfo.planes[p].data = frame_.plane[p].data;
            frameInfo.planes[p].width = frame_.plane[p].width;
            frameInfo.planes[p].height = frame_.plane[p].height;
            frameInfo.planes[p].strideBytes = frame_.plane[p].strideBytes;
        }
        frameInfo.bitDepth = frame_.bitDepth;
        frameInfo.subX = std::max(1, frame_.subX);
        frameInfo.subY = std::max(1, frame_.subY);
        frameInfo.matrix = int(info.matrix);
        frameInfo.range = int(info.range);
        frameInfo.siting = int(info.siting);
        frameInfo.hasAlpha = info.hasAlpha && frame_.planeCount == 4;
        // Output pixels (cx, cy, w, h) of the outW x outH scaled picture, in
        // source luma pixels.
        const double fx = double(decoded.width) / outW, fy = double(decoded.height) / outH;
        frameInfo.srcX = (int)std::lround(cx * fx);
        frameInfo.srcY = (int)std::lround(cy * fy);
        frameInfo.srcW = std::max(1, (int)std::lround(w * fx));
        frameInfo.srcH = std::max(1, (int)std::lround(h * fy));
    }
    switch (params.compressed) {
    case GFC_16HALF:
    case GFC_16BPC:
        frameInfo.dataType = GL_UNSIGNED_SHORT;
        frameInfo.internalFormat = GL_RGBA16F_ARB;
        break;
    case GFC_4BPC:
        frameInfo.dataType = GL_UNSIGNED_BYTE;
        frameInfo.internalFormat = GL_RGBA4;
        break;
    case GFC_S3TCDX1:
        frameInfo.dataType = GL_UNSIGNED_BYTE;
        frameInfo.internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        break;
    case GFC_8BPC:
    default:
        frameInfo.dataType = GL_UNSIGNED_BYTE;
        frameInfo.internalFormat = GL_RGBA;
        break;
    }
    channelNames = {"RGB"};
    return 0;
}

int gfcImageLoaderMovie::peek(gfcLoadParams params, gfcPeekInfo*) {
    gfcMovieInfo info;
    std::string err;
    if (!gfcMovieReader::probe(params.fileName, info, &err)) {
        loadErrorString = "Movie: " + err;
        return -1;
    }
    sizeX = info.width;
    sizeY = info.height;
    bitDepth = info.bitDepth;
    numOfComponents = info.hasAlpha ? 4 : 3;
    channelNames = {"RGB"};
    return 0;
}

void* gfcImageLoaderMovie::getPixelPointer() {
    return pixels_.empty() ? nullptr : pixels_.data();
}

void gfcImageLoaderMovie::releaseMemory() {
    std::vector<uint8_t>().swap(pixels_);
    frameInfo.dataPointer = nullptr;
    // Drop the decoder's frame; keep the description (index, size, depth).
    frame_.owner.reset();
    for (gfcMoviePlane& pl : frame_.plane) pl = gfcMoviePlane();
    frame_.planeCount = 0;
    for (gfcGLFrameInfo::Plane& pl : frameInfo.planes) pl = gfcGLFrameInfo::Plane();
    frameInfo.planeCount = 0;
    frameInfo.planarYuv = false;
    frameInfo.chromaInterleaved = false;
    planar_ = false;
}

std::vector<std::string> gfcImageLoaderMovie::getChannelNames() {
    return {"RGB"};
}

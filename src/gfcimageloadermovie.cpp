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
    if (!reader->convertToBGRA(decoded, bits, outW, outH, pixels_, &err)) {
        loadErrorString = "Movie: cannot convert frame " + std::to_string(index + 1) + ": " + err;
        std::printf("%s\n", loadErrorString.c_str());
        pixels_.clear();
        return -1;
    }

    // Keep the frame's description but let the decoder's buffer go: the BGRA
    // copy is what gets uploaded in this colour path.
    frame_ = decoded;
    frame_.owner.reset();
    for (gfcMoviePlane& pl : frame_.plane) pl = gfcMoviePlane();
    frame_.planeCount = 0;

    int w = outW, h = outH;
    const int bytesPerPixel = 4 * (bits / 8);
    if (params.crop && params.aoi.w > 0 && params.aoi.h > 0) {
        const int cx = std::clamp(params.aoi.x, 0, w - 1);
        const int cy = std::clamp(params.aoi.y, 0, h - 1);
        const int cw = std::min(params.aoi.w, w - cx);
        const int ch = std::min(params.aoi.h, h - cy);
        if (cw > 0 && ch > 0 && (cw != w || ch != h)) {
            std::vector<uint8_t> cropped((size_t)cw * ch * bytesPerPixel);
            for (int y = 0; y < ch; ++y)
                std::memcpy(cropped.data() + (size_t)y * cw * bytesPerPixel,
                            pixels_.data() + ((size_t)(y + cy) * w + cx) * bytesPerPixel,
                            (size_t)cw * bytesPerPixel);
            pixels_.swap(cropped);
            w = cw;
            h = ch;
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

    frameInfo.target = GL_TEXTURE_RECTANGLE_ARB;
    frameInfo.format = GL_BGRA;
    frameInfo.dataPointer = pixels_.data();
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
}

std::vector<std::string> gfcImageLoaderMovie::getChannelNames() {
    return {"RGB"};
}

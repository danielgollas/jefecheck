#include "gfcimageloaderoiio.h"
#include "gfcStructures.h"
#include <OpenImageIO/imageio.h>
#include <cstring>

gfcImageLoaderOIIO::gfcImageLoaderOIIO() : theBitmap(nullptr) {
}

gfcImageLoaderOIIO::~gfcImageLoaderOIIO() {
    releaseMemory();
}

int gfcImageLoaderOIIO::load(gfcLoadParams params) {
    this->params = params;

    auto inp = OIIO::ImageInput::open(params.fileName);
    if (!inp) {
        loadErrorString = "OIIO: Cannot open " + params.fileName;
        printf("OIIO Error: %s\n", loadErrorString.c_str());
        return -1;
    }

    const OIIO::ImageSpec &spec = inp->spec();
    int width = spec.width;
    int height = spec.height;
    int channels = spec.nchannels;

    // Determine bit depth
    int bitsPerComponent = 8;
    OIIO::TypeDesc readType = OIIO::TypeDesc::UINT8;
    if (spec.format == OIIO::TypeDesc::UINT16 || spec.format == OIIO::TypeDesc::HALF ||
        spec.format == OIIO::TypeDesc::FLOAT) {
        bitsPerComponent = 16;
        readType = OIIO::TypeDesc::UINT16;
    }

    // Store original info
    originalBitDepth = spec.format.size() * 8;
    originalNumOfComponents = channels;

    // Always read as BGRA for consistency with the rest of JefeCheck
    int outChannels = 4;
    theBitmap = gflAllockBitmapEx(GFL_BGRA, width, height, bitsPerComponent, outChannels, nullptr);
    if (!theBitmap || !theBitmap->Data) {
        loadErrorString = "OIIO: Failed to allocate bitmap";
        inp->close();
        return -1;
    }

    // Read into a temporary RGBA buffer, then swizzle to BGRA
    size_t pixelSize = outChannels * (bitsPerComponent / 8);
    size_t rowBytes = (size_t)width * pixelSize;
    std::vector<unsigned char> rowBuf(rowBytes);

    for (int y = 0; y < height; y++) {
        // Read one scanline as RGBA
        inp->read_scanline(y, 0, readType, rowBuf.data());

        unsigned char *dst = theBitmap->Data + (size_t)y * theBitmap->BytesPerLine;

        if (bitsPerComponent == 16) {
            unsigned short *src16 = (unsigned short*)rowBuf.data();
            unsigned short *dst16 = (unsigned short*)dst;
            for (int x = 0; x < width; x++) {
                int si = x * outChannels;
                int di = x * outChannels;
                dst16[di + 0] = (channels > 2) ? src16[si + 2] : src16[si]; // B
                dst16[di + 1] = (channels > 1) ? src16[si + 1] : src16[si]; // G
                dst16[di + 2] = src16[si + 0]; // R
                dst16[di + 3] = (channels > 3) ? src16[si + 3] : 0xFFFF;   // A
            }
        } else {
            unsigned char *src8 = rowBuf.data();
            for (int x = 0; x < width; x++) {
                int si = x * std::min(channels, outChannels);
                int di = x * outChannels;
                dst[di + 0] = (channels > 2) ? src8[si + 2] : src8[si]; // B
                dst[di + 1] = (channels > 1) ? src8[si + 1] : src8[si]; // G
                dst[di + 2] = src8[si + 0]; // R
                dst[di + 3] = (channels > 3) ? src8[si + 3] : 0xFF;     // A
            }
        }
    }

    inp->close();

    // Apply scale if requested
    if (params.scale > 0 && params.scale != 100) {
        int newW = (int)(width * params.scale / 100.0f);
        int newH = (int)(height * params.scale / 100.0f);
        gflResize(theBitmap, nullptr, newW, newH, GFL_RESIZE_BILINEAR, 0);
    }

    // Apply crop if requested
    if (params.crop && params.aoi.w > 0 && params.aoi.h > 0) {
        GFL_RECT rect;
        rect.x = (int)params.aoi.x;
        rect.y = (int)params.aoi.y;
        rect.w = (int)params.aoi.w;
        rect.h = (int)params.aoi.h;
        gflCrop(theBitmap, nullptr, &rect);
    }

    // Fill loader info
    sizeX = theBitmap->Width;
    sizeY = theBitmap->Height;
    bitDepth = bitsPerComponent;
    numOfComponents = outChannels;
    format = spec.format.c_str();
    formatDescription = inp->format_name();

    // Set GL frame info
    if (bitsPerComponent == 16) {
        frameInfo.format = GL_BGRA;
        frameInfo.internalFormat = GL_RGBA16;
        frameInfo.dataType = GL_UNSIGNED_SHORT;
    } else {
        frameInfo.format = GL_BGRA;
        frameInfo.internalFormat = GL_RGBA8;
        frameInfo.dataType = GL_UNSIGNED_BYTE;
    }
    frameInfo.dataPointer = theBitmap->Data;

    // Channel names
    channelNames.clear();
    for (int i = 0; i < channels && i < (int)spec.channelnames.size(); i++) {
        channelNames.push_back(spec.channelnames[i]);
    }

    // Copy metadata
    metaData.clear();
    for (auto &p : spec.extra_attribs) {
        metaData.insert({p.name().string(), p.get_string()});
    }

    return 0;
}

int gfcImageLoaderOIIO::peek(gfcLoadParams params, gfcPeekInfo *results) {
    auto inp = OIIO::ImageInput::open(params.fileName);
    if (!inp) return -1;
    const OIIO::ImageSpec &spec = inp->spec();
    sizeX = spec.width;
    sizeY = spec.height;
    bitDepth = spec.format.size() * 8;
    numOfComponents = spec.nchannels;
    inp->close();
    return 0;
}

void* gfcImageLoaderOIIO::getPixelPointer() {
    return theBitmap ? theBitmap->Data : nullptr;
}

void gfcImageLoaderOIIO::releaseMemory() {
    delete theBitmap;
    theBitmap = nullptr;
}

std::vector<std::string> gfcImageLoaderOIIO::getChannelNames() {
    return channelNames;
}

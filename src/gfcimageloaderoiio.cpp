#include "gfcimageloaderoiio.h"
#include "UIConstants.h"
#include "gfcStructures.h"
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/imagebufalgo.h>
#include <cstring>
#include <set>
#include <algorithm>

gfcImageLoaderOIIO::gfcImageLoaderOIIO() : theBitmap(nullptr) {
}

gfcImageLoaderOIIO::~gfcImageLoaderOIIO() {
    releaseMemory();
}

// Parse channel names to find layers and their channel indices.
// E.g. channels [R, G, B, right.R, right.G, right.B] ->
//   default layer "" has channels 0,1,2 (R,G,B)
//   layer "right" has channels 3,4,5 (right.R, right.G, right.B)
struct LayerInfo {
    std::string name;         // display name (e.g. "RGB", "right", "diffuse")
    int chBegin;              // first channel index
    int chCount;              // number of channels
    bool hasAlpha;
};

static std::vector<LayerInfo> discoverLayers(const OIIO::ImageSpec &spec) {
    std::vector<LayerInfo> layers;

    // Group channels by layer prefix
    struct ChannelGroup {
        std::string prefix;
        int firstIdx;
        int count;
        bool hasAlpha;
    };
    std::vector<ChannelGroup> groups;
    std::string currentPrefix = "";
    int groupStart = 0;

    for (int i = 0; i < (int)spec.channelnames.size(); i++) {
        std::string name = spec.channelnames[i];
        std::string prefix = "";
        size_t dot = name.rfind('.');
        if (dot != std::string::npos) {
            prefix = name.substr(0, dot);
        }

        if (i == 0 || prefix != currentPrefix) {
            if (i > 0) {
                // Close previous group
                ChannelGroup g;
                g.prefix = currentPrefix;
                g.firstIdx = groupStart;
                g.count = i - groupStart;
                g.hasAlpha = false;
                // Check if group has alpha
                for (int j = groupStart; j < i; j++) {
                    std::string cn = spec.channelnames[j];
                    size_t d = cn.rfind('.');
                    std::string suffix = (d != std::string::npos) ? cn.substr(d+1) : cn;
                    if (suffix == "A" || suffix == "Alpha" || suffix == "a") g.hasAlpha = true;
                }
                groups.push_back(g);
            }
            currentPrefix = prefix;
            groupStart = i;
        }
    }
    // Close last group
    if (spec.channelnames.size() > 0) {
        ChannelGroup g;
        g.prefix = currentPrefix;
        g.firstIdx = groupStart;
        g.count = (int)spec.channelnames.size() - groupStart;
        g.hasAlpha = false;
        for (int j = groupStart; j < (int)spec.channelnames.size(); j++) {
            std::string cn = spec.channelnames[j];
            size_t d = cn.rfind('.');
            std::string suffix = (d != std::string::npos) ? cn.substr(d+1) : cn;
            if (suffix == "A" || suffix == "Alpha" || suffix == "a") g.hasAlpha = true;
        }
        groups.push_back(g);
    }

    // Convert groups to layers
    for (auto &g : groups) {
        LayerInfo li;
        if (g.prefix.empty()) {
            // Default layer
            if (g.hasAlpha && g.count >= 4) li.name = "RGBA";
            else if (g.count >= 3) li.name = "RGB";
            else if (g.count == 1) li.name = "Luminance";
            else li.name = "RGB";
        } else {
            li.name = g.prefix;
        }
        li.chBegin = g.firstIdx;
        li.chCount = g.count;
        li.hasAlpha = g.hasAlpha;
        layers.push_back(li);
    }

    return layers;
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
    int totalChannels = spec.nchannels;

    // Determine bit depth from the requested compression mode
    int bitsPerComponent = 8;
    OIIO::TypeDesc readType = OIIO::TypeDesc::UINT8;
    if (params.compressed == GFC_16BPC || params.compressed == GFC_16HALF) {
        bitsPerComponent = 16;
        readType = OIIO::TypeDesc::UINT16;
    }

    originalBitDepth = spec.format.size() * 8;
    originalNumOfComponents = totalChannels;

    // Discover layers
    auto layers = discoverLayers(spec);

    printf("OIIO: File %s (%dx%d, %d-bit, %d total channels)\n",
           params.fileName.c_str(), width, height, bitsPerComponent, totalChannels);
    printf("OIIO: Channels: ");
    for (int i = 0; i < (int)spec.channelnames.size(); i++)
        printf("[%s] ", spec.channelnames[i].c_str());
    printf("\n");
    printf("OIIO: Layers: ");
    for (auto &l : layers)
        printf("[%s ch%d-%d] ", l.name.c_str(), l.chBegin, l.chBegin + l.chCount - 1);
    printf("\n");

    // Select which layer to read based on params.channelName
    int chBegin = 0;
    int srcChannels = std::min(totalChannels, 3); // default: first 3 channels
    bool hasAlpha = false;

    if (!params.channelName.empty()) {
        for (auto &l : layers) {
            if (l.name == params.channelName) {
                chBegin = l.chBegin;
                srcChannels = std::min(l.chCount, 4);
                hasAlpha = l.hasAlpha;
                printf("OIIO: Selected layer '%s' (channels %d-%d)\n",
                       l.name.c_str(), chBegin, chBegin + srcChannels - 1);
                break;
            }
        }
    } else {
        // Default: use first layer
        if (!layers.empty()) {
            chBegin = layers[0].chBegin;
            srcChannels = std::min(layers[0].chCount, 4);
            hasAlpha = layers[0].hasAlpha;
        }
    }

    // Allocate output bitmap (always BGRA)
    int outChannels = 4;
    theBitmap = gflAllockBitmapEx(GFL_BGRA, width, height, bitsPerComponent, outChannels, nullptr);
    if (!theBitmap || !theBitmap->Data) {
        loadErrorString = "OIIO: Failed to allocate bitmap";
        inp->close();
        return -1;
    }

    // Read selected channels
    int bytesPerSample = bitsPerComponent / 8;
    size_t srcPixelSize = srcChannels * bytesPerSample;
    size_t srcRowBytes = (size_t)width * srcPixelSize;
    std::vector<unsigned char> imgBuf(srcRowBytes * height);

    inp->read_image(0, 0, chBegin, chBegin + srcChannels, readType, imgBuf.data());

    // Convert to BGRA
    for (int y = 0; y < height; y++) {
        unsigned char *src = imgBuf.data() + (size_t)y * srcRowBytes;
        unsigned char *dst = theBitmap->Data + (size_t)y * theBitmap->BytesPerLine;

        if (bitsPerComponent == 16) {
            unsigned short *src16 = (unsigned short*)src;
            unsigned short *dst16 = (unsigned short*)dst;
            for (int x = 0; x < width; x++) {
                int si = x * srcChannels;
                int di = x * outChannels;
                unsigned short r = src16[si + 0];
                unsigned short g = (srcChannels > 1) ? src16[si + 1] : r;
                unsigned short b = (srcChannels > 2) ? src16[si + 2] : r;
                unsigned short a = (srcChannels > 3) ? src16[si + 3] : 0xFFFF;
                dst16[di + 0] = b;
                dst16[di + 1] = g;
                dst16[di + 2] = r;
                dst16[di + 3] = a;
            }
        } else {
            for (int x = 0; x < width; x++) {
                int si = x * srcChannels;
                int di = x * outChannels;
                unsigned char r = src[si + 0];
                unsigned char g = (srcChannels > 1) ? src[si + 1] : r;
                unsigned char b = (srcChannels > 2) ? src[si + 2] : r;
                unsigned char a = (srcChannels > 3) ? src[si + 3] : 0xFF;
                dst[di + 0] = b;
                dst[di + 1] = g;
                dst[di + 2] = r;
                dst[di + 3] = a;
            }
        }
    }

    inp->close();

    // Apply scale via OIIO (the filter actually matters here — our
    // local gflResize ignores filter selection and is nearest-only).
    if (params.scale > 0 && params.scale != 100) {
        int newW = (int)(width * params.scale / 100.0f);
        int newH = (int)(height * params.scale / 100.0f);

        const char* filterName = oiioFilterNameFor(params.filterType);

        // Wrap the decoded GFL bitmap as an OIIO ImageBuf without copying.
        OIIO::ImageSpec srcSpec(theBitmap->Width, theBitmap->Height,
                                theBitmap->ComponentsPerPixel,
                                theBitmap->BitsPerComponent == 8  ? OIIO::TypeDesc::UINT8 :
                                theBitmap->BitsPerComponent == 16 ? OIIO::TypeDesc::UINT16 :
                                                                    OIIO::TypeDesc::FLOAT);
        OIIO::ImageBuf src(srcSpec, theBitmap->Data);

        OIIO::ImageSpec dstSpec(newW, newH,
                                srcSpec.nchannels, srcSpec.format);
        OIIO::ImageBuf dst(dstSpec);

        // OIIO 3.x KWArgs form — pass the filter name as a string.
        bool ok = OIIO::ImageBufAlgo::resize(
            dst, src, {{ "filtername", filterName }});
        if (!ok) {
            // Fall back to box (nearest) if the filter name was rejected.
            ok = OIIO::ImageBufAlgo::resize(
                dst, src, {{ "filtername", "box" }});
        }

        if (ok) {
            // Copy resized pixels back into a freshly-allocated GFL buffer.
            const int bytesPerPixel = srcSpec.nchannels * (theBitmap->BitsPerComponent / 8);
            const int newBytesPerLine = newW * bytesPerPixel;
            unsigned char* newData = (unsigned char*)calloc(1,
                (size_t)newBytesPerLine * newH);
            dst.get_pixels(OIIO::ROI::All(), srcSpec.format, newData);
            free(theBitmap->Data);
            theBitmap->Data = newData;
            theBitmap->Width = newW;
            theBitmap->Height = newH;
            theBitmap->BytesPerLine = newBytesPerLine;
        } else {
            printf("OIIO: resize failed (filter=%s), keeping original size\n",
                   filterName);
        }
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
    printf("OIIO: Loaded %dx%d, %d-bit, reading %d channels from offset %d\n",
           theBitmap->Width, theBitmap->Height, bitsPerComponent, srcChannels, chBegin);
    sizeX = theBitmap->Width;
    sizeY = theBitmap->Height;
    bitDepth = bitsPerComponent;
    numOfComponents = outChannels;

    texCoords.x = 0;
    texCoords.y = 0;
    texCoords.w = (float)theBitmap->Width;
    texCoords.h = (float)theBitmap->Height;
    quadSizeX = theBitmap->Width;
    quadSizeY = theBitmap->Height;

    format = spec.format.c_str();
    formatDescription = "";

    // Set GL frame info based on requested compression mode (matching DPX loader)
    frameInfo.target = GL_TEXTURE_RECTANGLE_ARB;
    frameInfo.dataPointer = theBitmap->Data;
    switch (params.compressed) {
    case GFC_4BPC:
        frameInfo.format = GL_BGRA;
        frameInfo.dataType = GL_UNSIGNED_BYTE;
        frameInfo.internalFormat = GL_RGBA4;
        break;
    case GFC_16HALF:
    case GFC_16BPC:
        frameInfo.format = GL_BGRA;
        frameInfo.dataType = GL_UNSIGNED_SHORT;
        frameInfo.internalFormat = GL_RGBA16F_ARB;
        break;
    case GFC_S3TCDX1:
        frameInfo.format = GL_BGRA;
        frameInfo.dataType = GL_UNSIGNED_BYTE;
        frameInfo.internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        break;
    case GFC_8BPC:
    default:
        frameInfo.format = GL_BGRA;
        frameInfo.dataType = GL_UNSIGNED_BYTE;
        frameInfo.internalFormat = GL_RGBA;
        break;
    }

    // Build channel name list for the dropdown
    channelNames.clear();
    for (auto &l : layers) {
        channelNames.push_back(l.name);
    }

    // Copy metadata
    metaData.clear();
#if OIIO_VERSION_MAJOR >= 3
    for (auto &p : spec.extra_attribs) {
        metaData.insert({p.name().string(), p.get_string()});
    }
#else
    // OIIO 2.x: use serialized string to extract metadata
    for (auto &p : spec.extra_attribs) {
        std::string val;
        auto td = p.type();
        if (td == OIIO::TypeDesc::STRING)
            val = *(const char **)p.data();
        else if (td == OIIO::TypeDesc::INT)
            val = std::to_string(*(const int *)p.data());
        else if (td == OIIO::TypeDesc::FLOAT)
            val = std::to_string(*(const float *)p.data());
        else
            val = "?";
        metaData.insert({p.name().string(), val});
    }
#endif

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

    // Discover layers for channel names
    auto layers = discoverLayers(spec);
    channelNames.clear();
    for (auto &l : layers) {
        channelNames.push_back(l.name);
    }

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

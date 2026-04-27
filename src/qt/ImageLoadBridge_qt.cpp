#include "ImageLoadBridge_qt.h"

#include "../gfcimageloaderoiio.h"
#include "../gfcloadparams.h"
#include "../UIConstants.h"

#include <cstring>

namespace jefe::qt {

bool loadImageBGRA8(const std::string& path,
                    std::vector<unsigned char>& outBytes,
                    int& outW, int& outH,
                    std::string& outErr) {
    outBytes.clear();
    outW = outH = 0;
    outErr.clear();

    gfcImageLoaderOIIO loader;
    gfcLoadParams params;
    params.fileName = path;
    params.compressed = GFC_8BPC;

    if (loader.load(params) != 0) {
        outErr = loader.loadErrorString.empty()
                     ? "Failed to load image"
                     : loader.loadErrorString;
        return false;
    }

    outW = loader.sizeX;
    outH = loader.sizeY;
    if (outW <= 0 || outH <= 0) {
        outErr = "Loader returned invalid dimensions";
        return false;
    }

    void* src = loader.getPixelPointer();
    if (!src) {
        outErr = "Loader returned null pixel pointer";
        return false;
    }

    const size_t bytes = (size_t)outW * (size_t)outH * 4u;
    outBytes.resize(bytes);
    std::memcpy(outBytes.data(), src, bytes);

    loader.releaseMemory();
    return true;
}

}  // namespace jefe::qt

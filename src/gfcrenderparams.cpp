#include "gfcrenderparams.h"

gfcRenderParams::gfcRenderParams()
    : from(0)
    , to(0)
    , sizeX(0)
    , sizeY(0)
    , outWidth(0)
    , outHeight(0)
    , scale(1.0f)
    , padding(4)
    , format(GFC_RENDER_PNG)
    , quadrant(0)
    , frame(0)
    , jpegQuality(95)
    , jpegProgressive(false)
    , jpegOptimized(false)
    , jpegSubsampling(0)        // 4:4:4
    , pngQuality(6)             // libpng zlib compression level 0..9
    , tiffCompression(0)        // 0 = LZW (see gfcImageSaverOIIO)
    , videoCodec(0)
    , videoVBR(0)
    , videoBitrateKbps(0)
    , videoPreset(4)            // ~medium
    , createMovie(0)
    , deleteFramesAfter(0)
    , frameRate(24)
    , exrCompression(0)         // 0 = zip (see gfcImageSaverOIIO)
    , exrFormat(GFC_HALF)
    , bitsPerChannel(8)
    , bakeCropBars(false)
{
}


gfcRenderParams::~gfcRenderParams()
{
}



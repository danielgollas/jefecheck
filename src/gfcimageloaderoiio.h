#ifndef GFCIMAGELOADEROIIO_H
#define GFCIMAGELOADEROIIO_H

#include "gfcimageloader.h"
#include "gfcpixelbuffer.h"
#include <OpenImageIO/imageio.h>

/**
 * Image loader using OpenImageIO for general image formats
 * (TIFF, JPEG, PNG, TGA, etc.)
 */
class gfcImageLoaderOIIO : public gfcImageLoader {
public:
    gfcImageLoaderOIIO();
    ~gfcImageLoaderOIIO();

    virtual int load(gfcLoadParams params);
    virtual int peek(gfcLoadParams params, gfcPeekInfo *results);
    virtual void* getPixelPointer();
    virtual void releaseMemory();
    virtual std::vector<std::string> getChannelNames();

private:
    GFL_BITMAP *theBitmap;
    gfcLoadParams params;
};

#endif

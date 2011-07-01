#ifndef GFCIMAGELOADERGFL_H
#define GFCIMAGELOADERGFL_H

#include "gfcimageloader.h"

#include "libgfl.h"
#include "libgfle.h"

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/
class gfcImageLoaderGFL : public gfcImageLoader
{
public:
    gfcImageLoaderGFL();

    ~gfcImageLoaderGFL();
    
    virtual std::vector<std::string> getChannelNames();
    
    virtual int fillProcessor(gfcImageProcessor& processor);
    virtual int load(gfcLoadParams params);
    virtual int peek(gfcLoadParams params, gfcPeekInfo* results);
    virtual void* getPixelPointer();
    virtual void releaseMemory();
    
    GFL_BITMAP *theBitmap;
    

};

#endif

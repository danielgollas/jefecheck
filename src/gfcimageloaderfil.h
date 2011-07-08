#ifndef GFCIMAGELOADERFIL_H
#define GFCIMAGELOADERFIL_H

#include "gfcimageloader.h"

#include "FreeImage.h"
#include "FreeImagePlus.h"

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/
class gfcImageLoaderFIL : public gfcImageLoader
{
public:
    gfcImageLoaderFIL();

    ~gfcImageLoaderFIL();
    
    virtual std::vector<std::string> getChannelNames();
    
    virtual int fillProcessor(gfcImageProcessor& processor);
    virtual int load(gfcLoadParams params);
    virtual int peek(gfcLoadParams params, gfcPeekInfo* results);
    virtual void* getPixelPointer();
    virtual void releaseMemory();
    
    fipImage theBitmap;
    FREE_IMAGE_FORMAT fif;
    //FIBITMAP *theBitmap;
    
    //GFL_BITMAP *theBitmap;
    

};

#endif

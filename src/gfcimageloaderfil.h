#ifndef GFCIMAGELOADERFIL_H
#define GFCIMAGELOADERFIL_H

#include "gfcimageloader.h"

#ifdef USEFREEIMAGE
#include "FreeImage.h"
#include "FreeImagePlus.h"
#endif
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
#ifdef USEFREEIMAGE
    fipImage theBitmap;
    FREE_IMAGE_FORMAT fif;
#endif
    //FIBITMAP *theBitmap;
    
    //GFL_BITMAP *theBitmap;
    

};

#endif

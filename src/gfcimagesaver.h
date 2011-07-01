#ifndef GFCIMAGESAVER_H
#define GFCIMAGESAVER_H

#include "gfcrenderparams.h"

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
	@short
*/
class gfcImageSaver {
public:
    gfcImageSaver();
    gfcImageSaver(gfcRenderParams pparams);
    ~gfcImageSaver();

    int getGLPixelFormat();
    int getGLFormat();
    
    virtual void* getPixelPointer()=0;
    void setRenderParams(gfcRenderParams pparams);
    virtual int save(std::string filename="")=0;
    virtual void freeResources()=0;
    std::string getErrorString();


protected:

    std::string errorString;
    int pixelFormat;
    int requestFormat;
    gfcRenderParams params;

};

gfcImageSaver* getImageSaverInstance(gfcRenderParams params);

#endif

#ifndef GFCIMAGESAVER_GFL_H
#define GFCIMAGESAVER_GFL_H

#include "gfcimagesaver.h"

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcImageSaver_GFL : public gfcImageSaver
{
public:
    gfcImageSaver_GFL();

    ~gfcImageSaver_GFL();

    virtual int getGLFormat();
    virtual int getGLPixelFormat();
    virtual int save(std::string filename);
    virtual void* getPixelPointer();
    virtual void freeResources();
private:

    unsigned char *pixels;

};

#endif

#ifndef GFCIMAGESAVER_EXR_H
#define GFCIMAGESAVER_EXR_H

#include "gfcimagesaver.h"

#include <OpenEXR/ImfRgbaFile.h>
#include <OpenEXR/ImfTiledRgbaFile.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfTiledInputFile.h>
#include <OpenEXR/ImfPreviewImage.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/Iex.h>
#include <OpenEXR/ImathMath.h>
#include <OpenEXR/ImfRgba.h>
#include <OpenEXR/ImfArray.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImathFun.h>
#include <OpenEXR/halfFunction.h>
#include <OpenEXR/ImfStandardAttributes.h>
#include <OpenEXR/ImfKeyCode.h>
#include <OpenEXR/ImfTimeCode.h>
#include <OpenEXR/half.h>
#include <OpenEXR/halfLimits.h>


using namespace Imath;
using namespace Imf;
using namespace std;

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcImageSaver_EXR : public gfcImageSaver
{
public:
    gfcImageSaver_EXR();

    ~gfcImageSaver_EXR();

    virtual int save(std::string filename);
    virtual void freeResources();
    virtual void* getPixelPointer();
private:
    Rgba *pixels;
    Rgba *floatPixels;
};

#endif

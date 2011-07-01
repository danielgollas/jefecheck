#ifndef GFCIMAGEPROCESSOR_H
#define GFCIMAGEPROCESSOR_H

#include "gflC.h"
#include "gfcrectang.h"
#include <vector>
#include "gfcloadparams.h"
/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/
class gfcImageProcessor{
public:
    gfcImageProcessor(GFLC_BITMAP* theBitmap);

    ~gfcImageProcessor();
    
    void crop(gfcRectang rectang);
    void scale(float scale, int filterType);
    void gamma(float* gammaLUT);
    void applyLuts(std::vector<int> lutList);
    void convertToNBits(int nBits);
    void process(gfcLoadParams params);
    
    private:
    GFLC_BITMAP* bitmap;

};

#endif

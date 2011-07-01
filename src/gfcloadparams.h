#ifndef GFCLOADPARAMS_H
#define GFCLOADPARAMS_H

#include <string>
#include "gfcrectang.h"

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/
class gfcLoadParams{
public:
    gfcLoadParams();

    ~gfcLoadParams();

	bool crop;
    std::string fileName;
    float scale;
    gfcRectang aoi;
    int filterType;
    char append; //append mode can be stream, bool stream is now useless.
    bool stream;
    float gamma;
    //float *gammaLUT;
    int fromFrame;
    int toFrame;
    int loadFromTimeline;
    /*float exposition;
    float defog;
    float kneeH;
    float kneeL;*/
    int channel;
    char compressed;
    bool forceGFLLoading;
	std::string channelName;
	
	void fixWindowsPath();

};

#endif

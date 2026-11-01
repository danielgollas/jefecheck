#ifndef GFCLOADPARAMS_H
#define GFCLOADPARAMS_H

#include <memory>
#include <string>
#include "gfcrectang.h"

class gfcMovieReader;

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

	// Movies: the 0-based frame to decode (-1 = derive it from fromFrame) and
	// the track's shared, already-open reader (null = the loader opens its own).
	int movieFrameIndex = -1;
	std::shared_ptr<gfcMovieReader> movieReader;
	
	void fixWindowsPath();

};

#endif

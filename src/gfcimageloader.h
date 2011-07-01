#ifndef GFCIMAGELOADER_H
#define GFCIMAGELOADER_H

#include "gfcpeekinfo.h"
#include "gfcimageprocessor.h"
#include "gfcloadparams.h"
#include <string>
#ifdef WIN32 
#include <map> 
#else 
#include <map> 
#endif //for metadata
#include "gfcglframeinfo.h"

#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>

/**
All image loaders derive from this one

	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/
class gfcImageLoader{
public:
    
	virtual ~gfcImageLoader(){};

    int sizeX;
    int sizeY;
    int bitDepth;
    int originalBitDepth;
    int numOfComponents;
    int originalNumOfComponents;
    
    int quadSizeX;
    int quadSizeY;
    gfcRectangf texCoords;
    
    
    //std::string metaData;
   
    
    std::string format;
    std::string formatDescription;
    std::string compressionDescription;
    std::multimap<std::string,std::string> metaData;
    std::string loadErrorString;
	
    virtual int load(gfcLoadParams params)=0;
    virtual int peek(gfcLoadParams params, gfcPeekInfo *results)=0;
    virtual int fillProcessor(gfcImageProcessor &processor)=0;
    virtual void* getPixelPointer()=0;
    virtual void releaseMemory()=0;
    virtual std::vector<std::string> getChannelNames()=0; //this is really the same for all of them, it just helps us remind that we need to fill the channelNames.
    
    gfcGLFrameInfo getFrameInfo();
    
    protected:
    std::vector<std::string> channelNames;
    gfcGLFrameInfo frameInfo;
	boost::condition balanceReadCond;

};

#endif

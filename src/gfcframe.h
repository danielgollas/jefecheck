#ifndef GFCFRAME_H
#define GFCFRAME_H

#include <string.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#ifdef WIN32 
#include <map> 
#else 
#include <map> 
#endif //for metadata
#include <glad/glad.h>
#include <FL/Fl_Box.H>
#include "UIConstants.h"
#include "dpxslice.h"
#include <vector>
#include "gfcStructures.h"
#include "gfcloadparams.h"
#include "gfcimageloader.h"

// #include "gfcimageloadergfl.h" // TODO: replace with OIIO
#include "gfcimageloaderdpx.h"
// #include "gfcimageloaderfil.h" // TODO: replace with OIIO

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/

enum loaders{GFCLOADER_EXR,GFCLOADER_GFL,GFCLOADER_OTHER, GFCLOADER_DPX, GFCLOADER_FIL};

enum frameloaderrors{
	GFCFRAME_LOADERROR_NO_ERROR=0,
	GFCFRAME_LOADERROR_UNKNOWN=1,
GFCFRAME_LOADERROR_NO_MORE_MEMORY=2};

extern std::vector<int> dummyVectorForLut;
extern GLuint defaultTexture;

class gfcRawFrameLoadParams
{
public:
char trackID;
 std::string name;
  int scale;
   int cropX;
    int cropY; int cropW; 
    int cropH;
    int filterType;
    float gamma; 
    std::vector<int> lutList; 
    int compressed; 
    float exposition; 
    float defog; 
    float kneeH; 
    float kneeL; 
    int channel;
    float *gammaLUT;
};

class RawFrame
{
public:
    RawFrame()
    {
        depth=8;
        frameNumber=0;
    }
    ~RawFrame();
    int frameNumber; //the frame number according to the filename
    int indexNumber; //the index where this frame goes in the sequence vector.
    std::vector<GFLC_BITMAP*> bitmap;
    std::string fileName;
    int depth;
    GLint format;
    void *pboPointer;
    bool loadIntoPBO;
    
    GLuint gl_InternalFormat; //GL_RGBA, GL_RGBAF16
    GLuint gl_type; //GL_BYTE, GL_SHORT
    GLuint gl_Format; //GL_RGB, GL_RGBA
    
    int totalH;
    int totalW;
    int bpc;
    char formatDescription[64];
    char compressionDescription[64];
    char formatName[64];
    GLuint generateTexture();
    bool cleanUp();
    int loadFrame(gfcRawFrameLoadParams params);
    bool operator < (const RawFrame& a) const
    {
        return true;
    }
    
    bool loaded;
    bool skipped;
    DpxSlice dpxSlice;
    //bool operator > (const RawFrame& a){return true;}
};

class gfcFrame
{
public:
    gfcFrame()
    {
        sizeX=sizeY=15;
        loaded=false;
        skipped=false;
        theImageLoader=NULL;
        textureID=defaultTexture;
        indexNumber=0;
        forceGFLLoader=false;
    }

   //NOTE: No destructor!
   
    //const gfcFrame &operator=(const gfcFrame &frame);

    gfcImageLoader* theImageLoader;

    GLuint textureID;

    bool loaded;
    bool skipped;
    bool forceGFLLoader;
    int sizeX;
    int sizeY;
    int channels;
    int bpc;
    int originalBitDepth;
    int originalChannels;
    int compressed;
    float scale;
    int quadSizeX;
    int quadSizeY;
    gfcRectangf texCoords;
    
    int indexNumber;
    
    std::string fileName;
    std::string loadErrorString;

    //std::string metaData;
    std::multimap<std::string,std::string> metaData;
    std::string format;
    std::string formatDescription;
    std::string compressionDescription;
    
    std::string getInfoString();
    std::string getExtendedInfoString();
    
    std::multimap<std::string, std::string> getMetadata();
    std::string getMetadataItem(std::string name);

    int whatLoaderToUse(std::string name);
    int loadFrame(gfcLoadParams params);
    int loadFrame();
    std::vector<std::string> getChannelNames();
    GLuint generateTexture();
    void releaseMemory();
    void deleteTexture();
    
    void clearFrame();
    gfcLoadParams savedParams; //this is used when a frame is attepted to be loaded but an error ocurrs. The params are saved in the frame. Each frame knows how to load itself in case we need it.

    private:
    
    std::vector<std::string> channelNames;
   };

#endif

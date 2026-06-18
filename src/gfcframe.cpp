#include "gfcframe.h"


#include <iostream>
#include <sstream>
#include <iomanip>

extern bool mainWindowExists;
extern bool npotTextures;
extern std::mutex gGLMutex;
extern void* gGLContext;
extern bool gResizeTrigger;

int TEST_GLOBAL_loaderToUse=GFCLOADER_DPX;

/****/
//#include "gfcframeslice.h"
#include "gfcimageloaderdpx.h"
// Image loading handled by gfcpixelbuffer.h via gfcframe.h
#include "gfcimageloaderexr.h"

/****/

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

std::string gfcFrame::getInfoString()
{
	std::ostringstream stream;
	
	if ( !loaded )
	{	stream<<fileName<<std::endl;
		stream<<"Frame not loaded: " << loadErrorString<<std::endl;
		
		//	sprintf ( result,"frame not loaded" );
	}
	else
	{
	//	stream<<fileName << " ("<<quadSizeX<<" x "<<this->quadSizeY<<" x "<<originalChannels<<" "<<originalBitDepth<<"bpc"<<")";
			stream<<fileName << " ("<<sizeX<<" x "<<this->sizeY<<" x "<<originalChannels<<" "<<originalBitDepth<<"bpc"<<")";
	//	sprintf ( result,"%s (%ix%ix%i %ibpc)",fileName.c_str(),sizeX,sizeY,channels, bpc );
	}
	return stream.str();
}

//returns additional info when the file has DPX metadata;
std::string gfcFrame::getExtendedInfoString()
{
	std::ostringstream stream;
	
	if ( !loaded )
	{
		stream.str("");
		
	}
	else
	{	
		stream<<"=============================="<<std::endl;
		stream<<"Metadata"<<std::endl;
		stream<<"=============================="<<std::endl;
		if(metaData.size())
		{
			std::multimap<std::string,std::string>::iterator it=metaData.begin();
			std::multimap<std::string,std::string>::iterator endit=metaData.end();
			for(it;it!=endit;it++)
			{
				stream<<"*"<<it->first<<": "<<it->second<<std::endl;
			}
		}
		else{
		stream<<"No Metadata";
		}
	
	}
	return stream.str();
}


int gfcFrame::whatLoaderToUse ( std::string name )
{

	//return TEST_GLOBAL_loaderToUse;
	
	if(forceGFLLoader || name.find(".")==std::string::npos){ //if we are forcing or file has no extension use gfl
		
		return GFCLOADER_GFL;
		
			
	}
	
	if ( strcmp( &name[name.size()-4],".exr" )==0 || strcmp( &name[name.size()-4],".Exr" )==0 || strcmp( &name[name.size()-4],".EXR" )==0) //is it exr?
	{
		return GFCLOADER_EXR;
	}
	else
	{
		if ( strcmp ( &name[name.size()-4],".dpx" ) ==0 || strcmp ( &name[name.size()-4],".DPX" ) ==0  || strcmp ( &name[name.size()-4],".Dpx" ) ==0  || strcmp ( &name[name.size()-4],".dpX" ) ==0)  //is it dpx?
		{
			return GFCLOADER_DPX;
		}
		else
		{
			
			return GFCLOADER_GFL;
		}
	}
	
	
};

int gfcFrame::loadFrame()
{
	return loadFrame(savedParams);
}

int gfcFrame::loadFrame ( gfcLoadParams params )
{
	savedParams=params;
	//printf ( "Inside loadFrame\n" );
	//1. Detect the file type and instance a correct imageLoader
	forceGFLLoader=params.forceGFLLoading;
	switch ( whatLoaderToUse ( params.fileName ) )
	{

		case GFCLOADER_DPX:
			//theImageLoader=new gfcImageLoaderGFL();
//			printf("Unsing DPX\n");
			theImageLoader=new gfcImageLoaderDPX();
			break;

		case GFCLOADER_EXR:
			theImageLoader=new gfcImageLoaderOIIO();
			break;

		case GFCLOADER_GFL:
		case GFCLOADER_FIL:
			theImageLoader=new gfcImageLoaderOIIO();
			break;
	}

	loaded=false;
	//2. Load image using imageLoader and params
	int result=theImageLoader->load ( params );
	if ( result!=0 )
	{
		loaded=false;
		loadErrorString=theImageLoader->loadErrorString;
		fileName=params.fileName;
		return result;
	}
	else
	{
		loaded=true;
	}
	
	//4. Store relevant information in attributes.
	sizeX=theImageLoader->sizeX;
	sizeY=theImageLoader->sizeY;
	bpc=theImageLoader->bitDepth;
	originalBitDepth=theImageLoader->originalBitDepth;
	
	texCoords=theImageLoader->texCoords;
	
	quadSizeX=theImageLoader->quadSizeX;
	quadSizeY=theImageLoader->quadSizeY;
	originalChannels=theImageLoader->originalNumOfComponents;
	channels=theImageLoader->numOfComponents;
	fileName=params.fileName;
	channelNames=theImageLoader->getChannelNames();
	scale=params.scale;
	format=theImageLoader->format;
	formatDescription=theImageLoader->formatDescription;
	compressionDescription=theImageLoader->compressionDescription;
	metaData=theImageLoader->metaData;
	compressed=params.compressed;
	//printf("gfcFrame loaded %s: %ix%ix%ix%i\n",fileName.c_str(),sizeX,sizeY,channels,bpc);
	
	//4. Return
	return result;
}

std::vector< std::string > gfcFrame::getChannelNames()
{
	return channelNames;
}

GLuint gfcFrame::generateTexture(bool captureThumbnail)
{
	//printf ( " Inside gfcFrame::generateTexture\n" );
	//0. Clear previous texture no matter what 
	plateManager.setChanged();
	deleteTexture();
	
	//1. Find out glTexImage params from the image loaders frameInfo
	if(!loaded)
	{
		printf("gfcFrame::generateTexture: Frame not loaded! not generating texture\n");
		return -1;
	}
	else
	{
		//printf("gfcFrame::generateTexture: Frame IS loaded! generating texture (%s)\n",this->fileName.c_str());
	}
	
	gfcGLFrameInfo info=theImageLoader->getFrameInfo();

	
	//2. Create the texture according to info
	/*
	gfcTimer timer("texGenTimer");
	timer.start();
	*/
	glEnable(info.target);
	glGenTextures ( 1,&textureID );
	//printf("Texture ID: %i\n",textureID);
	glBindTexture(info.target, textureID);
	glTexParameterf ( info.target, GL_TEXTURE_WRAP_S, GL_CLAMP );
	glTexParameterf ( info.target, GL_TEXTURE_WRAP_T, GL_CLAMP );
	glTexParameterf ( info.target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameterf ( info.target, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexEnvf ( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
	while (glGetError() != GL_NO_ERROR) {} // clear stale errors
	glTexImage2D ( info.target,0,info.internalFormat,sizeX,sizeY,0,info.format,info.dataType,info.dataPointer);
	GLenum glErr = glGetError();
	if (glErr != GL_NO_ERROR) printf("generateTexture: glTexImage2D error: 0x%x\n", glErr);
	/*
	glFinish();
	timer.stop();
	timer.print();
	*/

	// Capture a downsampled RGBA8 thumbnail for the timeline filmstrip
	// while the decoder's CPU buffer is still alive (releaseMemory frees
	// it just below). Source is BGRA, 8- or 16-bit per info.dataType.
	if (captureThumbnail && info.dataPointer && sizeX > 0 && sizeY > 0
	    && (info.dataType == GL_UNSIGNED_BYTE || info.dataType == GL_UNSIGNED_SHORT)) {
		const int srcW = sizeX, srcH = sizeY;
		const int dstH = 48;
		int dstW = (int)((double)dstH * srcW / srcH + 0.5);
		if (dstW < 1) dstW = 1;
		if (dstW > 96) dstW = 96;
		thumbnail.w = dstW;
		thumbnail.h = dstH;
		thumbnail.rgba.assign((size_t)dstW * dstH * 4, 0);
		const bool sixteen = (info.dataType == GL_UNSIGNED_SHORT);
		const unsigned char*  s8  = (const unsigned char*)info.dataPointer;
		const unsigned short* s16 = (const unsigned short*)info.dataPointer;
		for (int y = 0; y < dstH; ++y) {
			const int sy = y * srcH / dstH;
			for (int x = 0; x < dstW; ++x) {
				const int sx = x * srcW / dstW;
				const size_t si = ((size_t)sy * srcW + sx) * 4; // BGRA, 4 comps
				unsigned char b, g, r, a;
				if (sixteen) {
					b = (unsigned char)(s16[si+0] >> 8);
					g = (unsigned char)(s16[si+1] >> 8);
					r = (unsigned char)(s16[si+2] >> 8);
					a = (unsigned char)(s16[si+3] >> 8);
				} else {
					b = s8[si+0]; g = s8[si+1]; r = s8[si+2]; a = s8[si+3];
				}
				const size_t di = ((size_t)y * dstW + x) * 4;
				thumbnail.rgba[di+0] = r;  // BGRA -> RGBA
				thumbnail.rgba[di+1] = g;
				thumbnail.rgba[di+2] = b;
				thumbnail.rgba[di+3] = a;
			}
		}
	}

	//3. check for glErrors.
	//glPrintError();
	//4. Release imageLoader memory.
	theImageLoader->releaseMemory();
	if(theImageLoader) 
		delete theImageLoader;
	theImageLoader=NULL;

	//5. return
	return textureID;

}

bool RawFrame::cleanUp()
{//OLD STUFF***

	return true;
}

RawFrame::~RawFrame()
{//OLD STUFF***
}

int RawFrame::loadFrame ( gfcRawFrameLoadParams params )
{//OLD STUFF***


	return 0;
}

void gfcFrame::releaseMemory()
{
	if(theImageLoader)
		theImageLoader->releaseMemory();
}

/*!
    \fn gfcFrame::deleteTexture
 */
void gfcFrame::deleteTexture()
{
    glDeleteTextures(1,&textureID);
}

void gfcFrame::clearFrame()
{
	deleteTexture();
	releaseMemory();
	thumbnail.w = 0;
	thumbnail.h = 0;
	thumbnail.rgba.clear();
	thumbnail.rgba.shrink_to_fit();
	sizeX=sizeY=15;
        loaded=false;
        if(theImageLoader)
        	delete theImageLoader;
        theImageLoader=NULL;
        textureID=defaultTexture;
        indexNumber=0;

		this->fileName.clear();
}

std::string gfcFrame::getMetadataItem(std::string name)
{
	std::multimap<std::string, std::string>::iterator result=metaData.find(name);
	if(result!=metaData.end())
	return metaData.find(name)->second;
	else
	return "";
}







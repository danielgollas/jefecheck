// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfcframe.h"
#include "gfcYuvConverter.h"
#include "gfcMediaKinds.h"


#include <atomic>
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

	// Movies first: forcing the image loader cannot read a .mov anyway.
	if (gfcIsMovieFile(name))
		return GFCLOADER_MOVIE;

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

		case GFCLOADER_MOVIE:
			theImageLoader=new gfcImageLoaderMovie();
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

	// Every loaded frame becomes a texture (generateTexture), so the pixels
	// go to their upload source now, still on the decode thread.
	theImageLoader->prepareUpload();
	
	//4. Return
	return result;
}

std::vector< std::string > gfcFrame::getChannelNames()
{
	return channelNames;
}


namespace {
std::atomic<long> g_liveTextures{0};
}  // namespace

long gfcFrame::liveTextures()
{
	return g_liveTextures.load(std::memory_order_relaxed);
}

namespace {
// Bytes per texel of an internal format, for gfcFrame::textureBytes. DXT1 is
// half a byte; formats not listed count as 4 (the drivers' usual RGBA8 storage).
size_t textureBytesFor(GLint internalFormat, int w, int h)
{
	const size_t px = (size_t)(w > 0 ? w : 0) * (size_t)(h > 0 ? h : 0);
	switch (internalFormat) {
	case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
	case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT: return px / 2;
	case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
	case GL_LUMINANCE8:
	case GL_LUMINANCE4:  return px;
	case GL_RGBA4:
	case GL_RGB4:
	case GL_LUMINANCE16F_ARB: return px * 2;
	case GL_RGBA16:
	case GL_RGBA16F_ARB:
	case GL_RGB16F_ARB:  return px * 8;
	case GL_RGBA32F_ARB:
	case GL_RGB32F_ARB:  return px * 16;
	default:             return px * 4;
	}
}
}  // namespace

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
	gfcUpload::service();   // recycle staging buffers, grow the pool on demand
	glEnable(info.target);
	if (info.planarYuv)
	{
		// Movies: YUV planes -> RGB on the GPU with the file's matrix/range,
		// straight into the ordinary resident RGBA / RGBA16F texture, so the
		// super-shader, FX, render read-back and thumbnails all see one RGB.
		textureID = gfcYuvConvertToTexture(info, info.srcX, info.srcY, info.srcW, info.srcH,
		                                   sizeX, sizeY, info.internalFormat);
		if (textureID == 0)
		{
			// GPU pass unavailable: same maths on the CPU, uploaded as RGBA8.
			printf("generateTexture: GPU YUV conversion failed, using the CPU path for %s\n", fileName.c_str());
			std::vector<unsigned char> rgba;
			glGenTextures ( 1,&textureID );
			glBindTexture(info.target, textureID);
			glTexParameterf ( info.target, GL_TEXTURE_WRAP_S, GL_CLAMP );
			glTexParameterf ( info.target, GL_TEXTURE_WRAP_T, GL_CLAMP );
			glTexParameterf ( info.target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
			glTexParameterf ( info.target, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			if (gfcYuvConvertToRGBA8(info, sizeX, sizeY, rgba))
				glTexImage2D ( info.target,0,info.internalFormat,sizeX,sizeY,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
		}
		else
		{
			glBindTexture(info.target, textureID);
		}
		glTexEnvf ( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
		// The conversion pass owns this texture's storage: never pool it.
		textureTarget = info.target;
		textureFormat = info.internalFormat;
		textureW = textureH = 0;
		textureBytes = textureID ? textureBytesFor(info.internalFormat, sizeX, sizeY) : 0;

		// Filmstrip thumbnail from the planes with the same maths (nearest
		// on the same grid as the BGRA path below).
		if (captureThumbnail && sizeX > 0 && sizeY > 0) {
			const int dstH = 48;
			int dstW = (int)((double)dstH * sizeX / sizeY + 0.5);
			if (dstW < 1) dstW = 1;
			if (dstW > 96) dstW = 96;
			if (gfcYuvConvertToRGBA8(info, dstW, dstH, thumbnail.rgba)) {
				thumbnail.w = dstW;
				thumbnail.h = dstH;
			}
		}
	}
	else
	{
		// Refill a pooled texture of the same size and format when one is waiting
		// (IOSurface textures get their storage from the surface instead).
		const GLuint reused = info.ioSurface ? 0
			: gfcUpload::takeTexture(info.target, info.internalFormat, sizeX, sizeY);
		if (reused) {
			textureID = reused;   // bound, parameters already set
		} else {
			glGenTextures ( 1,&textureID );
			glBindTexture(info.target, textureID);
			glTexParameterf ( info.target, GL_TEXTURE_WRAP_S, GL_CLAMP );
			glTexParameterf ( info.target, GL_TEXTURE_WRAP_T, GL_CLAMP );
			glTexParameterf ( info.target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
			glTexParameterf ( info.target, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			gfcUpload::noteTextureCreated();
		}
		glTexEnvf ( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
		while (glGetError() != GL_NO_ERROR) {} // clear stale errors
		// macOS: the decode thread left the pixels in an IOSurface; make it the
		// texture's storage (no copy). Otherwise, or if the driver refuses the
		// layout, upload: from the staging buffer the decode thread filled when
		// there is one (the GPU pulls it, no CPU copy here), else from the bitmap.
		const bool bound = info.ioSurface && info.target == GL_TEXTURE_RECTANGLE_ARB
			&& gfcSurface::bindToTexture(info.ioSurface, sizeX, sizeY, info.ioSurfaceLayout);
		bool uploaded = bound;
		if (bound) {
			ioSurface = info.ioSurface;   // the loader releases its own reference below
			gfcSurface::retain(ioSurface);
		} else {
			const bool staged = info.staging
				&& gfcUpload::upload(info.staging, info.target, info.internalFormat, sizeX, sizeY,
				                     info.format, info.dataType, !reused);
			// The loader keeps no CPU copy once it filled a surface or a
			// staging buffer: a surface the context would not bind is
			// uploaded from the surface's own memory instead.
			const void* pixels = info.dataPointer;
			size_t surfacePitch = 0;
			const void* surfacePixels = (!staged && !pixels && info.ioSurface)
				? gfcSurface::lockForReading(info.ioSurface, &surfacePitch) : nullptr;
			if (surfacePixels) {
				pixels = surfacePixels;
				const size_t pixelBytes = info.dataType == GL_UNSIGNED_BYTE ? 4 : 8;
				glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(surfacePitch / pixelBytes));
			}
			// Nothing to upload from (a staged upload that failed after the
			// loader freed its copy). A texture from the pool still holds the
			// frame it last showed: blank it rather than show the wrong frame.
			std::vector<unsigned char> blank;
			if (!staged && !pixels) {
				printf("generateTexture: no pixels to upload for %s\n", fileName.c_str());
				if (reused) {
					blank.assign((size_t)sizeX * sizeY * (info.dataType == GL_UNSIGNED_BYTE ? 4 : 8), 0);
					pixels = blank.data();
				}
			}
			if (!staged && !pixels) {
			} else if (!staged && reused)
				glTexSubImage2D ( info.target,0,0,0,sizeX,sizeY,info.format,info.dataType,pixels);
			else if (!staged)
				glTexImage2D ( info.target,0,info.internalFormat,sizeX,sizeY,0,info.format,info.dataType,pixels);
			if (surfacePixels) {
				glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
				gfcSurface::unlockForReading(info.ioSurface);
			}
			GLenum glErr = glGetError();
			if (glErr != GL_NO_ERROR) printf("generateTexture: texture upload error: 0x%x\n", glErr);
			uploaded = glErr == GL_NO_ERROR && (staged || pixels);
		}
		// Remember what the texture holds so deleteTexture can pool it. A texture
		// whose upload failed may have no storage: never pool that one.
		textureTarget = info.target;
		textureFormat = info.internalFormat;
		textureW = (uploaded && !bound) ? sizeX : 0;
		textureH = (uploaded && !bound) ? sizeY : 0;
		textureBytes = uploaded ? textureBytesFor(info.internalFormat, sizeX, sizeY) : 0;
		/*
		glFinish();
		timer.stop();
		timer.print();
		*/

		// Filmstrip thumbnail: made by the loader on the decode thread when it
		// handed the pixels off, else from the CPU copy still held here.
		if (captureThumbnail) {
			if (!info.thumbnailRGBA.empty()) {
				thumbnail.rgba = info.thumbnailRGBA;
				thumbnail.w = info.thumbnailW;
				thumbnail.h = info.thumbnailH;
			} else {
				gfcMakeThumbnail(info.dataPointer, sizeX, sizeY, (size_t)sizeX * 4
				                     * (info.dataType == GL_UNSIGNED_BYTE ? 1 : 2),
				                 info.format, info.dataType, thumbnail.rgba, thumbnail.w, thumbnail.h);
			}
		}
	}

	if (textureID != defaultTexture && textureID != 0) {
		countedTexture = true;
		g_liveTextures.fetch_add(1, std::memory_order_relaxed);
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
    if (textureID != defaultTexture) {
        if (textureW > 0 && textureH > 0)
            gfcUpload::giveTexture(textureID, textureTarget, textureFormat, textureW, textureH);
        else
            glDeleteTextures(1,&textureID);
    }
    if (countedTexture) {
        countedTexture=false;
        g_liveTextures.fetch_sub(1, std::memory_order_relaxed);
    }
    gfcSurface::release(ioSurface);   // only after the texture using it is gone
    ioSurface=nullptr;
    // Forget the name, so a second call never frees or pools it twice.
    textureID=defaultTexture;
    textureW=textureH=0;
    textureBytes=0;
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







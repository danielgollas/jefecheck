#ifndef GFCIMAGELOADEREXR_H
#define GFCIMAGELOADEREXR_H

#include "gfcimageloader.h"

#include <OpenEXR/ImfRgba.h>
#include <OpenEXR/ImfArray.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfArray.h>
#include <OpenEXR/ImfChannelList.h>
/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/
class gfcImageLoaderEXR : public gfcImageLoader
{
public:
    gfcImageLoaderEXR();

    ~gfcImageLoaderEXR();

    virtual int fillProcessor(gfcImageProcessor& processor);
    virtual int load(gfcLoadParams params);
    virtual int peek(gfcLoadParams params, gfcPeekInfo* results);
    virtual void* getPixelPointer();
    virtual std::vector<std::string> getChannelNames();
     virtual void releaseMemory();
     
     private:

		 int w;
		 int h;
		 int x;
		 int y;
		 int dw;
		 int dh;
		 int dx;
		 int dy;
		 float pixelAspectRatio;

     	GFL_BITMAP *theBitmap;
     	int loadHALF;
     	Imf::Array<Imf::Rgba> *pixels;
     	Imf::Array<half> *halfPixels;
		Imf::Array<float> *floatPixels;
     	void readMetaData(const Imf::Header &header);
		void resamplePixels(int originalW, int originalH, int scale, int &newWidth, int &newHeight);
		void resampleHalfPixels( int originalW, int originalH, int scale, int &newWidth, int &newHeight);
		void resampleFloatPixels( int originalW, int originalH, int scale, int &newWidth, int &newHeight);
		
		void copyPixelsToDisplayWindow(int startX, int startY, int copyWidth, int copyHeigth, int preLineOffset, int postLineOffset);
		void copyHalfPixelsToDisplayWindow(int startX, int startY, int copyWidth, int copyHeigth, int preLineOffset, int postLineOffset);
		void copyFloatPixelsToDisplayWindow(int startX, int startY, int copyWidth, int copyHeigth, int preLineOffset, int postLineOffset);
		int layerHasRGBA(Imf::ChannelList::ConstIterator start, Imf::ChannelList::ConstIterator end);
		std::vector<std::string> getChannelsInLayer(const Imf::ChannelList &channels, std::string layerName); //this will return all the channels in a layer, if they contain .R, .G, .B and optionally .A they will be returned in the correct order, not alphabetically.
};

#endif

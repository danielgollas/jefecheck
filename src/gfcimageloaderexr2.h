#ifndef GFCIMAGELOADEREXR2_H
#define GFCIMAGELOADEREXR2_H

#include "gfcimageloader.h"

#include <OpenEXR/ImfRgba.h>
#include <OpenEXR/ImfArray.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfArray.h>
#include <OpenEXR/ImfChannelList.h>
/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/

class gfcImageLoaderEXR2 : public gfcImageLoader
{
public:
    gfcImageLoaderEXR2();

    ~gfcImageLoaderEXR2();

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

		Imf::InputFile *file;
     	GFL_BITMAP *theBitmap;
     	//int loadHALF;
		Imf::PixelType channelType;
		std::set<std::string> layerNames;
		const ChannelList &channels;
     	Imf::Array<Imf::Rgba> *pixels;
		Imf::Rgba
		Imf::Array<unsigned int> *uintPixels;
     	Imf::Array<half> *halfPixels;
		Imf::Array<float> *floatPixels;
     	void readMetaData(const Imf::Header &header);
		int loadChannelNames();
		int layerHasRGBA(Imf::ChannelList::ConstIterator start, Imf::ChannelList::ConstIterator end);
		std::vector<std::string> getChannelsInLayer(const Imf::ChannelList &channels, std::string layerName); //this will return all the channels in a layer, if they contain .R, .G, .B and optionally .A they will be returned in the correct order, not alphabetically.
};


char exrCompressionDescriptions[8][60]={"No Compression","Run Lenght Encoding (RLE)","zlib, one scan line at a time compression","zlib, in blocks of 16 scan lines","piz-based wavelet","PXR24 (lossy )","B44,fixed rate (lossy)","B44 (lossy)"};


template<class T>
void EXR_resamplePixels(Imf::Array<T> *original, int originalW, int originalH, int scale, int &newWidth,int &newHeight){
	Imf::Array<T> resizedPixels;
	int N = 100/scale; //sample every N pixels 

	newWidth=originalW/(float)N;
	newHeight=originalH/(float)N;
	resizedPixels.resizeErase(newWidth*newHeight);
	//printf("newWidth= %i new Height= %i originalW= %i originalH=%i\n",newWidth, newHeight, originalW, originalH);
	//resample
	for (int y=0; y<newHeight; ++y)
	{
		for (int x=0;x<newWidth;++x)
		{
			int destination= y*newWidth+x;
			int origin = (y * N)*originalW+(x * N);
			//printf("(x=%i y=%i) Origin=%i destination=%i\n",x,y,origin, destination);

			resizedPixels[destination] = (*original)[origin];
			//*resizedPixels[y][x]=pixels[y * N][x * N];
		}
	}

	//copy to the original array
	original->resizeErase(newWidth*newHeight);
	for (int i=newHeight*newWidth-1;i>=0; --i)
	{
		(*original)[i]=resizedPixels[i];
	}
}

/*
void gfcImageLoaderEXR2::resampleHalfPixels(int originalW, int originalH, int scale, int &newWidth,int &newHeight){
	Array<half> resizedPixels;
	int N = 100/scale; //sample every N pixels 

	newWidth=originalW/(float)N;
	newHeight=originalH/(float)N;
	resizedPixels.resizeErase(newWidth*newHeight);
	//printf("newWidth= %i new Height= %i originalW= %i originalH=%i\n",newWidth, newHeight, originalW, originalH);
	//resample
	for (int y=0; y<newHeight; ++y)
	{
		for (int x=0;x<newWidth;++x)
		{
			int destination= y*newWidth+x;
			int origin = (y * N)*originalW+(x * N);
			//printf("(x=%i y=%i) Origin=%i destination=%i\n",x,y,origin, destination);

			resizedPixels[destination] = (*halfPixels)[origin];
			//*resizedPixels[y][x]=pixels[y * N][x * N];
		}
	}

	//copy to the original array
	halfPixels->resizeErase(newWidth*newHeight);
	for (int i=newHeight*newWidth-1;i>=0; --i)
	{
		(*halfPixels)[i]=resizedPixels[i];
	}
}


void gfcImageLoaderEXR2::resampleFloatPixels(int originalW, int originalH, int scale, int &newWidth,int &newHeight){
	Array<float> resizedPixels;
	int N = 100/scale; //sample every N pixels 

	newWidth=originalW/(float)N;
	newHeight=originalH/(float)N;
	resizedPixels.resizeErase(newWidth*newHeight);
	//printf("newWidth= %i new Height= %i originalW= %i originalH=%i\n",newWidth, newHeight, originalW, originalH);
	//resample
	for (int y=0; y<newHeight; ++y)
	{
		for (int x=0;x<newWidth;++x)
		{
			int destination= y*newWidth+x;
			int origin = (y * N)*originalW+(x * N);
			//printf("(x=%i y=%i) Origin=%i destination=%i\n",x,y,origin, destination);

			resizedPixels[destination] = (*floatPixels)[origin];
			//*resizedPixels[y][x]=pixels[y * N][x * N];
		}
	}

	//copy to the original array
	floatPixels->resizeErase(newWidth*newHeight);
	for (int i=newHeight*newWidth-1;i>=0; --i)
	{
		(*floatPixels)[i]=resizedPixels[i];
	}
}

void gfcImageLoaderEXR2::resamplePixels(int originalW, int originalH, int scale, int &newWidth,int &newHeight)
{
	Array<Rgba> resizedPixels;
	int N = 100/scale; //sample every N pixels 

	newWidth=originalW/N;
	newHeight=originalH/N;
	resizedPixels.resizeErase(newWidth*newHeight);
	//printf("newWidth= %i new Height= %i originalW= %i originalH=%i\n",newWidth, newHeight, originalW, originalH);
	//resample
	for (int y=0; y<newHeight; ++y)
	{
		for (int x=0;x<newWidth;++x)
		{
			int destination= y*newWidth+x;
			int origin = (y * N)*originalW+(x * N);
			//printf("(x=%i y=%i) Origin=%i destination=%i\n",x,y,origin, destination);
			
			resizedPixels[destination] = (*pixels)[origin];
			//*resizedPixels[y][x]=pixels[y * N][x * N];
		}
	}
	
	//copy to the original array
	pixels->resizeErase(newWidth*newHeight);
	for (int i=newHeight*newWidth-1;i>=0; --i)
	{
			(*pixels)[i]=resizedPixels[i];
	}

	

}
*/


template<class T> void EXR_copyToDisplayWindow(Imf::Array<T> *originaloriginal, int startX, int startY, int copyWidth, int copyHeigth, int preLineOffset, int postLineOffset){

	Imf::Array<T> newPixels;
	
	//copy pixels from the originalPixels array into the new pixels taking only the ones in the display window.
	newPixels.resizeErase(w*h);
	unsigned int counter=max((dy),0)*w;
	for (int i=0;i<copyHeigth;i++) 
	{
		int lineOffset=(startY+i)*dw+startX;
		counter+=preLineOffset;
		for (int j=0;j<copyWidth;j++) 
		{
			newPixels[counter++]=(*original)[lineOffset+j];
		}
		counter+=postLineOffset;
	}
	
	//copy to the original array
	original->resizeErase(w*h);
	for (int i=w*h-1;i>=0; --i)
	{
		(*original)[i]=newPixels[i];
	}

}

#endif

#ifndef GFCIMAGELOADERDPX_H
#define GFCIMAGELOADERDPX_H

#include "gfcimageloader.h"
#include "gfcStructures.h"
#include "gfcdpxstructures.h"

#include <stdio.h>
#include <iostream>
#include <fstream>
/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/
class gfcImageLoaderDPX : public gfcImageLoader
{
public:
    gfcImageLoaderDPX();

    ~gfcImageLoaderDPX();

    virtual int fillProcessor(gfcImageProcessor& processor);
    virtual int load(gfcLoadParams params);
    virtual int peek(gfcLoadParams params, gfcPeekInfo* results);
    virtual void* getPixelPointer();
    virtual void releaseMemory();
    virtual std::vector<std::string> getChannelNames();
    
    private:

    	 
    	
    	GFL_BITMAP *theBitmap;
    	
    	void *pixelPointer;
    	pixelRGB16Bit* rgb16pixels;
    	pixelRGB8Bit* rgb8pixels;
    	pixelRGBA16Bit* rgba16pixels;
    	pixelRGBA8Bit* rgba8pixels;
    	
    	
    	std::string timeCodeString;
    	gfcLoadParams params; ///internal parameters accesible by all threads and functions
    	
    	//dpx headers
    	dpx_file_information fileInfo;
	dpx_image_information imageInfo;
	dpx_image_orientation imageOrientation;
	dpx_motion_picture_film_header motionHeader;
	dpx_television_header tvHeader;
	dpx_UserInfo userInfo;
	
    	
    	///dpx reading and decoding variables
    	unsigned int bytePerRow; ///how many bytes this image requires per scanline
    	unsigned int samplesPerPixel; ///how many samples a pixel has (from imageInfo.image_element[0].descriptor)
    	
    	///reading functions and variables
    	int readHeader();
    	int readSlice(long startOffset, long numLines, long bitmapStorageStartIndex);
    	///extracts the samples from each raw frame
    	void ReadRowSamples(const unsigned char *scanline,
                           const unsigned int samples_per_row,
                           const unsigned int bits_per_sample,
                           const unsigned int packing_method,
                           const unsigned int endian_type,
                           const bool swap_word_datums,
                           unsigned int *samples); 

        ///Read A or B(deprecated) method Filled 10 bit RGB scanline into the pixelIndexStart
        void ReadRowSamplesRGBFilled10(const unsigned char *scanline,const int pixelIndexStart);
        ///Read A or B(deprecated) method packed 10 bit RGB scanline into the pixelIndexStart
        void ReadRowSamplesRGBPacked10(const unsigned char *scanline,const int pixelIndexStart);
        void ReadRowSamplesRGBAPacked10(const unsigned char *scanline,const int pixelIndexStart);
        
         ///Read A or B(deprecated) method Filled 10 bit RGBA scanline into the pixelIndexStart
        void ReadRowSamplesRGBAFilled10(const unsigned char *scanline,const int pixelIndexStart);
        ///Read A or B(deprecated) method Filled 10 bit RGB scanline into the pixelIndexStart
        void ReadRowSamplesRGB8(const unsigned char *scanline,const int pixelIndexStart);
        void ReadRowSamplesRGBA8(const unsigned char *scanline,const int pixelIndexStart);

        /***VARIABLES USED TO DETERMINE WHAT READROWSAMPLE TO USE AND USED IN THOSE FUNCTIONS TOO*****/
        unsigned int packing_method;
        unsigned int bytesPerRow;
        int endian_type;
        int bits_per_sample;
        unsigned int samples_per_row;
        bool swap_word_datums;
        unsigned int descriptor;
        int requestedBitSize;
        /************************************************************/
        
        unsigned int getSamplesPerPixel();
        size_t DPXRowBytes(const unsigned long rows,
                          const unsigned int samples_per_row,
                          const unsigned int bits_per_sample,
                          const unsigned int packing_method);
    	//bool swab;
    	std::string SMPTEBitsToString(const unsigned int value);
};

#endif

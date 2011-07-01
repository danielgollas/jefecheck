#include "gfcimagesaver_exr.h"

#include "glew.h"

using namespace Imath;
using namespace Imf;
using namespace std;

gfcImageSaver_EXR::gfcImageSaver_EXR()
        : gfcImageSaver() {
    pixelFormat=GL_HALF_FLOAT_ARB;
    requestFormat=GL_RGBA;
    pixels=NULL;
}


gfcImageSaver_EXR::~gfcImageSaver_EXR() {
}


int gfcImageSaver_EXR::save(std::string filename) {
    pixels=new Rgba[params.sizeX * params.sizeY];
    
    int pixelCounter=0;
    int halfCounter=0;
    //printf("Converting to half\n");
    for ( int i=0;i<params.sizeY ;i++ ) { //iterates rows
    	int lastRowMinusCurrentRow=(params.sizeY-i-1)*params.sizeX;
    	int rowTimesRowSize=i*params.sizeX;
        for ( int j=0;j<params.sizeX ;j++ ) { //iterates columns
        	pixelCounter=rowTimesRowSize+j; //row*rowSize + currentColumn
        	int reversePixelCounter=lastRowMinusCurrentRow+j; //(last row-row)*rowSize+currentColumn
        	
        	//printf("reversePixelCounter %i\npixelCounter %i\n",reversePixelCounter, pixelCounter);
        	
        	//write each 4 float component into the Rgba structure
        	/*pixels[pixelCounter].r=floatPixels[halfCounter++];
        	pixels[pixelCounter].g=floatPixels[halfCounter++];
        	pixels[pixelCounter].b=floatPixels[halfCounter++];
        	pixels[pixelCounter].a=floatPixels[halfCounter++];*/
        	
        	pixels[pixelCounter]=floatPixels[reversePixelCounter];
        }
    }

    RgbaOutputFile file(params.filename.c_str(),
                        params.sizeX,
                        params.sizeY,
                        WRITE_RGBA,
                        1,
                        Imath::V2f (0, 0),
                        1,
                        INCREASING_Y, //for some reason we get the buffer inverted.
                        (Imf::Compression)params.exrCompression,
                        globalThreadCount()
                       );
    file.setFrameBuffer(pixels, 1, params.sizeX);
    file.writePixels(params.sizeY);

    if (pixels) {
        delete [] pixels;
        pixels=NULL;
    }

	if (floatPixels){
		delete [] floatPixels;
		floatPixels=NULL;

	}
	return 0;
}

void gfcImageSaver_EXR::freeResources() {
    if (pixels) {
        delete [] pixels;
        pixels=NULL;
    }
}

void* gfcImageSaver_EXR::getPixelPointer() {

    /*pixels=new Rgba [params.sizeX * params.sizeY ];

    if (!pixels){
        errorString="Could not allocate enough pixel memory";
        pixels=NULL;
        }

    return pixels;*/

    floatPixels=new Rgba [params.sizeX * params.sizeY];

    if (!floatPixels) {
        errorString="Could not allocate enough pixel memory";
        floatPixels=NULL;
    }

    return floatPixels;
}


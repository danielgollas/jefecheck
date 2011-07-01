#include "gfcimagesaver_gfl.h"

#include "glew.h"
#include "gflC.h"
#include "gflCLoadParams.h"
#include "gflCBitmap.h"

gfcImageSaver_GFL::gfcImageSaver_GFL()
        : gfcImageSaver() {
    pixelFormat=GL_UNSIGNED_BYTE;
    requestFormat=GL_RGBA;
    pixels=NULL;
    
}


gfcImageSaver_GFL::~gfcImageSaver_GFL() {
}


int gfcImageSaver_GFL::getGLFormat() {
    return requestFormat;
}

int gfcImageSaver_GFL::getGLPixelFormat() {
    return pixelFormat;
}

int gfcImageSaver_GFL::save(std::string filename) {
    //1. allocate bitmap and convert pixels into GFL bitmap
    printf ( "Rendering %s\n",params.filename.c_str() );
    GFLC_SAVE_PARAMS saveParams;
    GFLC_BITMAP bitmap ( GFL_RGBA,params.sizeX,params.sizeY );
    int byteCounter=0;

    //printf ( "Setting Pixels %ix%i pixels...\n",bitmap.getHeight(),bitmap.getWidth() );
   // rw.progress->copy_label ( "Preparing to save image" );
   // Fl::check();
    for ( int iR=bitmap.getHeight()-1; iR>=0; iR-- ) {
        for ( int jR=0; jR<bitmap.getWidth(); ++jR ) {
            //printf("Data R: %i,  G: %i,  B: %i,  A: %i\n", (int)theData[byteCounter], (int)theData[byteCounter+1], (int)theData[byteCounter+2], (int)theData[byteCounter+3]);
            bitmap.setPixel ( jR,iR, GFLC_COLOR ( ( int ) pixels[byteCounter], ( int ) pixels[byteCounter+1], ( int ) pixels[byteCounter+2], ( int ) pixels[byteCounter+3] ) );
            //printf("Setting pixel %i,%i\n",iR, jR);
            //bitmap.setPixel(jR, iR, GFLC_COLOR(255,255,0));
            byteCounter+=4;
        }
    }

    if ( params.scale!=1.0 ) {
        //rw.progress->copy_label ( "Scaling Image" );
        //Fl::check();


        bitmap.resize ((int)(params.sizeX*params.scale),(int)(params.sizeY*params.scale),GFL_RESIZE_BILINEAR );

        printf ( "Bitmap final size: %ix%i\n",bitmap.getWidth(),bitmap.getHeight() );
    }

    
    GFLC_FORMAT format ( params.formatString.c_str() );
    saveParams.setFormat ( format );
    saveParams.setQuality ( params.jpegQuality );
    saveParams.setProgressive ( params.jpegProgressive );


    saveParams.setCompressionLevel ( params.pngQuality );

    switch ( params.tiffCompression ) {
    case 0:
        saveParams.setCompression ( GFL_NO_COMPRESSION );
        break;

    case 1:
        saveParams.setCompression ( GFL_LZW );
        break;

    }

    GFL_ERROR error=bitmap.saveIntoFile ( params.filename.c_str(),saveParams );//*/

    if ( error!=GFL_NO_ERROR ) {
        printf ( "Error writing file: %s\n",gflGetErrorString ( error ) );
        errorString="Error writing file: ";
        errorString+=gflGetErrorString ( error );
    }

    //printf("Saved!\n");
    /*rw.progress->label ( "File Saved!" );
    Fl::check();*/
    if(pixels){
    delete [] pixels;
    pixels=NULL;
    }
	return 0;
}

void* gfcImageSaver_GFL::getPixelPointer() {
    //1. Allocate pixels from params
   
    pixels=new unsigned char [params.sizeX * params.sizeY *4];
    if (!pixels){
        errorString="Could not allocate enough pixel memory";
        pixels=NULL;
        }

    return pixels;

}


void gfcImageSaver_GFL::freeResources()
{
    if(pixels){
    delete [] pixels;
    pixels=NULL;
    }
}


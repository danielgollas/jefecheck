#include "gfcimageloaderdpx.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include "gfcStructures.h"
#include <assert.h>
#include <math.h>
#include "gfcframe.h"
#include "gfctrackmanager.h"
//#include <FL/Fl_ask.H>

#include <boost/thread/thread.hpp>
#include <boost/bind.hpp>

extern gfcTrackManager trackManager;

gfcImageLoaderDPX::gfcImageLoaderDPX() {
}


gfcImageLoaderDPX::~gfcImageLoaderDPX() {
}


int gfcImageLoaderDPX::fillProcessor(gfcImageProcessor& processor) {
    return 0;
}

int gfcImageLoaderDPX::load(gfcLoadParams pparams) {
    params=pparams;

    //printf("DPX load\n");
    //read the header
    int readHeaderResult=readHeader();
    if (readHeaderResult==1) {
    	loadErrorString="Could not open file";
        //return 1;
    }
    
    if (readHeaderResult==2)
    {
    	loadErrorString="Could not read DPX header";
    	//return 1;
    }
    
    int bitSize=(int)imageInfo.image_element[0].bit_size;
    if(readHeaderResult!=0)
    {
    	printf("Attempting to read using GFL\n");
    	GFL_LOAD_PARAMS load_option; 
	GFL_FILE_INFORMATION info;
	GFL_ERROR error; 
	
	gflGetDefaultLoadParams( &load_option ); 
	load_option.ColorModel = GFL_BGRA;
	load_option.Flags|= GFL_LOAD_METADATA;
	int resizeToX=0;
	int resizeToY=0;
	
	if(params.compressed==GFC_16BPC || params.compressed==GFC_16HALF)
	{
		load_option.Flags|= GFL_LOAD_ORIGINAL_DEPTH;
	}
	
	
	
	error = gflLoadBitmap(params.fileName.c_str(), &theBitmap, &load_option, &info ); 
	
	if(error)
	{
		printf ( "GFL ERROR while loading file %s: %s\n",params.fileName.c_str(),gflGetErrorString ( error ) );
		loadErrorString=gflGetErrorString ( error );
		return 1;
	}
    }
    else{
    //allocate gflBitmap the correct size

    
    samplesPerPixel=getSamplesPerPixel();
    int bitSizeForAlloc=8;

    if (bitSize>8 && (params.compressed==GFC_16BPC || params.compressed==GFC_16HALF))
        bitSizeForAlloc=16;


    //gfcTimer memsetTimer("Memset");
    GFL_COLOR color;
    color.Red=0;
    color.Green=0;
    color.Blue=0;
    color.Alpha=255;
    //memsetTimer.start();
	

	
    theBitmap=gflAllockBitmapEx(GFL_BGRA,imageInfo.pixels_per_line,imageInfo.lines_per_image_ele,bitSizeForAlloc,4,NULL);
	if(theBitmap==NULL)
	{
		printf ( "ERROR Allocating space for %s\n",params.fileName.c_str() );
		loadErrorString="Error Allocating memory space";
		return GFCFRAME_LOADERROR_NO_MORE_MEMORY;
	}
    //memset(theBitmap->Data,255,imageInfo.pixels_per_line*imageInfo.lines_per_image_ele*bitSizeForAlloc*4/8);

    //memsetTimer.stop();
    //memsetTimer.print();

    //READ THE DPX DATA AND FEED IT INTO THE BITMAP
    if (readSlice(fileInfo.offset,imageInfo.lines_per_image_ele,0)) {
        return 1;
    }
    }




    //Process the image according to params
    {

        



        if (params.scale!=100) {
            gflResize(theBitmap,NULL,theBitmap->Width*params.scale/100.0,theBitmap->Height*params.scale/100.0,params.filterType==0?GFL_RESIZE_QUICK:GFL_RESIZE_BILINEAR,0);
        }
		
		//crop after scale! not the other way around, the crop area is selected with the scaled image... do the same for other loaders.
		if (params.crop) {
            GFL_RECT cropRect;
            /*cropRect.x=params.aoi.x;
            cropRect.y=params.aoi.y; //for some reason, probably inverted loading, we must invert the y crop coordinate.
            cropRect.w=params.aoi.w;
            cropRect.h=params.aoi.h;*/
			cropRect.x=params.aoi.x;
			cropRect.y=theBitmap->Height-(params.aoi.h+params.aoi.y); //for some reason, probably inverted loading, we must invert the y crop coordinate.
			cropRect.w=params.aoi.w;
			cropRect.h=params.aoi.h;
            gflCrop(theBitmap,NULL,&cropRect);
        }

        int resizeToX=theBitmap->Width;
        int resizeToY=theBitmap->Height;

        quadSizeX=theBitmap->Width;
        quadSizeY=theBitmap->Height;

        switch (params.compressed) {
        case GFC_16HALF:
        case GFC_16BPC: { //if loading to is 16bit up each component in each pixel to the correct value
            if(bitSize<=8)
            {
            	//do nothing
            }
            else if(readHeaderResult==2){ //we only upscale to 16bit if we use gfl
            int offset=16-bitSize;

            int imageHeight=theBitmap->Height;
            int imageWidth=theBitmap->Width;
            GFL_COLOR gfl_color,gflColor2;
            for ( int w=0;w<imageHeight;w++ )
            {
            	for ( int j=0;j<imageWidth;j++ )
            	{
            		gflGetColorAt(theBitmap, j, w, &gfl_color);
            		
            		gflColor2.Blue=gfl_color.Red<<offset;
            		gflColor2.Green=gfl_color.Green<<offset;
            		gflColor2.Red=gfl_color.Blue<<offset;
            		gflColor2.Alpha=gfl_color.Alpha<<offset;
            		gflSetColorAt(theBitmap, j, w, &gflColor2);
            	}
            }
            }

        }
        break;



        case GFC_S3TCDX1: {

            //resize to the next divisible by 4

            resizeToX=getNextDivisibleBy4(theBitmap->Width);
            resizeToY=getNextDivisibleBy4(theBitmap->Height);
            GFL_COLOR gfl_color;
            GFL_ERROR error;
            gfl_color.Red=gfl_color.Blue=gfl_color.Green=0.2;

            texCoords.x=0;
            texCoords.y=0;
            texCoords.w=theBitmap->Width/(float)resizeToX;
            texCoords.h=theBitmap->Height/(float)resizeToY;

            quadSizeX=theBitmap->Width;
            quadSizeY=theBitmap->Height;

            if (resizeToX!=theBitmap->Width || resizeToY!=theBitmap->Height) {
                printf("Resizing canvas to %ix%i\n",resizeToX,resizeToY);
                error=gflResizeCanvas(theBitmap,NULL,resizeToX,resizeToY,GFL_CANVASRESIZE_TOPLEFT,&gfl_color);
                if (error) {
                    printf("ResizeCanvas error: %s\n",gflGetErrorString(error));
                }
            }



        }
        break;
        }


        sizeX=theBitmap->Width;
        sizeY=theBitmap->Height;

        
        bitDepth=theBitmap->BitsPerComponent;
        originalBitDepth=bitSize;
        numOfComponents=theBitmap->ComponentsPerPixel;
        
        format="DPX";
        formatDescription="ANSI/SMPTE 268M-2003 Digital Picture Exchange ";
        formatDescription+=fileInfo.vers;
        compressionDescription="uncompressed";
	
	//6. fill required frame information 
    switch ( params.compressed ) {

    case GFC_4BPC:
        switch (numOfComponents) {
        case 1:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_LUMINANCE4;
            break;
        case 3:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_RGB4;
            break;
        case 4:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_RGBA4;
            break;
        default:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_LUMINANCE4;
            break;
        }
        frameInfo.target=GL_TEXTURE_RECTANGLE_ARB;
        frameInfo.dataPointer=theBitmap->Data;
        texCoords.x=0;
        texCoords.y=0;
        texCoords.w=theBitmap->Width;
        texCoords.h=theBitmap->Height;
        break;

    case GFC_8BPC:
		
		//printf("num of components: %i\n",numOfComponents);
        switch (numOfComponents) {
        case 1:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_LUMINANCE8;
            break;
        case 3:
            frameInfo.format=GL_BGR;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_RGB8;
            break;
        case 4:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_RGBA8;
            break;
        default:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_RGBA8;
            break;
        }
        frameInfo.target=GL_TEXTURE_RECTANGLE_ARB;
		//printf("setting data pointer\n");
        frameInfo.dataPointer=theBitmap->Data;
        texCoords.x=0;
        texCoords.y=0;
        texCoords.w=theBitmap->Width;
        texCoords.h=theBitmap->Height;
        break;

    case GFC_16HALF:
    case GFC_16BPC:
        switch (numOfComponents) {
        case 1:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_SHORT;
            frameInfo.internalFormat=GL_LUMINANCE16F_ARB;

            break;
        case 3:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_SHORT;
            frameInfo.internalFormat=GL_RGB16F_ARB;
            break;
        case 4:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_SHORT;
            frameInfo.internalFormat=GL_RGBA16F_ARB;
            break;
        default:
            frameInfo.format=GL_LUMINANCE;
            frameInfo.dataType=GL_UNSIGNED_SHORT;
            frameInfo.internalFormat=GL_LUMINANCE16F_ARB;
            break;
        }
        frameInfo.target=GL_TEXTURE_RECTANGLE_ARB;
        frameInfo.dataPointer=theBitmap->Data;
        texCoords.x=0;
        texCoords.y=0;
        texCoords.w=theBitmap->Width;
        texCoords.h=theBitmap->Height;
        break;

    case GFC_S3TCDX1:

        switch (numOfComponents) {
        case 1:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_LUMINANCE8;
            break;
        case 3:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
            break;
        case 4:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
            break;
        default:
            frameInfo.format=GL_LUMINANCE;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_LUMINANCE8;
            break;
        }
        frameInfo.target=GL_TEXTURE_2D;texCoords.x=0;
        frameInfo.dataPointer=theBitmap->Data;
        break;
    }
	
	switch (imageInfo.image_element[0].descriptor) {
    	case ImageElementRGB:
        	
        	channelNames.push_back("RGB");
        
        break;
    	case ImageElementRGBA:
        
        	channelNames.push_back("RGBA");
        
        break;

    	default:
                channelNames.push_back("RGBA");
				//return 2;
        break;
    	}
	
//         GFL_SAVE_PARAMS saveparams;
//         GFL_ERROR gflerror;
//         gflGetDefaultSaveParams(&saveparams);
//         saveparams.FormatIndex=gflGetFormatIndexByName("jpeg");
//         gflerror=gflSaveBitmap("/tmp/testDPXWrite.jpg",theBitmap,&saveparams);
//         if (gflerror) {
//             printf("%s\n",gflGetErrorString(gflerror));
//         }

        return 0;
    }

    return 1;

}

int gfcImageLoaderDPX::peek(gfcLoadParams params, gfcPeekInfo* results) {
    return 0;
}

void* gfcImageLoaderDPX::getPixelPointer() {

    //return rgba8pixels;
    return theBitmap->Data;
}

void gfcImageLoaderDPX::releaseMemory() {
    if(theBitmap)
    	gflFreeBitmap(theBitmap);
    theBitmap=NULL;
}

int gfcImageLoaderDPX::readHeader() {
    //printf("*********\nREAD DPX HEADER\n*********\n");
//    printf("Opening file %s...",params.fileName.c_str());
    std::ifstream fs;
    fs.open(params.fileName.c_str(),std::ios::binary);

    if (!fs) {
        printf("ERROR OPENING FILE!\n");
        printf("*********\n");
        return 1; //return true
    }
    //printf("done!\n");
   // printf("Reading file header sizeof fileinfo: %i...\n",sizeof(dpx_file_information));
    bool readError=false;
    fs.read((char*)&fileInfo,sizeof(dpx_file_information));
    readError=readError || fs.fail();
    
    fs.read((char*)&imageInfo,sizeof(dpx_image_information));
    readError=readError || fs.fail();
    
    fs.read((char*)&imageOrientation,sizeof(dpx_image_orientation));
readError=readError || fs.fail();

    fs.read((char*)&motionHeader,sizeof(dpx_motion_picture_film_header));
readError=readError || fs.fail();

    fs.read((char*)&tvHeader,sizeof(dpx_television_header));
readError=readError || fs.fail();

    fs.read((char*)&userInfo,sizeof(dpx_UserInfo ));
readError=readError || fs.fail();
    
    fs.close();
    
    //Was there an error reading the header?
    if(readError)
    {
    	printf("Error reading file\n");
    	return 2;
    }

    
    // printf("done\n");
    //endian_type=LSBEndian;
    bool swab=false;
    if (fileInfo.magic_num==0x58504453) { //if header is XPDS swab int, longs and floats
    	swab=true;
        //endian_type=MSBEndian;
        //printf("Swapping endians...\n");
//#ifndef __BIG_ENDIAN__
        
        swabUInt32(&fileInfo.magic_num);
        swabUInt32(&fileInfo.offset);
        swabUInt32(&fileInfo.file_size);
        swabUInt32(&fileInfo.ditto_key);
        swabUInt32(&fileInfo.gen_hdr_size);
        swabUInt32(&fileInfo.ind_hdr_size);
        swabUInt32(&fileInfo.user_data_size);
        swabUInt32(&fileInfo.key);

        swabUInt16(&imageInfo.orientation);
        swabUInt16(&imageInfo.element_number);
        swabUInt32(&imageInfo.pixels_per_line);
        swabUInt32(&imageInfo.lines_per_image_ele);
        for (int i=0;i<8;i++) {
            swabUInt32(&imageInfo.image_element[i].data_sign);
            swabUInt32(&imageInfo.image_element[i].ref_low_data);
            swabFloat(&imageInfo.image_element[i].ref_low_quantity);
            swabUInt32(&imageInfo.image_element[i].ref_high_data);
            swabFloat(&imageInfo.image_element[i].ref_high_quantity);
            swabUInt16(&imageInfo.image_element[i].packing);
            swabUInt16(&imageInfo.image_element[i].encoding);
            swabUInt32(&imageInfo.image_element[i].data_offset);
            swabUInt32(&imageInfo.image_element[i].eol_padding);
            swabUInt32(&imageInfo.image_element[i].eo_image_padding);
        }

        swabUInt32(&imageOrientation.x_offset);
        swabUInt32(&imageOrientation.y_offset);
        swabFloat(&imageOrientation.x_center);
        swabFloat(&imageOrientation.y_center);
        swabUInt32(&imageOrientation.x_orig_size);
        swabUInt32(&imageOrientation.y_orig_size);
        swabUInt16(&imageOrientation.border[0]);
        swabUInt16(&imageOrientation.border[1]);
        swabUInt16(&imageOrientation.border[2]);
        swabUInt16(&imageOrientation.border[3]);
        swabUInt32(&imageOrientation.pixel_aspect[0]);
        swabUInt32(&imageOrientation.pixel_aspect[1]);

        swabUInt32(&motionHeader.frame_position);
        swabUInt32(&motionHeader.sequence_len);
        swabUInt32(&motionHeader.held_count);
        swabFloat(&motionHeader.frame_rate);
        swabFloat(&motionHeader.shutter_angle);

        swabUInt32(&tvHeader.tim_code);
        swabUInt32(&tvHeader.userBits);
        swabFloat(&tvHeader.hor_sample_rate);
        swabFloat(&tvHeader.ver_sample_rate);
        swabFloat(&tvHeader.frame_rate);
        swabFloat(&tvHeader.time_offset);
        swabFloat(&tvHeader.gamma);
        swabFloat(&tvHeader.black_level);
        swabFloat(&tvHeader.black_gain);
        swabFloat(&tvHeader.break_point);
        swabFloat(&tvHeader.white_level);
        swabFloat(&tvHeader.integration_times);
//#endif //(__BIG_ENDIAN__)
    }
    
#ifdef __BIG_ENDIAN__
  //fl_alert("__BIG_ENDIAN__ DEFINED");
  /*if(swab)
     fl_alert("SWABING");
  else
     fl_alert("NOT SWABING");*/
	 
  endian_type = (swab ? LSBEndian : MSBEndian);
  
#else
  endian_type = (swab ? MSBEndian : LSBEndian);
#endif
    
	//sprintf(fileName,"%s",params.fileName);
    timeCodeString=SMPTEBitsToString(tvHeader.tim_code);
    std::ostringstream stream(std::iostream::out);
    //store into metadata
    std::multimap<std::string,std::string>::iterator it=metaData.begin();
    //metaData[exif->ItemsList[i].Name]=exif->ItemsList[i].Value;
    it=metaData.insert(it,std::pair<std::string,std::string>("Filename",fileInfo.file_name));
    it=metaData.insert(it,std::pair<std::string,std::string>("Version",fileInfo.vers));
    it=metaData.insert(it,std::pair<std::string,std::string>("Creation",fileInfo.create_time));
    it=metaData.insert(it,std::pair<std::string,std::string>("Creator",fileInfo.creator));
    it=metaData.insert(it,std::pair<std::string,std::string>("Project",fileInfo.project));
    it=metaData.insert(it,std::pair<std::string,std::string>("Copyright",fileInfo.copyright));
    stream.str("");
    stream<< (int)imageInfo.image_element[0].bit_size;
    it=metaData.insert(it,std::pair<std::string,std::string>("BitDepth",stream.str()));
    stream.str("");
    stream<< imageInfo.image_element[0].description;
    it=metaData.insert(it,std::pair<std::string,std::string>("Description",stream.str()));
    it=metaData.insert(it,std::pair<std::string,std::string>("Input Device Name",imageOrientation.input_dev));
    it=metaData.insert(it,std::pair<std::string,std::string>("Input Serial",imageOrientation.input_serial));
    it=metaData.insert(it,std::pair<std::string,std::string>("SMPTE TimeCode",timeCodeString));
    it=metaData.insert(it,std::pair<std::string,std::string>("Format",motionHeader.format));
    it=metaData.insert(it,std::pair<std::string,std::string>("Frame ID",motionHeader.frame_id));
    it=metaData.insert(it,std::pair<std::string,std::string>("Slate Info",motionHeader.slate_info));
    stream.str("");
    stream<< motionHeader.frame_rate;
    it=metaData.insert(it,std::pair<std::string,std::string>("Frame Rate",stream.str()));
    stream.str("");
    //stream<< (unsigned int)tvHeader.field_num;
    //it=metaData.insert(it,std::pair<std::string,std::string>("Field:",stream.str()));
    std::string keykode="";
    {//Calculate KeyKode

        if (motionHeader.film_mfg_id[0]!=0)
            keykode.append(motionHeader.film_mfg_id,2);
        keykode+=" ";
        if (motionHeader.film_type[0]!=0)
            keykode.append(motionHeader.film_type,2);
        keykode+=" ";
        if (motionHeader.prefix[0]!=0)
            keykode.append(motionHeader.prefix,6);
        keykode+=" ";
        if (motionHeader.count[0]!=0)
            keykode.append(motionHeader.count,4);
        if (motionHeader.offset[0]!=0) {
            keykode+="+";

            char charOffset[2];
            //charOffset[2]='\0';
            strncpy(charOffset,motionHeader.offset,2);
            int intOffset=atoi(charOffset);
            //intOffset/=4;
            char offsetResult[10];
            sprintf(offsetResult,"%i",intOffset);
            keykode.append(offsetResult,intOffset>=10?2:1);
        }
    }
    it=metaData.insert(it,std::pair<std::string,std::string>("KeyKode",keykode));

    packing_method=imageInfo.image_element[0].packing;
    bytesPerRow = DPXRowBytes(1,imageInfo.pixels_per_line*getSamplesPerPixel(),(int)imageInfo.image_element[0].bit_size,imageInfo.image_element[0].packing);
    bits_per_sample=(int)imageInfo.image_element[0].bit_size;
    samples_per_row=getSamplesPerPixel()*imageInfo.pixels_per_line;
    swap_word_datums=false;
    descriptor=imageInfo.image_element[0].descriptor;
    
    switch (descriptor) {
    case ImageElementRGB:
        originalNumOfComponents=3;
        // printf("Supported!:RGB\n");
        break;
    case ImageElementRGBA:
        originalNumOfComponents=4;
        // printf("Supported!:RGBA\n");
        break;

    default:
        printf("DPX descriptor NOT Supported!: %i\n",descriptor);
        char tmpChar[4];
        sprintf(tmpChar,"%i",descriptor);
        loadErrorString=std::string("DPX descriptor NOT Supported: ")+tmpChar;
		originalNumOfComponents=0;
        return 3;
        break;
    }

    // printf("Checking for supported bit depth...");
    switch (bits_per_sample) {
    case 8:
    break;
    case 10:
        // printf("Supported!: %i\n",bits_per_sample);
		if(descriptor==ImageElementRGBA){
			printf("10bit RGBA DPX not supported!");
			char tmpChar[4];
			loadErrorString=std::string("10bit RGBA DPX NOT Supported: ");
			return 3;
		}
        break;

    default:
        printf("DPX Bit depth NOT Supported: %i\n", bits_per_sample);
        char tmpChar[4];
        sprintf(tmpChar,"%i",bits_per_sample);
        loadErrorString=std::string("DPX bitdepth NOT Supported: ")+tmpChar;
        return 3;
        break;
    }

    //printf("Checking for supported packing method...");
    switch (packing_method) {

    
    case PackingMethodWordsFillMSB:
    case PackingMethodWordsFillLSB:
        //  printf("Supported!: %i\n",packing_method);
        break;

    default:
        printf("DPX packing method NOT Supported!: %i\n", packing_method);
        char tmpChar[4];
        sprintf(tmpChar,"%i",packing_method);
        loadErrorString=std::string("DPX packing method NOT Supported: ")+tmpChar;
        return 3;
        break;
    }
    
    // printf("*********\n");
    return 0; //all went well
}

std::string gfcImageLoaderDPX::SMPTEBitsToString(const unsigned int value) {
    unsigned int
    pos,
    shift = 28;
    std::string result;
    char *str=new char[30];
    char *originalStr=str;
    for (pos=8; pos > 0; pos--, shift -= 4) {
        sprintf(str,"%01u",(unsigned int) ((value >> shift) & 0x0fU));
        str += 1;
        if ((pos > 2) && (pos % 2)) {
            strcat(str,":");
            str++;
        }
    }
    *str='\0';
    result=originalStr;
    //printf("OriginalStr: %s (%s)\n",originalStr,result.c_str());
    delete [] originalStr;

    return result;
}


void testThread(gfcImageLoaderDPX* loader, int *pixelStart, unsigned char* scanline)
{
	
};

/**
 * 
 * @param startOffset 
 * @param numLines 
 * @param bitmapStorageStartIndex 
 * @return 
 */
int gfcImageLoaderDPX::readSlice(long startOffset, long numLines, long bitmapStorageStartIndex) {

    //printf("\n*********\nREAD DPX SLICE\n*********\n");
    

    /*
          Are datums returned in reverse order when extracted from a
          32-bit word?  This is to support Note 2 in Table 1 which
          describes how RGB/RGBA are returned in reversed order for the
          10-bit "filled" format.  Note 3 refers to Note 2 so presumably
          the same applies for ABGR.  The majority of YCbCr 4:2:2 files
          received have been swapped (but not YCbCr 4:4:4 for some
          reason) so swap the samples for YCbCr 4:2:2 as well.
        */
    if ((descriptor == ImageElementRGB) ||
            (descriptor == ImageElementRGBA) ||
            (descriptor == ImageElementABGR) ||
            (descriptor == ImageElementCbYCrY422) ||
            (descriptor == ImageElementCbYACrYA4224) ||
            (descriptor == ImageElementLuma)) {
        if (((int)imageInfo.image_element[0].bit_size == 10) && (imageInfo.image_element[0].packing != PackingMethodPacked))
            swap_word_datums = true;
    }

    //printf("Checking for supported descriptor...");
    switch (descriptor) {
    case ImageElementRGB:
        originalNumOfComponents=3;
        // printf("Supported!:RGB\n");
        break;
    case ImageElementRGBA:
        originalNumOfComponents=4;
        // printf("Supported!:RGBA\n");
        break;

    default:
        printf("DPX descriptor NOT Supported!: %i\n",descriptor);
        char tmpDescriptor[4];
        sprintf(tmpDescriptor,"%i",descriptor);
        loadErrorString=std::string("DPX descriptor NOT Supported!: ")+tmpDescriptor;
        return 1;
        break;
    }

    // printf("Checking for supported bit depth...");
    switch (bits_per_sample) {
    case 8:
    case 10:
        // printf("Supported!: %i\n",bits_per_sample);
        break;

    default:
        printf("DPX Bit depth NOT Supported: %i\n", bits_per_sample);
        char bitsPerSample[4];
        sprintf(bitsPerSample,"%i",bits_per_sample);
        loadErrorString=std::string("DPX Bit depth NOT Supported: ")+bitsPerSample;
        return 1;
        break;
    }

    //printf("Checking for supported packing method...");
    switch (packing_method) {

    case PackingMethodPacked:
    case PackingMethodWordsFillMSB:
    case PackingMethodWordsFillLSB:
        //  printf("Supported!: %i\n",packing_method);
        break;

    default:
        printf("DPX packing method NOT Supported!: %i\n", packing_method);
        char tmpChar[4];
        sprintf(tmpChar,"%i",packing_method);
        loadErrorString=std::string("DPX packing method NOT Supported: ")+tmpChar;
        return 1;
        break;
    }

    unsigned char *scanline; //raw info read from the file



    //open the file and scan to the startOffset
    // printf("Opening file %s...",params.fileName.c_str());
    std::ifstream fs(params.fileName.c_str(), std::ios::binary);
    if (!fs) {
        printf("ERROR READING FILE %s, could not open file;\n",params.fileName.c_str());
        printf("*********\n");
        loadErrorString=std::string("Error reading file, could not open file");
        return 0; //TODO: shouldnt this be 1??
    }
    fs.seekg(startOffset);
    //printf("done\n");


    requestedBitSize=8;
    switch ( params.compressed ) {
    case GFC_16HALF:
    case GFC_16BPC:
        requestedBitSize=16;
        break;

    case GFC_4BPC:
    case GFC_S3TCDX1:
    case GFC_8BPC:
        requestedBitSize=8;
        break;
    }




    int pixelIndexStart=0;
    int pixelIndexOffsetPerLine=(imageInfo.pixels_per_line<<2);

/***TEST BLOCK****/
    
    
//     	gfcTimer readingTestTimer("HugeBlock");
//     	readingTestTimer.start();
//         unsigned char* testHugeBlock= new unsigned char [bytesPerRow*numLines];
//         fs.read((char*)testHugeBlock,bytesPerRow*numLines);
//         readingTestTimer.stop();
//         readingTestTimer.print();
    
    
    /*	gfcTimer readingTestTimer("Ona at a time");
    	readingTestTimer.start();
    	int howManyScanlines=numLines/16;
    unsigned char* testScanline= new unsigned char [bytesPerRow*howManyScanlines];
    	fs.read((char*)testScanline,bytesPerRow*howManyScanlines);
    for (int i=0;i<numLines/howManyScanlines-1;i++) {
        //start a bunch of threads.
        boost::thread* myThread=new boost::thread ( boost::bind ( &testThread,this,&i,testScanline) );
        fs.read((char*)testScanline,bytesPerRow*howManyScanlines);
        myThread->join();
        
        
    }
    readingTestTimer.stop();
    readingTestTimer.print();
    */ 
    
    
//ENDOF: TEST BLOCK****/

	int totalImageBytes=bytesPerRow*numLines;
	unsigned char* rawBytes=new unsigned char[totalImageBytes];
	
	//read the whole file into RAM, then process each scanline 
	//fs.read((char*)rawBytes,totalImageBytes);
	
	
	{
		//this mutex is used to limit the reading to one frame at a time. 
		if (sett.balanceReads)
		{
			//printf("Checking read condition\n");
			boost::try_mutex::scoped_lock lock ( trackManager.readMutex );
			while (trackManager.ioBusy!=0)
			{
			//printf("Waiting for read condition\n");
			//balanceReadCond.wait(lock);
			}
			trackManager.ioBusy=1;
			fs.rdbuf()->sgetn((char*)rawBytes,totalImageBytes);
			//read((char*)rawBytes,totalImageBytes);
			fs.close();
			trackManager.ioBusy=0;
			balanceReadCond.notify_one();
		}
		else
		{
			fs.rdbuf()->sgetn((char*)rawBytes,totalImageBytes);
			//read((char*)rawBytes,totalImageBytes);
			fs.close();
		}
	}
	

    // printf("Reading %i scanlines of %i bytes each\n",numLines,bytesPerRow);
    int i=0;
	switch (bits_per_sample) {
    case 10: {
        switch (descriptor) {
        case ImageElementRGB:
            /*if (packing_method!=PackingMethodPacked)*/
        {
		
            for (i=0;i<numLines;i++) {
                //process each scanline
				ReadRowSamplesRGBFilled10(&rawBytes[bytesPerRow*i],pixelIndexStart);
				pixelIndexStart+=pixelIndexOffsetPerLine;
												
			/*boost::thread* myThread=new boost::thread ( boost::bind ( &testThread,this,&i) );
			myThread->join(); */
			
            }
        } /*else
    if (packing_method==PackingMethodPacked) {
        ReadRowSamplesRGBPacked10(scanline,pixelIndexStart);
    }*/
        break;

        case ImageElementRGBA:
            if (packing_method!=PackingMethodPacked) {
				for (i=0;i<numLines;i++) {
					//process each scanline
					ReadRowSamplesRGBAFilled10(&rawBytes[bytesPerRow*i],pixelIndexStart);
					pixelIndexStart+=pixelIndexOffsetPerLine;

					/*boost::thread* myThread=new boost::thread ( boost::bind ( &testThread,this,&i) );
					myThread->join(); */

				}
            }
            break;
        }
    }
    break;

    case 8: {
        switch (descriptor) {
        case ImageElementRGB: {
            for (i=0;i<numLines;i++) {
                //read a scanline
                //fs.read((char*)scanline_data,bytesPerRow);

				ReadRowSamplesRGB8(&rawBytes[bytesPerRow*i],pixelIndexStart);
				pixelIndexStart+=pixelIndexOffsetPerLine;

                /*pixelIndexStart=i*(imageInfo.pixels_per_line<<2);
                ReadRowSamplesRGB8(scanline,pixelIndexStart);*/
            }

        }
        break;

        case ImageElementRGBA: {
            for (i=0;i<numLines;i++) {
                //read a scanline
                //fs.read((char*)scanline_data,bytesPerRow);

				ReadRowSamplesRGBA8(&rawBytes[bytesPerRow*i],pixelIndexStart);
				pixelIndexStart+=pixelIndexOffsetPerLine;

                /*pixelIndexStart=i*(imageInfo.pixels_per_line<<2);
                ReadRowSamplesRGBA8(scanline,pixelIndexStart);*/
            }

        }
        break;
        }
    }
    break;
    }

   
    
	delete [] rawBytes;
    //printf("done\n");
    //printf("*********\n");
    return 0;
}

void gfcImageLoaderDPX::ReadRowSamplesRGBFilled10(const unsigned char * scanline, int pixelIndexStart) {
    //printf("ReadRowSamplesRGBFilledA10\n");
    int datum=0;
    register PackedU32Word packed_u32;

    /*bool word_pad_lsb=false, word_pad_msb=false;
    if (packing_method == PackingMethodWordsFillLSB)
        word_pad_lsb=true;
    else if (packing_method == PackingMethodWordsFillMSB)
        word_pad_msb=true;*/

    ///*DETERMINE THE SHIFTS NEEDED TO EXTRACT COMPONENTS FROM DATA*///
    unsigned int
    shifts[3] = { 0, 0, 0 };

    if (packing_method == PackingMethodWordsFillLSB) {
        /*
          Padding in LSB (Method A)  Standard method.
        */
        if (swap_word_datums == false) {
            shifts[0]=2;  /* datum-0 / blue */
            shifts[1]=12; /* datum-1 / green */
            shifts[2]=22; /* datum-2 / red */
        } else {
            shifts[0]=22; /* datum-2 / red */
            shifts[1]=12; /* datum-1 / green */
            shifts[2]=2;  /* datum-0 / blue */
        }
    } else if (packing_method == PackingMethodWordsFillMSB) {
        /*
          Padding in MSB (Method B)  Deprecated method.
        */
        if (swap_word_datums == false) {
            shifts[0]=0;  /* datum-0 / blue */
            shifts[1]=10; /* datum-1 / green */
            shifts[2]=20; /* datum-2 / red */
        } else {
            shifts[0]=20; /* datum-2 / red */
            shifts[1]=10; /* datum-1 / green */
            shifts[2]=0;  /* datum-0 / blue */
        }
    }

    if (endian_type == MSBEndian) {
        if (requestedBitSize==8) {
            for (int i=samples_per_row/3; i > 0; --i) {
                datum=0;
#ifndef __BIG_ENDIAN__ //x86 little indian
                //printf("MSBEndian in LSB Machine %i\n",i);
                //swap the bytes
                packed_u32.octets[3]=*scanline++;
                packed_u32.octets[2]=*scanline++;
                packed_u32.octets[1]=*scanline++;
                packed_u32.octets[0]=*scanline++;
                //extract the bytes from the word, and shift them
                theBitmap->Data[pixelIndexStart+2]=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
                theBitmap->Data[pixelIndexStart+1]=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
                theBitmap->Data[pixelIndexStart]=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
#else //PPC Mac or other Big Endian
                //printf("MSBEndian in MSB Machine\n"); 
                theBitmap->Data[pixelIndexStart+2]=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)>>2;
                theBitmap->Data[pixelIndexStart+1]=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)>>2;
                theBitmap->Data[pixelIndexStart]=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)>>2;
                scanline+=4;
#endif
                pixelIndexStart+=4;
            }
        } else { //requestedBitSize is 16
            for (int i=samples_per_row/3; i > 0; --i) {
                datum=0;
#ifndef __BIG_ENDIAN__ //x86 little indian
                //printf("MSBEndian in LSB Machine up to 16bits\n");
                //swap the bytes
                packed_u32.octets[3]=*scanline++;
                packed_u32.octets[2]=*scanline++;
                packed_u32.octets[1]=*scanline++;
                packed_u32.octets[0]=*scanline++;
                //extract the bytes from the word, and shift them to their right size
                (((unsigned short*)(theBitmap->Data))[pixelIndexStart+2])=((packed_u32.word >> shifts[datum++]) & 0x3FF)<<6;
                (((unsigned short*)(theBitmap->Data))[pixelIndexStart+1])=((packed_u32.word >> shifts[datum++]) & 0x3FF)<<6;
                (((unsigned short*)(theBitmap->Data))[pixelIndexStart])=((packed_u32.word >> shifts[datum++]) & 0x3FF)<<6;
#else
                //printf("MSBEndian in MSB Machine\n");
                (((unsigned short*)(theBitmap->Data))[pixelIndexStart+2])=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)<<6;
                (((unsigned short*)(theBitmap->Data))[pixelIndexStart+1])=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)<<6;
                (((unsigned short*)(theBitmap->Data))[pixelIndexStart])=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)<<6;
                scanline+=4;
#endif
                pixelIndexStart+=4;
            }
        }

    } else if (endian_type == LSBEndian) {
		
		if (requestedBitSize==8) {
			for (int i=samples_per_row/3; i > 0; --i) {
				datum=0;
#ifndef __BIG_ENDIAN__ //x86 little indian
				//PPC Mac or other Big Endian
				//printf("MSBEndian in MSB Machine\n"); 
				theBitmap->Data[pixelIndexStart+2]=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)>>2;
				theBitmap->Data[pixelIndexStart+1]=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)>>2;
				theBitmap->Data[pixelIndexStart]=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)>>2;
				scanline+=4;
#else 
				//printf("MSBEndian in LSB Machine %i\n",i);
				//swap the bytes
				packed_u32.octets[3]=*scanline++;
				packed_u32.octets[2]=*scanline++;
				packed_u32.octets[1]=*scanline++;
				packed_u32.octets[0]=*scanline++;
				//extract the bytes from the word, and shift them
				theBitmap->Data[pixelIndexStart+2]=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
				theBitmap->Data[pixelIndexStart+1]=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
				theBitmap->Data[pixelIndexStart]=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
				
				
#endif
				pixelIndexStart+=4;
			}
		} else { //requestedBitSize is 16
			for (int i=samples_per_row/3; i > 0; --i) {
				datum=0;
#ifndef __BIG_ENDIAN__ //x86 little indian
				//printf("MSBEndian in MSB Machine\n");
				(((unsigned short*)(theBitmap->Data))[pixelIndexStart+2])=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)<<6;
				(((unsigned short*)(theBitmap->Data))[pixelIndexStart+1])=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)<<6;
				(((unsigned short*)(theBitmap->Data))[pixelIndexStart])=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)<<6;
				scanline+=4;
#else

				//printf("MSBEndian in LSB Machine up to 16bits\n");
				//swap the bytes
				packed_u32.octets[3]=*scanline++;
				packed_u32.octets[2]=*scanline++;
				packed_u32.octets[1]=*scanline++;
				packed_u32.octets[0]=*scanline++;
				//extract the bytes from the word, and shift them to their right size
				(((unsigned short*)(theBitmap->Data))[pixelIndexStart+2])=((packed_u32.word >> shifts[datum++]) & 0x3FF)<<6;
				(((unsigned short*)(theBitmap->Data))[pixelIndexStart+1])=((packed_u32.word >> shifts[datum++]) & 0x3FF)<<6;
				(((unsigned short*)(theBitmap->Data))[pixelIndexStart])=((packed_u32.word >> shifts[datum++]) & 0x3FF)<<6;

				
#endif
				pixelIndexStart+=4;
			}
		}
		
		/*

#ifndef __BIG_ENDIAN__
        printf("LSBEndian in LSB Machine RGB Filled 10bpc\n");
#else
        printf("LSBEndian in MSB Machine\n");
#endif*/
    }
}

void gfcImageLoaderDPX::ReadRowSamples(const unsigned char * scanline, const unsigned int samples_per_row, const unsigned int bits_per_sample, const unsigned int packing_method, const unsigned int endian_type, const bool swap_word_datums, unsigned int * samples) {
    register unsigned long
    i;


    if ((packing_method != PackingMethodPacked) && ((bits_per_sample == 10) || (bits_per_sample == 12))) {
        bool word_pad_lsb=false, word_pad_msb=false;

        if (packing_method == PackingMethodWordsFillLSB)
            word_pad_lsb=true;
        else if (packing_method == PackingMethodWordsFillMSB)
            word_pad_msb=true;


        if (bits_per_sample == 10) {
            register PackedU32Word packed_u32;

            register unsigned int datum;

            unsigned int
            shifts[3] = { 0, 0, 0 };

            if (word_pad_lsb) {
                /*
                  Padding in LSB (Method A)  Standard method.
                */
                if (swap_word_datums == false) {
                    shifts[0]=2;  /* datum-0 / blue */
                    shifts[1]=12; /* datum-1 / green */
                    shifts[2]=22; /* datum-2 / red */
                } else {
                    shifts[0]=22; /* datum-2 / red */
                    shifts[1]=12; /* datum-1 / green */
                    shifts[2]=2;  /* datum-0 / blue */
                }
            } else if (word_pad_msb) {
                /*
                  Padding in MSB (Method B)  Deprecated method.
                */
                if (swap_word_datums == false) {
                    shifts[0]=0;  /* datum-0 / blue */
                    shifts[1]=10; /* datum-1 / green */
                    shifts[2]=20; /* datum-2 / red */
                } else {
                    shifts[0]=20; /* datum-2 / red */
                    shifts[1]=10; /* datum-1 / green */
                    shifts[2]=0;  /* datum-0 / blue */
                }
            }
            int pixelIndexStart=0;
            switch ( imageInfo.image_element[0].descriptor ) {

            case ImageElementRGB: {

#if defined(__BIG_ENDIAN__)
                printf("Big ENDIANDS!\n");
                for (i=samples_per_row/3; i > 0; --i) {
                    datum=0;
                    /*unsigned int data=*((unsigned int*)scanline);
                      theBitmap->Data[pixelIndexStart+2]=((data >> shifts[datum++]) & 0x3FF)>>2;
                      theBitmap->Data[pixelIndexStart+1]=((data >> shifts[datum++]) & 0x3FF)>>2;
                      theBitmap->Data[pixelIndexStart]=((data >> shifts[datum++]) & 0x3FF)>>2;
                      scanline+=4;
                      pixelIndexStart+=4;*/

                    theBitmap->Data[pixelIndexStart+2]=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)>>2;
                    theBitmap->Data[pixelIndexStart+1]=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)>>2;
                    theBitmap->Data[pixelIndexStart]=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)>>2;
                    scanline+=4;
                    pixelIndexStart+=4;

//                         datum=0;
//                         MSBOctetsToPackedU32Word(scanline,packed_u32);
//                         theBitmap->Data[pixelIndexStart+2]=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
//                         theBitmap->Data[pixelIndexStart+1]=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
//                         theBitmap->Data[pixelIndexStart]=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
//                         pixelIndexStart+=4;

                }
#else
                printf("LITTLE ENDIANDS!\n");




#endif

            }

            break;
            case ImageElementRGBA: {
                for (i=samples_per_row/4; i > 0; --i) {
                    datum=0;
                    MSBOctetsToPackedU32Word(scanline,packed_u32);
                    theBitmap->Data[pixelIndexStart+3]=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
                    theBitmap->Data[pixelIndexStart+2]=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
                    theBitmap->Data[pixelIndexStart+1]=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
                    theBitmap->Data[pixelIndexStart]=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
                    pixelIndexStart+=4;

                }

                if ((samples_per_row % 4)) {
                    datum=0;
                    MSBOctetsToPackedU32Word(scanline,packed_u32);
                    for (i=(samples_per_row % 4); i > 0; --i) {
                        theBitmap->Data[pixelIndexStart+i]=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;

                    }
                }
            }
            break;
            }

            return;
        }
    }


}

unsigned int  gfcImageLoaderDPX::getSamplesPerPixel() {
    unsigned int
    samples_per_pixel=0;

    switch (imageInfo.image_element[0].descriptor) {
    case ImageElementUnspecified:
    case ImageElementRed:
    case ImageElementGreen:
    case ImageElementBlue:
    case ImageElementAlpha:
    case ImageElementLuma:
    case ImageElementColorDifferenceCbCr:
        samples_per_pixel=1;
        break;
    case ImageElementRGB:
        samples_per_pixel=3;
        break;
    case ImageElementRGBA:
    case ImageElementABGR:
        samples_per_pixel=4;
        break;
    case ImageElementCbYCrY422:
        /* CbY | CrY | CbY | CrY ..., even number of columns required. */
        samples_per_pixel=2;
        break;
    case ImageElementCbYACrYA4224:
        /* CbYA | CrYA | CbYA | CrYA ..., even number of columns required. */
        samples_per_pixel=3;
        break;
    case ImageElementCbYCr444:
        samples_per_pixel=3;
        break;
    case ImageElementCbYCrA4444:
        samples_per_pixel=4;
        break;
    default:
        samples_per_pixel=0;
        break;
    }

    return samples_per_pixel;
}

size_t gfcImageLoaderDPX::DPXRowBytes(const unsigned long rows,
                                      const unsigned int samples_per_row,
                                      const unsigned int bits_per_sample,
                                      const unsigned int packing_method) {
    size_t
    octets = 0;

    switch (bits_per_sample) {
    case 1:
        /* Packed 1-bit samples in 32-bit words. Rows are padded out to 32-bit alignment */
        octets=rows*((samples_per_row*bits_per_sample+31)/32)*sizeof(unsigned int);
        break;
    case 8:
        /* C.1 8-bit samples in a 32-bit word. Rows are padded out to 32-bit alignment */
        octets=rows*(( samples_per_row*bits_per_sample+31)/32)*sizeof(unsigned int);
        break;
    case 32:
        /* 32-bit samples in a 32-bit word */
        octets=samples_per_row*sizeof(unsigned int)*rows;
        break;
    case 10:
        if ((packing_method == PackingMethodWordsFillLSB) ||
                (packing_method == PackingMethodWordsFillMSB)) {
            /* C.3 Three 10-bit samples per 32-bit word */
            octets=(((( (rows*samples_per_row+2)/3)*sizeof(unsigned int)*8)+31)/32)*sizeof(unsigned int)-samples_per_row%2;
        } else {
            /* C.2 Packed 10-bit samples in a 32-bit word. */
            octets=rows*(( samples_per_row*bits_per_sample+31)/32)*sizeof(unsigned int)-samples_per_row%2;
        }
        break;
    case 12:
        if ((packing_method == PackingMethodWordsFillLSB) ||
                (packing_method == PackingMethodWordsFillMSB)) {
            /* C.5: One 12-bit sample per 16-bit word */
            octets=((( rows*samples_per_row*sizeof(unsigned short)*8)+15)/16)*sizeof(unsigned short);
        } else {
            /* C.4: Packed 12-bit samples in a 32-bit word. */
            octets=rows*(( samples_per_row*bits_per_sample+31)/32)*sizeof(unsigned int);
        }
        break;
    case 16:
        /* C.6 16-bit samples in 16-bit words. */
        octets=((( rows*samples_per_row*bits_per_sample)+15)/16)*sizeof(unsigned short);
        break;
    case 64:
        /* 64-bit samples in 64-bit words. */
        octets= rows*samples_per_row*8;
        break;
    }

    return octets;
}

void gfcImageLoaderDPX::ReadRowSamplesRGB8(const unsigned char * scanline, int pixelIndexStart) {

    for (int i=samples_per_row/3; i > 0; --i) {
        theBitmap->Data[pixelIndexStart+2]=(int)*scanline++;
        theBitmap->Data[pixelIndexStart+1]=(int)*scanline++;
        theBitmap->Data[pixelIndexStart]=(int)*scanline++;
        pixelIndexStart+=4;
    }

}
void gfcImageLoaderDPX::ReadRowSamplesRGBA8(const unsigned char * scanline, int pixelIndexStart) {

    for (int i=samples_per_row/3; i > 0; --i) {
        theBitmap->Data[pixelIndexStart+2]=(int)*scanline++;
        theBitmap->Data[pixelIndexStart+1]=(int)*scanline++;
        theBitmap->Data[pixelIndexStart+0]=(int)*scanline++;
        theBitmap->Data[pixelIndexStart+3]=(int)*scanline++;
        pixelIndexStart+=4;
    }

}

void gfcImageLoaderDPX::ReadRowSamplesRGBAFilled10(const unsigned char * scanline, int pixelIndexStart) {
    int datum=0;
    register PackedU32Word packed_u32;

    /*bool word_pad_lsb=false, word_pad_msb=false;
    if (packing_method == PackingMethodWordsFillLSB)
        word_pad_lsb=true;
    else if (packing_method == PackingMethodWordsFillMSB)
        word_pad_msb=true;*/

    ///*DETERMINE THE SHIFTS NEEDED TO EXTRACT COMPONENTS FROM DATA*///
    unsigned int
    shifts[3] = { 0, 0, 0 };

    if (packing_method == PackingMethodWordsFillLSB) {
        /*
          Padding in LSB (Method A)  Standard method.
        */
        if (swap_word_datums == false) {
            shifts[0]=2;  /* datum-0 / blue */
            shifts[1]=12; /* datum-1 / green */
            shifts[2]=22; /* datum-2 / red */
        } else {
            shifts[0]=22; /* datum-2 / red */
            shifts[1]=12; /* datum-1 / green */
            shifts[2]=2;  /* datum-0 / blue */
        }
    } else if (packing_method == PackingMethodWordsFillMSB) {
        /*
          Padding in MSB (Method B)  Deprecated method.
        */
        if (swap_word_datums == false) {
            shifts[0]=0;  /* datum-0 / blue */
            shifts[1]=10; /* datum-1 / green */
            shifts[2]=20; /* datum-2 / red */
        } else {
            shifts[0]=20; /* datum-2 / red */
            shifts[1]=10; /* datum-1 / green */
            shifts[2]=0;  /* datum-0 / blue */
        }
    }

    if (endian_type == MSBEndian) {
        if (requestedBitSize==8) {
            for (int i=samples_per_row/3; i > 0; --i) {
                datum=0;
#ifndef __BIG_ENDIAN__ //x86 little indian
                //printf("MSBEndian in LSB Machine %i\n",i);
                //swap the bytes
                packed_u32.octets[3]=*scanline++;
                packed_u32.octets[2]=*scanline++;
                packed_u32.octets[1]=*scanline++;
                packed_u32.octets[0]=*scanline++;
                //extract the bytes from the word, and shift them
                theBitmap->Data[pixelIndexStart+2]=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
                theBitmap->Data[pixelIndexStart+1]=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
                theBitmap->Data[pixelIndexStart]=((packed_u32.word >> shifts[datum++]) & 0x3FF)>>2;
                //scanline++
                //theBitmap->Data[pixelIndexStart+3]=128;
#else
                //printf("MSBEndian in MSB Machine\n");
                theBitmap->Data[pixelIndexStart+2]=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)>>2;
                theBitmap->Data[pixelIndexStart+1]=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)>>2;
                theBitmap->Data[pixelIndexStart]=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)>>2;
                scanline+=4;
#endif
                pixelIndexStart+=4;
            }
        } else { //requestedBitSize is 16
            for (int i=samples_per_row/3; i > 0; --i) {
                datum=0;
#ifndef __BIG_ENDIAN__ //x86 little indian
                //printf("MSBEndian in LSB Machine up to 16bits\n");
                //swap the bytes
                packed_u32.octets[3]=*scanline++;
                packed_u32.octets[2]=*scanline++;
                packed_u32.octets[1]=*scanline++;
                packed_u32.octets[0]=*scanline++;
                //extract the bytes from the word, and shift them to their right size
                (((unsigned short*)(theBitmap->Data))[pixelIndexStart+2])=((packed_u32.word >> shifts[datum++]) & 0x3FF)<<6;
                (((unsigned short*)(theBitmap->Data))[pixelIndexStart+1])=((packed_u32.word >> shifts[datum++]) & 0x3FF)<<6;
                (((unsigned short*)(theBitmap->Data))[pixelIndexStart])=((packed_u32.word >> shifts[datum++]) & 0x3FF)<<6;
#else
                //printf("MSBEndian in MSB Machine\n");
                (((unsigned short*)(theBitmap->Data))[pixelIndexStart+2])=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)<<6;
                (((unsigned short*)(theBitmap->Data))[pixelIndexStart+1])=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)<<6;
                (((unsigned short*)(theBitmap->Data))[pixelIndexStart])=(((*((unsigned int*)scanline)) >> shifts[datum++]) & 0x3FF)<<6;
                scanline+=4;
#endif
                pixelIndexStart+=4;
            }
        }

    } else if (endian_type == LSBEndian) {
#ifndef __BIG_ENDIAN__
        printf("LSBEndian in LSB Machine\n");
#else
        printf("LSBEndian in MSB Machine\n");
#endif
    }
}

void gfcImageLoaderDPX::ReadRowSamplesRGBPacked10(const unsigned char * scanline, int pixelIndexStart) {

    //printf("ReadRowSamplesRGBFilledA10\n");

    {
        ReadWordU32State
        read_state;

        WordStreamReadHandle
        read_stream;

        WordStreamReadFunc
        read_func=0;

        if (endian_type == MSBEndian)
            read_func=ReadWordU32BE;
        else if (endian_type == LSBEndian)
            read_func=ReadWordU32LE;

        read_state.words=scanline;
        WordStreamInitializeRead(&read_stream,(void *) &read_state,read_func);
        if (requestedBitSize==8) {
            for (int i=samples_per_row; i > 0; i--) {
                // printf("pixelIndexStart=%i\n",pixelIndexStart);
                theBitmap->Data[pixelIndexStart++]=(WordStreamLSBRead(&read_stream,bits_per_sample))>>2;
                if (pixelIndexStart%3==0) pixelIndexStart++;
            }

        } else { //requestedBitSize is 16
        }
    }



}


void gfcImageLoaderDPX::ReadRowSamplesRGBAPacked10(const unsigned char * scanline, int pixelIndexStart) {

    //printf("ReadRowSamplesRGBFilledA10\n");

    {
        ReadWordU32State
        read_state;

        WordStreamReadHandle
        read_stream;

        WordStreamReadFunc
        read_func=0;

        if (endian_type == MSBEndian)
            read_func=ReadWordU32BE;
        else if (endian_type == LSBEndian)
            read_func=ReadWordU32LE;

        read_state.words=scanline;
        WordStreamInitializeRead(&read_stream,(void *) &read_state,read_func);
        if (requestedBitSize==8) {
            for (int i=samples_per_row; i > 0; i--) {
                // printf("pixelIndexStart=%i\n",pixelIndexStart);
                theBitmap->Data[pixelIndexStart++]=(WordStreamLSBRead(&read_stream,bits_per_sample))>>2;
                if (pixelIndexStart%3==0) pixelIndexStart++;
            }

        } else { //requestedBitSize is 16
        }
    }



}




std::vector< std::string > gfcImageLoaderDPX::getChannelNames()
{
	return channelNames;
}
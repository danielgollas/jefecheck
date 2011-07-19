#include "gfcimageloaderfil.h"
#include "UIConstants.h"
#include "gfcStructures.h"
#include "gfcframe.h"

#include "gfctrackmanager.h"
extern gfcTrackManager trackManager;

gfcImageLoaderFIL::gfcImageLoaderFIL()
 : gfcImageLoader()
{
	//theBitmap=NULL;
}


gfcImageLoaderFIL::~gfcImageLoaderFIL()
{
}


int gfcImageLoaderFIL::fillProcessor(gfcImageProcessor& processor)
{
	return 0;
}


#ifdef USEFREEIMAGE
void FreeImageErrorHandler(FREE_IMAGE_FORMAT fif, const char *message) {
  printf("\n*** "); 
  if(fif != FIF_UNKNOWN) {
    printf("%s Format\n", FreeImage_GetFormatFromFIF(fif));
  }
  printf(message);
  printf(" ***\n");
}
#endif

int gfcImageLoaderFIL::load(gfcLoadParams params)
{
	#ifdef USEFREEIMAGE
	fif = FreeImage_GetFileType(params.fileName.c_str(), 0);
	FreeImage_SetOutputMessage(FreeImageErrorHandler);
	//figure out the file path
	if(fif == FIF_UNKNOWN) {
		// no signature ?
		// try to guess the file format from the file extension
		fif = FreeImage_GetFIFFromFilename(params.fileName.c_str());
	}
	// check that the plugin has reading capabilities ...
	if((fif != FIF_UNKNOWN) && FreeImage_FIFSupportsReading(fif)) {
		printf("File format supported\n");
	}
	else{
		//there is something wrong, we should return 
		printf ( "FIL ERROR while loading file %s: %s\n",params.fileName.c_str(), "No support for file format" );
		return GFCFRAME_LOADERROR_UNKNOWN;
		
	}

	/*gflGetDefaultLoadParams( &load_option ); 
	load_option.ColorModel = FIL_BGRA;
	load_option.Flags|=FIL_LOAD_FORCE_COLOR_MODEL;
	load_option.Flags|= FIL_LOAD_METADATA;
	//load_option.Origin=FIL_BOTTOM_LEFT;*/
	
	/*if(params.compressed==GFC_16BPC || params.compressed==GFC_16HALF)
	{
		load_option.Flags|= FIL_LOAD_ORIGINAL_DEPTH;
	}*/

	int resizeToX=0;
	int resizeToY=0;
	
	int loadSuccess;
	
	if (sett.balanceReads)
	{
		boost::try_mutex::scoped_lock lock ( trackManager.readMutex );
		while (trackManager.ioBusy!=0)
		{
			balanceReadCond.wait(lock);
		}
		trackManager.ioBusy=1;

		// ok, let's load the file
		//theBitmap = FreeImage_Load(fif, params.fileName.c_str(), 0);
		loadSuccess = theBitmap.load(params.fileName.c_str(), 0);
		
		//how do we handle errors here?
		trackManager.ioBusy=0;
		balanceReadCond.notify_one(); 
		
	}
	else
	{
		loadSuccess = theBitmap.load(params.fileName.c_str(), 0);
	}
	
	if(!loadSuccess)
	{
		printf ( "FIL ERROR while loading file %s: %s\n",params.fileName.c_str(),"unknown error");
		//this is where we would handle the errors if we knew how to get the error code o string in FIL.
		loadErrorString="Unknown error";		
		return GFCFRAME_LOADERROR_UNKNOWN;
	}
	else
	{
		
		if(params.scale!=100) 
		{
			theBitmap.rescale(theBitmap.getWidth()*params.scale/100.0,theBitmap.getHeight()*params.scale/100.0,
						params.filterType==0?FILTER_BOX:FILTER_BILINEAR);
			
			//gflResize(theBitmap,NULL,theBitmap->Width*params.scale/100.0,theBitmap->Height*params.scale/100.0,params.filterType==0?FIL_RESIZE_QUICK:FIL_RESIZE_BILINEAR,0);
		}
		
		//crop after scale!
		if(params.crop)
		{
			int cropRectX=params.aoi.x;
			int cropRectY=theBitmap.getVerticalResolution()-params.aoi.y-params.aoi.h; //we need to invert the y coordinate since the image has a top origin.
			int cropRectW=params.aoi.w;
			int cropRectH=params.aoi.h;
			//printf("cropping to %i %i %i %i\n",cropRect.x,cropRect.y,cropRect.w,cropRect.h);
			//crop works like this (left, top, right, bottom)			
			theBitmap.crop(cropRectX, cropRectY+cropRectW, cropRectX+cropRectW, cropRectY);
		}
		
		resizeToX=theBitmap.getWidth();
		resizeToY=theBitmap.getHeight();
		
		quadSizeX=theBitmap.getWidth();
		quadSizeY=theBitmap.getHeight();
		
		switch(params.compressed)
		{
		case GFC_16HALF:
		case GFC_16BPC: //if loading to is 16bit up each component in each pixel to the correct value
		{
			/*if(info.BitsPerComponent<=8)
			{
				//do nothing
			}
			else{
			int offset=16-info.BitsPerComponent;
			
			int imageHeight=theBitmap->Height;
			int imageWidth=theBitmap->Width;
			FIL_COLOR gfl_color,gflColor2; 
			int dataCounter=0;
			for ( int w=0;w<imageHeight;w++ )
			{
				//printf("dataCounter %i , %i\n",dataCounter,dataCounter/imageWidth);
				for ( int j=0;j<imageWidth;j++ )
				{
					
					
					
					
// 					(((unsigned short*)(theBitmap->Data))[dataCounter])=theBitmap->Data[dataCounter]<<offset;
// 					dataCounter++;
// 					(((unsigned short*)(theBitmap->Data))[dataCounter])=theBitmap->Data[dataCounter]<<offset;
// 					dataCounter++;
// 					(((unsigned short*)(theBitmap->Data))[dataCounter])=theBitmap->Data[dataCounter]<<offset;
// 					dataCounter++;
// 					(((unsigned short*)(theBitmap->Data))[dataCounter])=theBitmap->Data[dataCounter]<<offset;
// 					dataCounter++;
// 					
					gflGetColorAt(theBitmap, j, w, &gfl_color);
					gflColor2.Blue=gfl_color.Red<<offset;
					gflColor2.Green=gfl_color.Green<<offset;
					gflColor2.Red=gfl_color.Blue<<offset;
					gflColor2.Alpha=gfl_color.Alpha<<offset;
					gflSetColorAt(theBitmap, j, w, &gflColor2);
				}
			}
			}*/
			
		}
		break;
		
		
		
		case GFC_S3TCDX1:
		{
			
			//resize to the next divisible by 4
			
			resizeToX=getNextDivisibleBy4(theBitmap.getWidth());
			resizeToY=getNextDivisibleBy4(theBitmap.getHeight());
			/*FIL_COLOR gfl_color;
			FIL_ERROR error;
			gfl_color.Red=gfl_color.Blue=gfl_color.Green=0.2; */
			
			texCoords.x=0;
			texCoords.y=0;
			texCoords.w=theBitmap.getWidth()/(float)resizeToX;
			texCoords.h=theBitmap.getHeight()/(float)resizeToY;
			
			//printf("generatedtexCoords: %f %f %f %f\n",texCoords.x, texCoords.y, texCoords.w, texCoords.h);

			quadSizeX=theBitmap.getWidth();
			quadSizeY=theBitmap.getHeight();
			
			if(resizeToX!=theBitmap.getWidth() || resizeToY!=theBitmap.getHeight())
			{
				printf("Resizing canvas to %ix%i\n",resizeToX,resizeToY);
				FIBITMAP *tmpBitmap;
				RGBQUAD c;
				c.rgbRed = 0x00;
				c.rgbBlue = 0x00;
				c.rgbBlue = 0x00;
				c.rgbReserved = 0x00;

				tmpBitmap=FreeImage_EnlargeCanvas(theBitmap, 0,0,resizeToX-theBitmap.getHeight(), resizeToY-theBitmap.getWidth(), &c, FI_COLOR_IS_RGB_COLOR);
				
				//error=gflResizeCanvas(theBitmap,NULL,resizeToX,resizeToY,FIL_CANVASRESIZE_TOPLEFT,&gfl_color);
				if(tmpBitmap==NULL)
				{
					printf("EnlargeCanvas error\n");
				}
				else{
					theBitmap=tmpBitmap;
				}
			}
			
			
			
		}
		break;
		}
		
		
		sizeX=theBitmap.getWidth();
		sizeY=theBitmap.getHeight();
		
		
		//bits per component depends on the type of image.
		switch(theBitmap.getImageType()){
			case FIT_BITMAP:
				
				switch(theBitmap.getBitsPerPixel()){
					case 24:
						bitDepth=8;
						numOfComponents=3;
					break;
					
					case 32:
						bitDepth=8;
						numOfComponents=4;
					default:
						bitDepth=0;
						numOfComponents=0;
			
				}
				break;
			default:
				bitDepth=8;
				break;
		}
		
		//TODO: This would need to be stored before doing any color conversions (upscaling to 16bit for example), which we arent doing yet.
		originalBitDepth=bitDepth;
		switch(theBitmap.getColorType())
		{
			case FIC_RGB:
				originalNumOfComponents=3;
			break;
			
			case FIC_RGBALPHA:
				originalNumOfComponents=4;
			break;
			
			case FIC_PALETTE:
				originalNumOfComponents=1;
			break;
		}
		
		//format=info.FormatName;
		format = FreeImage_GetFormatFromFIF(fif);
		formatDescription=FreeImage_GetFIFDescription(fif);
		compressionDescription="";
		
		//printf("getting metadata\n");
		/*if(gflBitmapHasEXIF(theBitmap)==GFC_TRUE)
		{
		    //printf("Inside has exif\n");
			FIL_EXIF_DATA *exif=gflBitmapGetEXIF(theBitmap,0);
			if(exif){
			printf("MetaData: %i items (%i in map)\n",exif->NumberOfItems,metaData.size());
			//std::multimap<std::string,std::string>::iterator it=metaData.begin();
			for(int i=0;i<exif->NumberOfItems;i++)
			{
				
				//metaData[exif->ItemsList[i].Name]=exif->ItemsList[i].Value;
				//it=metaData.insert(it,std::pair<std::string,std::string>(exif->ItemsList[i].Name,exif->ItemsList[i].Value));
				metaData.insert(std::pair<std::string,std::string>(exif->ItemsList[i].Name,exif->ItemsList[i].Value));

			}
			
			
			gflFreeEXIF(exif);
			}
		}*/
		//printf("done metadata\n");
		
		//printf("getting metadata iptc\n");
		/*if(gflBitmapHasIPTC(theBitmap)==FIL_TRUE)
		{
			FIL_IPTC_DATA *iptc=gflBitmapGetIPTC(theBitmap);
			if(iptc){
			std::multimap<std::string,std::string>::iterator it=metaData.begin();
			
			for(int i=0;i<iptc->NumberOfItems;i++)
			{
				//metaData[iptc->ItemsList[i].Name]=iptc->ItemsList[i].Value;
				it=metaData.insert(it,std::pair<std::string,std::string>(iptc->ItemsList[i].Name,iptc->ItemsList[i].Value));
			}
			
			//printf("MetaData: %i items (%i in map)\n",iptc->NumberOfItems,metaData.size());
			gflFreeIPTC(iptc);
			}
		}*/
		//printf("done metadata iptc\n");
		
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
            frameInfo.format=GL_BGR;
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
        frameInfo.dataPointer=theBitmap.accessPixels();
        texCoords.x=0;
        texCoords.y=0;
        texCoords.w=theBitmap.getWidth();
        texCoords.h=theBitmap.getHeight();
        break;

    case GFC_8BPC:
		
		//printf("Num of components: %i\n",numOfComponents);
        switch (numOfComponents) {
        case 1:
            frameInfo.format=GL_LUMINANCE8;
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
            frameInfo.format=GL_LUMINANCE;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_LUMINANCE8;
            break;
        }
        frameInfo.target=GL_TEXTURE_RECTANGLE_ARB;
        frameInfo.dataPointer=theBitmap.accessPixels();
        texCoords.x=0;
        texCoords.y=0;
        texCoords.w=theBitmap.getWidth();
        texCoords.h=theBitmap.getHeight();
        break;

    case GFC_16HALF:
    case GFC_16BPC:
        switch (numOfComponents) {
        case 1:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=bitDepth<=8?GL_UNSIGNED_BYTE:GL_UNSIGNED_SHORT;
            frameInfo.internalFormat=GL_LUMINANCE16F_ARB;

            break;
        case 3:
            frameInfo.format=GL_BGR;
            frameInfo.dataType=bitDepth<=8?GL_UNSIGNED_BYTE:GL_UNSIGNED_SHORT;
            frameInfo.internalFormat=GL_RGB16F_ARB;
            break;
        case 4:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=bitDepth<=8?GL_UNSIGNED_BYTE:GL_UNSIGNED_SHORT;
            frameInfo.internalFormat=GL_RGBA16F_ARB;
            break;
        default:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=bitDepth<=8?GL_UNSIGNED_BYTE:GL_UNSIGNED_SHORT;
            frameInfo.internalFormat=GL_RGBA16F_ARB;
            break;
        }
        frameInfo.target=GL_TEXTURE_RECTANGLE_ARB;
        frameInfo.dataPointer=theBitmap.accessPixels();
        texCoords.x=0;
        texCoords.y=0;
        texCoords.w=theBitmap.getWidth();
        texCoords.h=theBitmap.getHeight();
        break;

    case GFC_S3TCDX1:

        switch (numOfComponents) {
        case 1:
            frameInfo.format=GL_BGRA;
            frameInfo.dataType=GL_UNSIGNED_BYTE;
            frameInfo.internalFormat=GL_LUMINANCE8;
            break;
        case 3:
            frameInfo.format=GL_BGR;
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
        frameInfo.dataPointer=theBitmap.accessPixels();
		//texCoords.x=0;
        //texCoords.y=0;
        //texCoords.w=1;
        //texCoords.h=1;
        break;
    }
		
		//fill the channel names
		switch(theBitmap.getColorType())
		{
			case FIC_RGB:
				channelNames.push_back("RGB");
			break;
			
			case FIC_RGBALPHA:
				channelNames.push_back("RGBA");
			break;
			
			case FIC_PALETTE:
				channelNames.push_back("PALETTE");
			break;
		}
		
		/*if(format=="DPX" || format=="dpx")
		{
			printf("IT'S A DPX, GET METADATA!!!\n");
		}*/
		
		return 0; //return 0 to indicate all went well
	}
	
	
	#endif //USEFREEIMAGE
	return 1;
}

int gfcImageLoaderFIL::peek(gfcLoadParams params, gfcPeekInfo* results)
{
	return 0;
}

void* gfcImageLoaderFIL::getPixelPointer()
{
	#ifdef USEFREEIMAGE
	return theBitmap.accessPixels();
	#endif
	return NULL;
}

void gfcImageLoaderFIL::releaseMemory()
{	
	#ifdef USEFREEIMAGE
	 if(theBitmap)
	 	theBitmap.clear();
	#endif
}

std::vector< std::string > gfcImageLoaderFIL::getChannelNames()
{
	return channelNames;
}

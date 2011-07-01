#include "gfcimageloadergfl.h"
#include "UIConstants.h"
#include "gfcStructures.h"
#include "gfcframe.h"

#include "gfctrackmanager.h"
extern gfcTrackManager trackManager;

gfcImageLoaderGFL::gfcImageLoaderGFL()
 : gfcImageLoader()
{
	theBitmap=NULL;
}


gfcImageLoaderGFL::~gfcImageLoaderGFL()
{
}


int gfcImageLoaderGFL::fillProcessor(gfcImageProcessor& processor)
{
	return 0;
}


int gfcImageLoaderGFL::load(gfcLoadParams params)
{
	GFL_LOAD_PARAMS load_option; 
	GFL_FILE_INFORMATION info;
	GFL_ERROR error; 
	
	//printf("GFL load\n");
	
	gflGetDefaultLoadParams( &load_option ); 
	load_option.ColorModel = GFL_BGRA;
	load_option.Flags|=GFL_LOAD_FORCE_COLOR_MODEL;
	load_option.Flags|= GFL_LOAD_METADATA;
	//load_option.Origin=GFL_BOTTOM_LEFT;
	int resizeToX=0;
	int resizeToY=0;
	
	if(params.compressed==GFC_16BPC || params.compressed==GFC_16HALF)
	{
		load_option.Flags|= GFL_LOAD_ORIGINAL_DEPTH;
	}

	
	
	if (sett.balanceReads)
	{
		boost::try_mutex::scoped_lock lock ( trackManager.readMutex );
		while (trackManager.ioBusy!=0)
		{
			balanceReadCond.wait(lock);
		}
		trackManager.ioBusy=1;
		error = gflLoadBitmap(params.fileName.c_str(), &theBitmap, &load_option, &info ); 
		trackManager.ioBusy=0;
		balanceReadCond.notify_one();
		
	}
	else
	{
		error = gflLoadBitmap(params.fileName.c_str(), &theBitmap, &load_option, &info ); 
	}
	
	if(error)
	{
		printf ( "GFL ERROR while loading file %s: %s\n",params.fileName.c_str(),gflGetErrorString ( error ) );
		loadErrorString=gflGetErrorString ( error );
		if (error==GFL_ERROR_NO_MEMORY)
		{
			return GFCFRAME_LOADERROR_NO_MORE_MEMORY;
		}
		else
				
		return GFCFRAME_LOADERROR_UNKNOWN;
	}
	else
	{
		
		
		
		
		
		if(params.scale!=100)
		{
			gflResize(theBitmap,NULL,theBitmap->Width*params.scale/100.0,theBitmap->Height*params.scale/100.0,params.filterType==0?GFL_RESIZE_QUICK:GFL_RESIZE_BILINEAR,0);
		}
		
		//crop after scale!
		if(params.crop)
		{
			GFL_RECT cropRect;
			cropRect.x=params.aoi.x;
			cropRect.y=theBitmap->Height-params.aoi.y-params.aoi.h; //we need to invert the y coordinate since the image has a top origin.
			cropRect.w=params.aoi.w;
			cropRect.h=params.aoi.h;
			//printf("cropping to %i %i %i %i\n",cropRect.x,cropRect.y,cropRect.w,cropRect.h);
			gflCrop(theBitmap,NULL,&cropRect);
		}
		
		resizeToX=theBitmap->Width;
		resizeToY=theBitmap->Height;
		
		quadSizeX=theBitmap->Width;
		quadSizeY=theBitmap->Height;
		
		switch(params.compressed)
		{
		case GFC_16HALF:
		case GFC_16BPC: //if loading to is 16bit up each component in each pixel to the correct value
		{
			if(info.BitsPerComponent<=8)
			{
				//do nothing
			}
			else{
			int offset=16-info.BitsPerComponent;
			
			int imageHeight=theBitmap->Height;
			int imageWidth=theBitmap->Width;
			GFL_COLOR gfl_color,gflColor2; 
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
			}
			
		}
		break;
		
		
		
		case GFC_S3TCDX1:
		{
			
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
			
			//printf("generatedtexCoords: %f %f %f %f\n",texCoords.x, texCoords.y, texCoords.w, texCoords.h);

			quadSizeX=theBitmap->Width;
			quadSizeY=theBitmap->Height;
			
			if(resizeToX!=theBitmap->Width || resizeToY!=theBitmap->Height)
			{
				printf("Resizing canvas to %ix%i\n",resizeToX,resizeToY);
				error=gflResizeCanvas(theBitmap,NULL,resizeToX,resizeToY,GFL_CANVASRESIZE_TOPLEFT,&gfl_color);
				if(error)
				{
					printf("ResizeCanvas error: %s\n",gflGetErrorString(error));
				}
			}
			
			
			
		}
		break;
		}
		
		
		sizeX=theBitmap->Width;
		sizeY=theBitmap->Height;
		
// 		if(params.compressed!=GFC_S3TCDX1)
// 		{
// 			texCoords.x=0;
// 			texCoords.y=0;
// 			//texCoords.w=theBitmap->Width;
// 			//texCoords.h=theBitmap->Height;
// 			
// 		}
		bitDepth=theBitmap->BitsPerComponent;
		originalBitDepth=info.BitsPerComponent;
		originalNumOfComponents=info.ComponentsPerPixel;
		numOfComponents=theBitmap->ComponentsPerPixel;
		format=info.FormatName;
		formatDescription=info.Description;
		compressionDescription=info.CompressionDescription;
		//printf("getting metadata\n");
		if(gflBitmapHasEXIF(theBitmap)==GFL_TRUE)
		{
		    //printf("Inside has exif\n");
			GFL_EXIF_DATA *exif=gflBitmapGetEXIF(theBitmap,0);
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
		}
		//printf("done metadata\n");
		
		//printf("getting metadata iptc\n");
		if(gflBitmapHasIPTC(theBitmap)==GFL_TRUE)
		{
			GFL_IPTC_DATA *iptc=gflBitmapGetIPTC(theBitmap);
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
		}
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
        frameInfo.dataPointer=theBitmap->Data;
        texCoords.x=0;
        texCoords.y=0;
        texCoords.w=theBitmap->Width;
        texCoords.h=theBitmap->Height;
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
        frameInfo.dataPointer=theBitmap->Data;
		//texCoords.x=0;
        //texCoords.y=0;
        //texCoords.w=1;
        //texCoords.h=1;
        break;
    }
		
		//fill the channel names
		switch(theBitmap->Type)
		{
			case GFL_RGB:
			case GFL_BGR:
				channelNames.push_back("RGB");
			break;
			
			case GFL_RGBA:
			case GFL_BGRA:
				channelNames.push_back("RGBA");
			break;
			
			case GFL_GREY:
				channelNames.push_back("GREY");
			break;
		}
		
		/*if(format=="DPX" || format=="dpx")
		{
			printf("IT'S A DPX, GET METADATA!!!\n");
		}*/
		
		return 0; //return 0 to indicate all went well
	}
	
	return 1;
	
}

int gfcImageLoaderGFL::peek(gfcLoadParams params, gfcPeekInfo* results)
{
	return 0;
}

void* gfcImageLoaderGFL::getPixelPointer()
{
	return theBitmap->Data;
}

void gfcImageLoaderGFL::releaseMemory()
{	
	 if(theBitmap)
	 gflFreeBitmap(theBitmap);
	 
	 theBitmap=NULL;
}

std::vector< std::string > gfcImageLoaderGFL::getChannelNames()
{
	return channelNames;
}

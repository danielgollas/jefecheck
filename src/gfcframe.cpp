#include "gfcframe.h"

#include "exrWindow.h"

#include <iostream>
#include <sstream>
#include <iomanip>

extern ExrWindow ew;
extern bool mainWindowExists;
extern bool npotTextures;
extern boost::mutex gGLMutex;
extern void* gGLContext;
extern bool gResizeTrigger;

int TEST_GLOBAL_loaderToUse=GFCLOADER_DPX;

/****/
//#include "gfcframeslice.h"
#include "gfcimageloaderdpx.h"
#include "gfcimageloadergfl.h"
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
			theImageLoader=new gfcImageLoaderEXR();
			//theImageLoader=new gfcImageLoaderGFL();
			break;

		case GFCLOADER_GFL:
	//		printf("Unsing GFL\n");
			theImageLoader=new gfcImageLoaderGFL();
			break;

		case GFCLOADER_FIL:
	//		printf("Unsing GFL\n");
			theImageLoader=new gfcImageLoaderFIL();
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

GLuint gfcFrame::generateTexture()
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
	//printf("sizeX, sizeY, dataType: %i %i %i\n",sizeX,sizeY,info.dataType);
	glTexImage2D ( info.target,0,info.internalFormat,sizeX,sizeY,0,info.format,info.dataType,info.dataPointer);
	/*
	glFinish();
	timer.stop();
	timer.print();
	*/

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



/*RawFrame gfcFrame::loadFrame ( char trackID, std::string name, int scale, int cropX, int cropY, int cropW, int cropH,int filterType,float gamma, std::vector<int> lutList, int compressed, float exposition, float defog, float kneeH, float kneeL, int channel )
{ //OLD STUFF***
	RawFrame tmpRawFrameComplete;
	return tmpRawFrameComplete;
}*/

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

/*
// int gfcFrame::loadPreviewFrame ( char trackID, std::string filename, int ascale, int afilterType,float agamma,float exposition, float defog, float kneeH, float kneeL, int channel )
// {//OLD STUFF***
// 	RawFrame rawFrame;
// 	gfcRawFrameLoadParams loadParams;
//
// 	loadParams;
//
// 	float gammaLUT[65536];
// 	if ( agamma!=1 )
// 	{
//
// 		//Get the bitdepth from first frame;
// 		GFLC_FILE_INFORMATION info ( filename.c_str() );
// 		printf ( "Got File Info In sequence loading: %ix%ix%i\n",info.getHeight(),info.getWidth(),info.getBitsPerComponent() );
// 		//calculate the size of the lut based on bitdepth
// 		int numberOfEntries=1<<info.getBitsPerComponent();
// 		printf ( "Number of Entries for gammaLUT: %i\n",numberOfEntries );
//
// 		//create table, remember to delete de memory allocation at function's end.
// 		//gammaLUT=new float[numberOfEntries];
// 		//fill the table
// 		float gammaExp=1.0/agamma;
// 		for ( int i=0;i<numberOfEntries;i++ )
// 		{
// 			gammaLUT[i]=pow ( i/ ( float ) numberOfEntries,gammaExp );
// 			printf ( "LUT: %i=%f\n",i,gammaLUT[i] );
// 		}
//
// 	}
//
// 	loadParams.trackID=trackID;
// 	loadParams.name=filename;
// 	loadParams.scale=ascale;
// 	loadParams.filterType=afilterType;
// 	loadParams.gamma=agamma;
// 	loadParams.gammaLUT=gammaLUT;
// 	loadParams.exposition=exposition;
// 	loadParams.defog=defog;
// 	loadParams.kneeH=kneeH;
// 	loadParams.kneeL=kneeL;
// 	loadParams.channel=channel;
//
// 	rawFrame.loadFrame ( loadParams );
//
// 	//WE LOAD AND GENERATE THE PREVIEW FRAME IN ALMOST THE EXACT SAME WAY WE LOAD THE SEQUENCE TO BE CONSISTENT
// 	switch ( trackID )
// 	{
// 		case 'A':
// 			mw.vp->trackA.generateTexture ( &rawFrame,this );
// 			break;
// 		case 'B':
// 			mw.vp->trackB.generateTexture ( &rawFrame,this );
// 			break;
// 		case 'C':
// 			mw.vp->trackB.generateTexture ( &rawFrame,this );
// 			break;
// 		case 'D':
// 			mw.vp->trackB.generateTexture ( &rawFrame,this );
// 			break;
// 	}
//
// 	if ( sett.recentBrowsed.size() <sett.maxRecentBrowsed )
// 	{
// 		//check if the stack is not in the vector already.
// 		bool alreadyInRecent=false;
// 		for ( int i=0;i<sett.recentBrowsed.size();i++ )
// 		{
// 			if ( sett.recentBrowsed[i]==filename )
// 			{
// 				alreadyInRecent=true;
// 				break;
// 			}
// 		}
//
// 		if ( !alreadyInRecent )
// 			sett.recentBrowsed.push_back ( filename );
// 	}
// 	else
// 	{
// 		{
// 			bool alreadyInRecent=false;
// 			for ( int i=0;i<sett.recentBrowsed.size();i++ )
// 			{
// 				if ( sett.recentBrowsed[i]==filename )
// 				{ //if the stack is full, and the name already exists, then delete it from where it was and push it at the top.
// 					alreadyInRecent=true;
// 					sett.recentBrowsed.erase ( sett.recentBrowsed.begin() +i );
// 					sett.recentBrowsed.push_back ( filename );
// 					break;
// 				}
// 			}
//
// 			if ( !alreadyInRecent ) //if it's not in the recent, then erase the first one and push the new one.
// 			{
// 				sett.recentBrowsed.erase ( sett.recentBrowsed.begin() );
// 				sett.recentBrowsed.push_back ( filename );
// 			}
//
//
// 		}
// 	}
//
// 	//generateTexture(&rawFrame, this);
//
//
// 	//     if(filename.size()==0)
// 	//         return 0;
// 	//
// 	//     GFLC_LOAD_PARAMS load_option;
// 	//     GFLC_BITMAP *bitmap;
// 	//     GFLC_FILE_INFORMATION info;
// 	//     GFL_ERROR gflError;
// 	//     int sizeDiffX=0;
// 	//     int sizeDiffY=0;
// 	//     int resizeToX=2048;
// 	//     int resizeToY=2048;
// 	//
// 	//
// 	//     //*****exr Vars*****
// 	//     Box2i displayWindow;
// 	//     Box2i dataWindow;
// 	//     int lx = -1;
// 	//     int ly = -1;
// 	//     float pixelAspect;
// 	//
// 	//     int imageSizeKB=0;
// 	//
// 	//     char blobFormat[4];
// 	//     GLint glTextureFormat;
// 	//     int glChannels;
// 	//     GLint glError;
// 	//     //***************
// 	//
// 	//
// 	//     // Read a file into image object
// 	//
// 	//
// 	//
// 	//     // printf("&name[name.size()-4]: %s", &name[name.size()-4]);
// 	//
// 	//     if(strcmp(&filename[filename.size()-4],".exr")==0)
// 	//     {
// 	//         printf("\n\nOpenEXR File!\n\n");
// 	//         //read the exr and then use imageMagick to read from the screen pixels
// 	//
// 	//         if(exrChannelList.size()>0)
// 	//         {
// 	//             ImageView *exrimage;
// 	//             Imf::Array<Imf::Rgba>	exrpixels;
// 	//             if(strcmp(exrChannelList[channel].prefix,"RGB")==0 || strcmp(exrChannelList[channel].prefix,"RGBA")==0)
// 	//             {
// 	//                 loadImage(&filename[0],
// 	//                           displayWindow,
// 	//                           dataWindow,
// 	//                           pixelAspect,
// 	//                           exrpixels);
// 	//                 //load with color interface
// 	//                 glChannels=strlen(exrChannelList[channel].prefix);
// 	//                 glTextureFormat=(strcmp(exrChannelList[channel].prefix,"RGBA")==0)?GL_RGBA:GL_RGB;
// 	//                 if(exrChannelList[channel].numOfComponents==4)
// 	//                 {
// 	//                     strcpy(blobFormat,"RGBA");
// 	//
// 	//                 }
// 	//                 else
// 	//                 {
// 	//                     strcpy(blobFormat,"RGB");
// 	//                 }
// 	//
// 	//             }
// 	//             else
// 	//             {
// 	//                 loadImageChannelGroups(&filename[0],
// 	//                                        channel,
// 	//                                        displayWindow,
// 	//                                        dataWindow,
// 	//                                        pixelAspect,
// 	//                                        exrpixels);
// 	//                 //load with channel interface
// 	//                 glChannels=exrChannelList[channel].numOfComponents;
// 	//                 glTextureFormat=(exrChannelList[channel].numOfComponents==4)?GL_RGBA:GL_RGB;
// 	//
// 	//                 if(exrChannelList[channel].numOfComponents==4)
// 	//                 {
// 	//                     strcpy(blobFormat,"RGBA");
// 	//                 }
// 	//                 else
// 	//                 {
// 	//                     strcpy(blobFormat,"RGB");
// 	//                 }
// 	//             }
// 	//
// 	//
// 	//
// 	//
// 	//             int w  = displayWindow.max.x - displayWindow.min.x + 1;
// 	//             int h  = displayWindow.max.y - displayWindow.min.y + 1;
// 	//             int dw = dataWindow.max.x - dataWindow.min.x + 1;
// 	//             int dh = dataWindow.max.y - dataWindow.min.y + 1;
// 	//             int dx = dataWindow.min.x - displayWindow.min.x;
// 	//             int dy = dataWindow.min.y - displayWindow.min.y;
// 	//
// 	//             int mw = max (200, w);
// 	//
// 	//             exrimage = new ImageView (5 + (mw - w) / 2, 105,
// 	//                                       w, h,
// 	//                                       "",
// 	//                                       exrpixels,
// 	//                                       dw, dh,
// 	//                                       dx, dy,
// 	//                                       0,
// 	//                                       0,
// 	//                                       0,
// 	//                                       5.5);
// 	//
// 	//             exrimage->setExposure(exposition);
// 	//             exrimage->setDefog(defog);
// 	//             exrimage->setKneeHigh( kneeH);
// 	//             exrimage->setKneeLow( kneeL);
// 	//             exrimage->updateScreenPixels();
// 	//             //image.read(w,h,blobFormat,Magick::CharPixel,exrimage->_screenPixels );
// 	//         }
// 	//     }
// 	//
// 	//
// 	//     gfcFrameSlice tmpSlice;
// 	//     tmpSlice.fileName=filename;
// 	//     tmpSlice.filterType=afilterType;
// 	//     tmpSlice.gamma=agamma;
// 	//     tmpSlice.scale=ascale;
// 	//     tmpSlice.cropX=-1;
// 	//     tmpSlice.cropY=-1;
// 	//     tmpSlice.cropH=-1;
// 	//     tmpSlice.cropW=-1;
// 	//
// 	//     tmpSlice.trackID=trackID;
// 	//     tmpSlice.compressed=false;
// 	//
// 	//     glDeleteTextures(1,&textureID);
// 	//
// 	//     if(tmpSlice.load()!=-1)
// 	//     {
// 	//         GLuint tmpTextureID;
// 	//         glGenTextures(1,&tmpTextureID);
// 	//         textureID=tmpTextureID;
// 	//
// 	//         glBindTexture(sett.textureRectangles?GL_TEXTURE_RECTANGLE_ARB:GL_TEXTURE_2D, textureID);
// 	//
// 	//         glTexParameterf(sett.textureRectangles?GL_TEXTURE_RECTANGLE_ARB:GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
// 	//         glTexParameterf(sett.textureRectangles?GL_TEXTURE_RECTANGLE_ARB:GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
// 	//         glTexParameterf(sett.textureRectangles?GL_TEXTURE_RECTANGLE_ARB:GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
// 	//         glTexParameterf(sett.textureRectangles?GL_TEXTURE_RECTANGLE_ARB:GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
// 	//         glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
// 	//
// 	//         int tmpChannels=tmpSlice.bitmap->getComponentsPerPixel();
// 	//         GLuint tmpFormat=tmpChannels==3?GL_BGR:GL_BGRA;
// 	//         channels=tmpChannels;
// 	//         bpc=tmpSlice.info.getBitsPerComponent();
// 	//         strcpy(format,tmpSlice.info.getNameOfFormat());
// 	//         strcpy(formatDescription,tmpSlice.info.getDescription());
// 	//         strcpy(compressionDescription,tmpSlice.info.getCompressionDescription());
// 	//         //info = tmpSlice.info;
// 	//         if(sett.renderingEngine==0)
// 	//         {
// 	//             glTexImage2D(sett.textureRectangles?GL_TEXTURE_RECTANGLE_ARB:GL_TEXTURE_2D,0,tmpChannels, tmpSlice.originalSizeX, tmpSlice.originalSizeY,0,tmpFormat,GL_UNSIGNED_BYTE,tmpSlice.bitmap->getDataPtr());
// 	//             delete tmpSlice.bitmap;
// 	//             printf("Preview texture info:\n  originalSizeX: %i\n  originalSizeY: %i\n",tmpSlice.originalSizeX,tmpSlice.originalSizeY);
// 	//         }
// 	//         else
// 	//         {
// 	//             //if npot textures are not supported, create a square texture of the size it has to be, and then subimage the real texture on there
// 	//              int dummySize=tmpSlice.originalSizeX*tmpSlice.originalSizeY*tmpChannels;
// 	//              printf("Dummy: %ix%ix%i\n",tmpSlice.originalSizeY,tmpSlice.originalSizeX,tmpChannels);
// 	//              float *dummyTex= new float[dummySize];
// 	//              for(int i=dummySize-1; i>=0; i--)
// 	//                  dummyTex[i]=0;
// 	//
// 	//              printf("DummyTex alocated normally... dummySize: %i\n",dummySize);
// 	//
// 	//              glTexImage2D(GL_TEXTURE_2D, 0,tmpChannels, tmpSlice.sizeX, tmpSlice.sizeY,0,tmpFormat,GL_UNSIGNED_BYTE,dummyTex);
// 	//              glTexSubImage2D(GL_TEXTURE_2D,0, 0,0,tmpSlice.originalSizeX, tmpSlice.originalSizeY,tmpFormat, GL_UNSIGNED_BYTE, tmpSlice.bitmap->getDataPtr());
// 	//              delete[] dummyTex;
// 	//
// 	//         }
// 	//     }
// 	//     else //preview frame not loaded correctly!
// 	//     {
// 	//         printf("ERROR LOADING PREVIEW FRAME\n");
// 	//         return 1;
// 	//     }
// 	//     originalSizeX=tmpSlice.originalSizeX;
// 	//     originalSizeY=tmpSlice.originalSizeY;
// 	//     sizeX=tmpSlice.sizeX;
// 	//     sizeY=tmpSlice.sizeY;
// 	//     loaded=true;
// 	//
// 	//     //if the preview frame was loaded correctly, then we add it to the recently browsed vector.
// 	//     if(sett.recentBrowsed.size()<sett.maxRecentBrowsed)
// 	//     {
// 	//         //check if the stack is not in the vector already.
// 	//         bool alreadyInRecent=false;
// 	//         for(int i=0;i<sett.recentBrowsed.size();i++)
// 	//         {
// 	//             if(sett.recentBrowsed[i]==filename)
// 	//             {
// 	//                 alreadyInRecent=true;
// 	//                 break;
// 	//             }
// 	//         }
// 	//
// 	//         if(!alreadyInRecent)
// 	//             sett.recentBrowsed.push_back(filename);
// 	//     }
// 	//     else
// 	//     {
// 	//         {
// 	//             bool alreadyInRecent=false;
// 	//             for(int i=0;i<sett.recentBrowsed.size();i++)
// 	//             {
// 	//                 if(sett.recentBrowsed[i]==filename)
// 	//                 { //if the stack is full, and the name already exists, then delete it from where it was and push it at the top.
// 	//                     alreadyInRecent=true;
// 	//                     sett.recentBrowsed.erase(sett.recentBrowsed.begin()+i);
// 	//                     sett.recentBrowsed.push_back(filename);
// 	//                     break;
// 	//                 }
// 	//             }
// 	//
// 	//             if(!alreadyInRecent) //if it's not in the recent, then erase the first one and push the new one.
// 	//             {
// 	//                 sett.recentBrowsed.erase(sett.recentBrowsed.begin());
// 	//                 sett.recentBrowsed.push_back(filename);
// 	//             }
// 	//
// 	//
// 	//         }
// 	//     }
//
// 	return 0;
// }
*/

void gfcFrame::releaseMemory()
{
	if(theImageLoader)
		theImageLoader->releaseMemory();
}
/*
const gfcFrame & gfcFrame::operator =(const gfcFrame & frame)
{
	theImageLoader=frame.theImageLoader;
	textureID=frame.textureID;
	
	loaded=frame.loaded;
	sizeX=frame.sizeX;
	sizeY=frame.sizeY;
	channels=frame.channels;
	bpc=frame.bpc;
	
	indexNumber=frame.indexNumber;
	fileName=frame.fileName;
	metaData=frame.metaData;
	compressed=frame.compressed;
	//dpxSlice=frame.dpxSlice;
	
	
}*/


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







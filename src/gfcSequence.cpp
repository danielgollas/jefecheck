// SPDX-License-Identifier: GPL-2.0-or-later


// gfcSequence.cpp: implementation of the gfcSequence class.
//
//////////////////////////////////////////////////////////////////////

#include <glad/glad.h>
#include <functional>
#include <algorithm>
#include <atomic>
#include "gfcSequence.h"
#include "gfcMediaKinds.h"
#include "gfcMovieReader.h"
#include "gfcAudioEngine.h"
#include "ui/IApplication.h"
namespace { jefe::ui::IApplication& app() { return jefe::ui::IApplication::instance(); } }
#include <string.h>
#include <stdio.h>
#include <vector>
#include <ctype.h>
#include <sys/stat.h>
//#include "gfcframeslice.h"
#ifdef WIN32
#else
#endif

#include <stdlib.h>

#include "gfcStructures.h"

#include "gfcimageloaderdpx.h"
#include "gfcimageloaderexr.h"



#include "gfctrackmanager.h"
extern gfcTrackManager trackManager;
#include "gfcplaybackmanager.h"
extern gfcPlaybackManager playbackManager;

#include "gfcmemorymanager.h"
extern gfcMemoryManager memoryManager;

#include "gfcFrameCache.h"
#include <chrono>
#ifdef __APPLE__
#include <pthread/qos.h>
#endif

namespace {
std::atomic<int> g_testDecodeDelayMs{0};
std::atomic<int> g_testAudioBlockDelayMs{0};
std::atomic<long> g_loadAnnouncements{0};
}  // namespace

// A queued frame that will never be uploaded: give back its pixels (staging
// buffer, IOSurface) and its loader.
static void discardRawFrame(gfcFrame& f)
{
	f.releaseMemory();
	delete f.theImageLoader;
	f.theImageLoader=NULL;
	f.loaded=false;
}


#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;


#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

extern bool mainWindowExists;
extern bool npotTextures;
extern std::mutex gGLMutex;
extern void* gGLContext;
extern bool gResizeTrigger;
std::vector<int> dummyVectorForLut;

//macro to assiste with PBO texture uploads
#define BUFFER_OFFSET(i) ((char *)NULL + (i))

#ifdef WIN32
// glext.h removed — GLAD provides GL extension definitions
#endif


//memory mutex and stuff
bool gMTOutOfMemory=false;
std::mutex gNoMoreRamMutex;
std::condition_variable gNoMoreRamCondition;

bool gLoadCanceled=false;
int dummyInt2;
std::mutex rawMutexA;
std::mutex loadingOutOfRamMutex;
std::vector<ExrChannelInfo> exrChannelList;

bool gOutOfMemory=false;
bool gLoadingMemoryError=false;
int gRangeBegin=1;
int gRangeEnd=1;


extern double getFreeMem();
extern void IdleFunc ( void* pData );

bool maxFramesInRamReached() {

	return false;
}



const char* gfcSequenceInfo::getString() {
	std::string tmp;
	//sprintf(tmp,"File: %s\nFormat: %s\nFrames: %i\nChannels: %i\nBPC: %i\nw: %i\nh: %i\n",fileName,fileFormat,numFrames,channels,bpp,sizeX,sizeY);
	tmp="File:";
	//tmp+=fileName;
	//sprintf(tmp,"File: %s","fileFormat");

	//printf("%s",tmp);
	return tmp.c_str();
}

void resizeAllSldrs();


void findSequence (std::vector<std::string> &refFiles, std::string inputFilename, std::string &label, int &startNum, int &endNum) {

	/*find the position of the numeration of a given filename, i.e. efect45.0056.dpx will return the start and end positions of the 0056,
	if the filename is of the form name.ext.#### it will also indicate that the extension order is inverted
	first will return the first part of the filename befor the numeration and last the extension if it is not inverted (otherwise it will be
	a part of first);
	*/

	//printf("Starting Find Sequence\n");
	refFiles.clear();

	std::string theFilename;
	//printf("inputFilename:%s\n",inputFilename.c_str());
	//NOTE: If we can't find the file, try to find it in the search paths if the option is enabled.
	if(!fileExists(inputFilename) && sett.useSearchPaths){
		theFilename=findFileInSearchPaths(inputFilename);
		//printf("theFilename:%s\n",theFilename.c_str());
	}
	else
	{
		theFilename=inputFilename;
	}

	// A movie is always one clip: never parse digits in its name as a frame number.
	// N copies of the path keep frames.size()/getNumFrames/framePathAt working untouched.
	if (gfcIsMovieFile(theFilename))
	{
		gfcMovieInfo info;
		int64_t n = 1;
		if (gfcMovieReader::probe(theFilename, info, nullptr) && info.frameCount > 0)
			n = info.frameCount;
		refFiles.assign(static_cast<size_t>(n), theFilename);
		label = theFilename;
		startNum = 1;
		endNum = static_cast<int>(n);
		return;
	}

	//printf("theFilename:%s\n",theFilename.c_str());
	#define MAXSTRINGLENGHTFORSEQUENCEDETECTION 16384
	char str[MAXSTRINGLENGHTFORSEQUENCEDETECTION]="";
	sprintf(str,"%s",theFilename.c_str());
	//	printf("str:%s\n",str);

	int tmpStringsLenght; //variable used to determine the size of the temporary strings used to construct the filenames, in the worst case, each string is the size of the input filename
	tmpStringsLenght=theFilename.length();

	if (tmpStringsLenght==0)
	{
		tmpStringsLenght=1;
	}

	//printf("creating strings of %i chars\n",tmpStringsLenght);

	/*char *name=new char[tmpStringsLenght];
	char *tmpName=new char[tmpStringsLenght];
	char *num=new char[tmpStringsLenght];
	char *nextString=new char[tmpStringsLenght];
	char *ext=new char[tmpStringsLenght];

	memset(name,0,tmpStringsLenght);
	memset(tmpName,0,tmpStringsLenght);
	memset(num,0,tmpStringsLenght);
	memset(nextString,0,tmpStringsLenght);
	memset(ext,0,tmpStringsLenght);*/



	char name[MAXSTRINGLENGHTFORSEQUENCEDETECTION]="";
	char tmpName[MAXSTRINGLENGHTFORSEQUENCEDETECTION]="";
	char num[MAXSTRINGLENGHTFORSEQUENCEDETECTION]="";
	char nextString[MAXSTRINGLENGHTFORSEQUENCEDETECTION]="";
	char ext[MAXSTRINGLENGHTFORSEQUENCEDETECTION]="";
	
	#undef MAXSTRINGLENGHTFORSEQUENCEDETECTION

	/*strcpy(name,"");
	strcpy(tmpName,"");
	strcpy(num,"");
	strcpy(nextString,"");*/

	int nextNum=0;
	
	int start=0, end=0, padding;
	//int startNum=0, endNum=0;
	bool extensionInverted;

	int i=strlen ( str );
	if ( isdigit ( str[i-1] ) )
		extensionInverted=true;

	bool foundLast=false;

	//find the start and end positions of the numeration in the string, the numeration is identified by a digit or by a #
	//printf("Analyzing characters: ");
	for ( i;i>0;i-- ) {
		//printf("%c",str[i-1]);
		if ( ( isdigit ( str[i-1] ) || str[i-1]=='#' ) && !foundLast ) {
			//printf("Found last number: %c at pos %i\n",str[i-1],i-1);
			end=i;
			foundLast=true;
		} else
			if ( !isdigit ( str[i-1] ) && str[i-1]!='#' && foundLast ) {
				//printf("Found first number: %c at pos %i\n",str[i-1],i-1);
				start=i+1;
				break;
			}

			if ( str[i-1]=='\\' || str[i-1]=='/' ) {
				//a backslash was found before the numeration, means that the file is probably not part of a sequence
				//therefor simply fill the names vector with one file with that name and return
				refFiles.push_back ( str );
				startNum=1;
				label=str;

				//cleanup temp strings
				/*delete [] name;
				delete [] tmpName;
				delete [] num;
				delete [] nextString;
				delete [] ext;*/

				return;
			}
	}

	padding=end-start+1;
	//printf("start: %i, end:%i\n",start,end);
	if ( start!=0 || ! ( start==end && start==0 ) ) {

		//if the numbers come first, then we need to change the order

		strncpy ( name,str,start-1 );
		strncpy ( ext,&str[end],strlen ( str )-end );
		strncpy ( num,&str[start-1],end- ( start-1 ) );

		//printf("name: %s\next:%s\nnum:%s\n",name, ext, num);
		bool poundNumbering=false;
		//printf("Pound numbering!\n");

		if ( num[0]=='#' )
			poundNumbering=true;

		if ( poundNumbering ) //if we have a poundNumbering then start the file finding at 0
			nextNum=0;
		else
			nextNum=atoi ( num );

		//printf ( "\n Sequence Info: \nfileName: %s\nFilename numStart/numEnd: %i/%i\nname: %s\next: %s\nnum: %s",str,start,end,name,ext,num );

		//construct the format string for padding
		//sould result in someting of the form %s%0paddingi%s

		sprintf ( nextString,"%s%%0%ii%s\0",name,padding,ext );
		//construct the filename
		sprintf ( tmpName,nextString,nextNum );
		//	printf("\nConstructed filename: %s\n",tmpName);

		//construct the generic filename with name, padding and ext using #
		{
			label=name;
			for (int i=0;i<padding;i++) {
				label+="#";
			}
			label+=ext;
		}
		if ( !poundNumbering ) { //if it's not a pound numbering, then start looking under the filename and above the filename
			//find file before the specified filename i-1,i-2... while i-x positive
			//printf("Looking for files below...\n");
			while ( ( FILE_EXISTS( tmpName ) ) && nextNum>=0 ) {
				//if ( fileExists ( tmpName ) )
				startNum=nextNum;
				sprintf ( nextString,"%s%%0%ii%s\0",name,padding,ext );
				//construct the filename
				--nextNum;
				sprintf ( tmpName,nextString,nextNum );
				//	printf("\nConstructed filename: %s\n",tmpName);
			}
			//printf("...found %i files below\n",nextNum);

			//find file before the specified filename i-1,i-2... while i-x positive
			nextNum=atoi ( num ) +1;
			sprintf ( tmpName,nextString,nextNum );
			//printf("\nConstructed filename: %s\n",tmpName);
			// The originally-picked frame exists (that's what triggered detection),
			// so endNum is at least atoi(num). Initializing to startNum was a bug
			// when there were frames below the pick: e.g. pick=0008 with 0001..0008
			// on disk → startNum=1 (down walk), endNum stayed at 1 because the up
			// walk starts at 0009 and immediately fails.
			endNum=atoi ( num );
			//printf("Looking for files above...\n",tmpName);
			while ( FILE_EXISTS( tmpName ) ) {
				endNum=nextNum;
				sprintf ( nextString,"%s%%0%ii%s",name,padding,ext );
				//construct the filename
				++nextNum;
				sprintf ( tmpName,nextString,nextNum );
				//printf("\nConstructed filename: %s\n",tmpName);
			}
			//printf("...Found %i files\n",nextNum);
		} else {//if it's pound numbering start looking for files at frame 0, after finding the first frame startNum (not always 0) find the last file
			// endNum
			while ( nextNum<999999999 ) {
				if ( fileExists ( tmpName ) ) {
					startNum=nextNum;
					break;
				}
				sprintf ( nextString,"%s%%0%ii%s",name,padding,ext );
				//construct the filename
				nextNum++;
				sprintf ( tmpName,nextString,nextNum );
				//	printf("\nConstructed filename: %s\n",tmpName);


			}
			while ( nextNum<999999999) { //find the last one, that is, till the nextNum file does not exist
				if ( !fileExists ( tmpName ) ) {
					endNum=nextNum;
					break;
				}
				sprintf ( nextString,"%s%%0%ii%s",name,padding,ext );
				//construct the filename
				nextNum++;
				sprintf ( tmpName,nextString,nextNum );
				//	printf("\nConstructed filename: %s\n",tmpName);


			}
		}

		i=refFiles.size();

		for ( i=startNum;i<=endNum;i++ ) {
			sprintf ( tmpName,nextString,i );
			refFiles.push_back ( tmpName );
			//printf("files[%i]: %s",i,&files[i][0]);
		}
	}

	//cleanup tmpstrings
	/*delete [] name;
	delete [] tmpName;
	delete [] num;
	delete [] nextString;
	delete [] ext;*/


	//printf("Found sequence: %i -> %i\n",startNum, endNum);
}









bool gfcSequence::freeFrames ( int numFrames ) {
	printf ( "\t\t\tFreeing %i Frames!\n",numFrames );
	int freed=0;
	for ( int i=0;i<(int)frames.size() && freed<numFrames;i++ ) {
		if ( frameTable_.state(i)!=gfcFrameState::Resident )
			continue;
		evictFrame(i);
		++freed;
	}
	return freed==numFrames;
}

void gfcSequence::evictFrame ( int index ) {
	if ( index<0 || index>=(int)frames.size() )
		return;
	gfcFrame &frame=frames[index];
	// Through deleteTexture, so the name is forgotten (a later clearFrame
	// must not pool or free it again) and the IOSurface is released.
	frame.deleteTexture();
	frame.loaded=false;
	if ( frame.theImageLoader ) {
		frame.theImageLoader->releaseMemory();
		delete frame.theImageLoader;
		frame.theImageLoader=NULL;
	}
	// The thumbnail and savedParams stay: the filmstrip keeps the frame and
	// forceLoad can decode it again.
	frameTable_.setUnloaded(index);
}

//generate Texture can generate frames from rawFrames and store them in the frames stack, or if pRawFrame is not null, it can pass generate a frame and store it in pFrame, which is used to generate preview frames to be consistent with the loaded frames.
bool gfcSequence::generateTexture ( RawFrame *pRawFrame, gfcFrame *pFrame ) {
	int totalGenTime=0;
	gfcFrame tmpRawFrame;
	gfcFrame tmpFrame;
	bool notEmpty=false;
	static long startTime, endTime;
	int textureSizeX=0;
	int textureSizeY=0;

	void *theImageDataPtr;

	//try lock mutex
	{

		std::unique_lock<std::mutex> lock ( rawQueueMutex, std::try_to_lock );
		if ( lock.owns_lock() ) {


			//if lock is successful check for empty queue

			if ( !rawFrames.empty() ) {//if not empty pop element and store in tmp

				tmpRawFrame=rawFrames.front();
				rawFrames.pop();
				notEmpty=true;
			}


		}


	}//end scope for mutex lock

	//IF WE ARE GENERATING FROM A PARAMETER RAW FRAME, REPLACE THE TMPRAWFRAME WITH THE ONE PASSED
	/*if ( pRawFrame!=NULL )
	{
	tmpRawFrame=*pRawFrame;
	notEmpty=true;
	printf ( "Generating from pRawFrame\n" );
	}

	bool memoryProblem=false;
	if ( notEmpty )
	{//generate texture and push into frames
	if ( tmpRawFrame.loaded )
	{
	//CHECK IF WE ARE DOING SLICED LOADING OR IF FORCED PBOs ARE ON
	if ( sett.numOfPartitions>1 || sett.forcePBO )
	{
	printf ( "Using PBOs in texGen\n" );
	//TODO Fill the PBO with the slices data and set the bitmap ptr to the start of the PBO.

	if ( !pbo || pboX!=tmpRawFrame.totalW || pboY!=tmpRawFrame.totalH )
	{
	printf ( "Regenerating PBO\n" );
	//THE PBO HAS NOT BEEN CREATED OR HAS A DIFFERENT SIZE
	if ( pbo!=NULL )
	glDeleteBuffersARB ( 1,&pbo );

	// 			 startTime=glutGet(GLUT_ELAPSED_TIME);
	glGenBuffersARB ( 1,&pbo );

	glBindBufferARB ( GL_PIXEL_UNPACK_BUFFER_ARB, pbo );
	pboX=tmpRawFrame.totalW;
	pboY=tmpRawFrame.totalH;
	glBufferDataARB ( GL_PIXEL_UNPACK_BUFFER_ARB,tmpRawFrame.totalW*tmpRawFrame.totalH*tmpRawFrame.bitmap[0]->getComponentsPerPixel(),
	NULL, GL_STREAM_DRAW );
	//                        endTime=glutGet(GLUT_ELAPSED_TIME);
	//                 	printf("Buffer generation and alloc time: %i\n",endTime-startTime);
	printf ( "PBO=%u\n",pbo );
	}
	glBindBufferARB ( GL_PIXEL_UNPACK_BUFFER_ARB, 0 );
	glTexImage2D ( GL_TEXTURE_2D, 0, GL_RGBA8, tmpRawFrame.totalW, tmpRawFrame.totalH, 0,
	GL_BGRA, GL_UNSIGNED_BYTE, NULL );

	glBindBufferARB ( GL_PIXEL_UNPACK_BUFFER_ARB, pbo );

	printf ( "Mapping Buffer\n" );
	//                 startTime=glutGet(GLUT_ELAPSED_TIME);
	void* pboMemory = glMapBufferARB ( GL_PIXEL_UNPACK_BUFFER_ARB, GL_WRITE_ONLY );
	//                 endTime=glutGet(GLUT_ELAPSED_TIME);
	//                 totalGenTime+=endTime-startTime;
	//                 printf("glMapBufferARB time: %i\n",endTime-startTime);
	assert ( pboMemory );


	printf ( "Copying data to BUFFER\n" );
	//                 startTime=glutGet(GLUT_ELAPSED_TIME);
	memcpy ( pboMemory,tmpRawFrame.bitmap[0]->getDataPtr(), tmpRawFrame.totalW*tmpRawFrame.totalH*tmpRawFrame.bitmap[0]->getComponentsPerPixel() );
	//memset(pboMemory,1,pboX*pboY*4);
	//                 endTime=glutGet(GLUT_ELAPSED_TIME);
	//                 totalGenTime+=endTime-startTime;
	//                 printf("Memcpy time: %i\n",endTime-startTime);
	printf ( "Unmapping buffer\n" );
	if ( !glUnmapBufferARB ( GL_PIXEL_UNPACK_BUFFER_ARB ) )
	{
	printf ( "Could not UNMAP Pixel Buffer Object, returning\n" );
	return 0;
	}


	theImageDataPtr=BUFFER_OFFSET ( 0 );
	}
	else
	{

	theImageDataPtr=tmpRawFrame.bitmap[0]->getDataPtr();
	}


	{

	GLuint glError = glGetError();
	{
	std::lock_guard<std::mutex> lock ( gNoMoreRamMutex )
	;
	//do
	{

	GLuint tmpTextureID;
	glGenTextures ( 1,&tmpTextureID );
	tmpFrame.textureID= ( tmpTextureID );

	//bind texture
	//if(sett.renderingEngine==0)
	{
	//printf("\n\t\t**************BEFORE GENERATING TEXTURE*************!\n");

	//DECIDE IF WE USE COMPRESSION
	if ( tmpRawFrame.depth==GL_COMPRESSED_RGB_ARB || tmpRawFrame.depth==GL_COMPRESSED_RGBA_ARB )
	{

	glBindTexture ( GL_TEXTURE_2D, tmpFrame.textureID );

	//printf("..");
	glTexParameterf ( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP );
	glTexParameterf ( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP );
	glTexParameterf ( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameterf ( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexEnvf ( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );


	glError = glGetError();
	glTexImage2D ( GL_TEXTURE_2D,0,tmpRawFrame.depth,tmpRawFrame.totalW, tmpRawFrame.totalH, 0, tmpRawFrame.format, tmpRawFrame.bpc>8?GL_UNSIGNED_SHORT:GL_UNSIGNED_BYTE, theImageDataPtr );
	glError = glGetError();
	if ( glError!=GL_NO_ERROR )
	{
	printf ( "Error gen texture! %i\n", glError );
	}
	else
	{
	printf ( "No Error! %i\n", glError );
	}

	}
	else //IF NOT USING S3TC COMPRESSION
	{
	//                                 startTime=glutGet(GLUT_ELAPSED_TIME);
	glBindTexture ( sett.textureRectangles?GL_TEXTURE_RECTANGLE_ARB:GL_TEXTURE_2D, tmpFrame.textureID );

	//printf("..");
	glTexParameterf ( sett.textureRectangles?GL_TEXTURE_RECTANGLE_ARB:GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP );
	glTexParameterf ( sett.textureRectangles?GL_TEXTURE_RECTANGLE_ARB:GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP );
	glTexParameterf ( sett.textureRectangles?GL_TEXTURE_RECTANGLE_ARB:GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameterf ( sett.textureRectangles?GL_TEXTURE_RECTANGLE_ARB:GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexEnvf ( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
	//                                 startTime=glutGet(GLUT_ELAPSED_TIME);

	GLuint dataType=tmpRawFrame.bpc>8?GL_UNSIGNED_SHORT:GL_UNSIGNED_BYTE;

	//glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,tmpRawFrame.depth, tmpRawFrame.totalW, tmpRawFrame.totalH,0, tmpRawFrame.format, dataType, theImageDataPtr);
	char printingInternalFormat[25];
	switch ( tmpRawFrame.gl_InternalFormat )
	{
	case GL_RGBA:
	sprintf ( printingInternalFormat,"%s","GL_RGBA" );
	break;

	case GL_RGBA16F_ARB:
	sprintf ( printingInternalFormat,"%s","GL_RGBA16F_ARB" );
	break;

	case GL_RGBA4:
	sprintf ( printingInternalFormat,"%s","GL_RGBA4" );
	break;
	default:
	sprintf ( printingInternalFormat,"%s","Who Knows" );
	break;
	}

	printf ( "tmpRawFrame: gl_Format=%s gl_InternalFormat=%s gl_type=%s\n",tmpRawFrame.gl_Format==GL_RGB?"GL_RGB":"GL_RGBA",printingInternalFormat,tmpRawFrame.gl_type==GL_UNSIGNED_BYTE?"GL_UNSIGNED_BYTE":"GL_UNSIGNED_SHORT" );

	gfcTimer texTimer ( "glTexImage2D" ), subTexTimer ( "glTexSubImage2D" );

	texTimer.start();
	glTexImage2D ( GL_TEXTURE_RECTANGLE_ARB,0,tmpRawFrame.gl_InternalFormat, tmpRawFrame.totalW, tmpRawFrame.totalH,0, tmpRawFrame.gl_Format, tmpRawFrame.gl_type, theImageDataPtr );
	glFlush();
	glFinish();
	texTimer.stop();
	texTimer.print();

	// 								subTexTimer.start();
	// 								glTexSubImage2D ( GL_TEXTURE_RECTANGLE_ARB,0,0,0,tmpRawFrame.totalW, tmpRawFrame.totalH,tmpRawFrame.gl_Format, tmpRawFrame.gl_type, theImageDataPtr );
	// 								glFlush();
	// 								glFinish();
	// 								subTexTimer.stop();
	// 								subTexTimer.print();
	if ( sett.numOfPartitions>1 || sett.forcePBO )
	{
	glBindBuffer ( GL_PIXEL_UNPACK_BUFFER_ARB, 0 );
	}

	//
	}
	//
	//printf("\n\t\t**************AFTER GENERATING TEXTURE*************!\n");

	}

	memoryProblem=false;
	glError = glGetError();
	if ( glError!=GL_NO_ERROR )
	{
	printf ( "Error on glError!\n" );
	switch ( glError )
	{
	case GL_OUT_OF_MEMORY:
	//if(glError==GL_OUT_OF_MEMORY)
	{
	//gOutOfMemory=true;
	printf ( "Out of memory!\n" );
	memoryProblem=true;
	//return glError;
	//Out of Memory, unload the first loaded frame for each sequence and try again, probably good idea to do in a do-while above

	// 									 if(!mw.infinitePlaybackToggle->value())
	// 									     gOutOfMemory=true;
	// 									 else
	// 									     gOutOfMemory=false;


	//if(!gOutOfMemory)
#ifdef JEFECHECK_USE_FLTK
	if ( mw.infinitePlaybackToggle->value() )
	{
	int frameToFree=5;
	printf ( "\t\tOut of RAM, freeing memory space\n" );
	mw.vp->trackA.freeFrames ( frameToFree );
	mw.vp->trackB.freeFrames ( frameToFree );
	mw.vp->trackC.freeFrames ( frameToFree );
	mw.vp->trackD.freeFrames ( frameToFree ); //don't just free from my space, free all sequences to maintain coherence between tracks
	gNoMoreRamCondition.notify_one();
	//gOutOfMemory=false;
	}
	else //if we have no more RAM, and the infinite toggle is off, then Stop loading and stop generating textures.
	{
	//tell all the sequences to stop loading.
	mw.vp->trackA.loadingCanceled=true;
	mw.vp->trackB.loadingCanceled=true;
	mw.vp->trackC.loadingCanceled=true;
	mw.vp->trackD.loadingCanceled=true;
	break;
	}
#else
	break;
#endif

	}
	break;
	default:
	printf ( "Unhandled error\n" );
	break;
	}

	}
	else
	{
	//printf("No memory problem\n");
	}
	}
	//while(memoryProblem && !gOutOfMemory);
	}


	//printf("\n\t\t**************BEFORE COPY TO TMPFRAME*************!\n");
	//printf("Texture Gen OK! (ID: %i)\n",tmpFrame.textureID[j]);

	if ( !memoryProblem )
	{ //asked in case we are not in infinite mode and the last texture is not generated to avoid a white frame at the end
	strcpy ( tmpFrame.formatDescription,tmpRawFrame.formatDescription );
	strcpy ( tmpFrame.compressionDescription,tmpRawFrame.compressionDescription );
	strcpy ( tmpFrame.format,tmpRawFrame.formatName );
	tmpFrame.channels=tmpRawFrame.bitmap[0]->getComponentsPerPixel();
	tmpFrame.bpc=tmpRawFrame.bitmap[0]->getBitsPerComponent();
	tmpFrame.dpxSlice=tmpRawFrame.dpxSlice;
	tmpFrame.originalSizeX=tmpRawFrame.totalW;
	tmpFrame.originalSizeY=tmpRawFrame.totalH;
	//tmpFrame.sizeX=tmpRawFrame.w[j];
	//tmpFrame.sizeY=tmpRawFrame.h[j];
	tmpFrame.loaded=true;
	tmpFrame.fileName=tmpRawFrame.fileName;
	//sldr->size(0,15);

	//printf("\n\t\t**************AFTER COPY TO TMPFRAME*************!\n");
	}
	}
	}

	else
	{ //if the tmpRawframe was not loaded
	tmpFrame.loaded=false;
	tmpFrame.fileName="ERROR READING FILE or FILE NOT FOUND";
	}

	//IF pFrame IS NOT NULL, THEN SAVE TO THAT FRAME INSTEAD OF TO THE FRAMES VECTOR
	if ( pFrame!=NULL )
	{
	*pFrame=tmpFrame;
	}
	else
	{
	if ( tmpRawFrame.indexNumber<frames.size() && tmpRawFrame.indexNumber>=0 )
	{
	//printf("\nFrame inserted into %i\n",tmpRawFrame.indexNumber) ;
	frames[tmpRawFrame.indexNumber]=tmpFrame;
	loadedFrames.push ( tmpRawFrame.indexNumber );
	printf("DpxInfo in frame Stack[%i]:\n",tmpRawFrame.indexNumber);
	frames[tmpRawFrame.indexNumber].dpxSlice.printInfo();
	}
	else
	{
	//printf("\nOut of sequence bounds! Index: %i frames.size: %i \n",tmpRawFrame.indexNumber,frames.size());
	}
	rangeBegin=loadedFrames.front();
	//printf("rangeBegin: %i\n",loadedFrames.front());
	if ( loadedFrames.size() >0 )
	rangeEnd=loadedFrames.back();
	else
	rangeEnd=1;

	//IF WE ARE LOADING FROM A RAWFRAME, DELETE IT.
	tmpRawFrame.cleanUp(); //deletes all the bitmaps
	//printf("rangeEnd: %i\n",rangeEnd);

	//printf("\nTrack %c : totalFrames in frames: %i\n)",trackID, frames.size());

	// 			 char alertText[1024];
	// 			 sprintf(alertText,"Tex Gen Time was %ims (%fs)",endTime-startTime,(endTime-startTime)/1000.0);
	// 			 printf("%s\n",alertText);

	sldr->setRange ( rangeBegin,rangeEnd,mw.timeLine->w() / ( mw.playUpToInput->value()-mw.playFromInput->value() ) );
	sldr->damage();
	app().processEvents();
	}
	}
	*/

	return 0;
}

gfcSequence::gfcSequence() : isActive ( false )
, quadrant ( 0 ) {
	numFrames=frameOffset=0;
	strcpy ( label,"" );
	rangeBegin=rangeEnd=0;
	taoi= new Rectang;
	aoi.set(-1,-1,-1,-1);
	myThread=NULL;
	loadingCanceled=false;
	holdFrameMode=0;
	holdFrameCurrent=0;
	forceGFLLoading=false;
	continueLoadingOnError=true;
}

gfcSequence::~gfcSequence() {
	stopLoading();   // never tear a track down under its own threads (clearSequence stops them too)
	clearSequence();
}


int gfcSequence::generateTextures ( int howMany ) {
	//printf("Inside gfcSequence::generateTextures\n");
	//1. Lock rawFrames mutex
	gfcFrame tmpRawFrame;
	bool notEmpty=false;


	{//START scope for try lock (it's a try lock, if it can't get the lock, it moves on, we don't want to hold up the main thread)
		std::unique_lock<std::mutex> lock ( rawQueueMutex, std::try_to_lock )
			;

		if ( lock.owns_lock() ) {//if lock is succesfull check for empty queue


			//2. Check if there are raw frames in there, if so, pop one.
			if ( !rawFrames.empty() ) {//if not empty pop element and store in tmp
				//printf ( "rawFrames not empty (%i), poping one\n",rawFrames.size() );
				tmpRawFrame=rawFrames.front();
				rawFrames.pop();
				notEmpty=true;

				//we should also notify the loading thread that the queue size has changed, maybe it can load a new frame now. 
				cond.notify_all();

			} else {
				return 1;
			}

		}
		//END of the try lock scope, when it comes out of scope it is automatically unlocked.

	}

	//4, generateTexture from the extracted frame.
	if ( notEmpty) {
		const int index=tmpRawFrame.indexNumber;
		if ( index<0 || index>=(int)frames.size() ) {
			printf ( "\nOut of sequence bounds! Index: %i frames.size: %zu\n",index,frames.size() );
			discardRawFrame(tmpRawFrame);
			return 0;
		}
		// The table was reset (a new load) while it waited in the queue: it is
		// no longer wanted.
		if ( tmpRawFrame.loaded && frameTable_.state(index)!=gfcFrameState::Decoding ) {
			discardRawFrame(tmpRawFrame);
			return 0;
		}
		//    	printf("gfcSequence::generateTextures: rawQueue not empty\n");
		if (tmpRawFrame.loaded ) {
			//            printf ( "gfcSequence::generateTextures:  generating texture...\n" );
			// Capture a filmstrip thumbnail when enabled, capped per
			// sequence: store at most kMaxThumbs frames (every Nth beyond
			// that) so very long sequences stay bounded in memory.
			constexpr int kMaxThumbs = 2000;
			const int totalFrames = (int)files.size();
			const int thumbStride = totalFrames > kMaxThumbs
			                        ? (totalFrames + kMaxThumbs - 1) / kMaxThumbs : 1;
			const bool capture = sett.showThumbnails
			                     && (tmpRawFrame.indexNumber % thumbStride == 0);
			GLuint tmpTexID=tmpRawFrame.generateTexture(capture);
			//printf ( "texID=%i\n",tmpTexID );
			//check for gl errors.

			//5. Put into the regular frames vector.
			storeFrame(index, tmpRawFrame);
			// The decode's commit: Resident with the texture's real bytes.
			const int track=cacheTrack();
			// Lock-free (table leaf lock + notify): this runs in the playback tick.
			if ( track>=0 )
				frameCache.commitUploaded(frameTable_, index, tmpRawFrame.textureBytes);
			else
				frameTable_.setResidentIfDecoding(index, tmpRawFrame.textureBytes);
			// The first real upload corrects the coordinator's per-frame estimate.
			if ( !cacheFrameBytesChecked_ && tmpRawFrame.textureBytes>0 ) {
				cacheFrameBytesChecked_=true;
				if ( track>=0 && tmpRawFrame.textureBytes!=cacheFrameBytes_ ) {
					cacheFrameBytes_=tmpRawFrame.textureBytes;
					frameCache.setTrackFrameBytes(track, cacheFrameBytes_);
				}
			}
			myGUI->setLoadedRange(residentCount());
		} else {
			// A decode that failed: keep its name and error for the info line.
			// The coordinator already marked it (Failed, or Unloaded when stale).
			discardRawFrame(tmpRawFrame);
			storeFrame(index, tmpRawFrame);
			myGUI->setLoadedRange(residentCount());
		}
	}





	return 0;
}

void gfcSequence::prepareSequenceFiles()
{
	//this method gets the necesary files in the files vector, if the previewFiles are not empty, it uses those, otherwise it finds them again.
	if(previewFiles.size()>0){
		files=previewFiles;
	}
	else
	{
		findSequenceFiles(files,false);
	}
	// files[0] is the resolved path (search paths included); open the movie
	// once here so every frame of this load shares the reader. Its sound stays
	// off: the audio thread decodes it with a reader of its own.
	if (!files.empty() && gfcIsMovieFile(myGUI->getFilename()))
		openMovieReaderFor(files[0]);
	else
		openMovieReaderFor("");
}

void gfcSequence::openMovieReaderFor(const std::string& path)
{
	std::shared_ptr<gfcMovieReader> reader;
	if (!path.empty()) {
		reader = std::make_shared<gfcMovieReader>();   // sound off
		std::string err;
		if (!reader->open(path, &err)) {
			printf("Track %c: cannot open movie %s: %s\n", trackID, path.c_str(), err.c_str());
			reader.reset();
		}
	}
	std::lock_guard<std::mutex> lock(movieReaderMutex_);
	movieReader_ = std::move(reader);
	forcedReader_.reset();
	forcedReaderPath_.clear();
}

std::shared_ptr<gfcMovieReader> gfcSequence::forcedMovieReader(const std::string& path)
{
	{
		std::lock_guard<std::mutex> lock(movieReaderMutex_);
		if (!movieReader_ || path.empty()) return nullptr;
		if (forcedReader_ && forcedReaderPath_ == path) return forcedReader_;
	}
	// Opened outside the lock: the loader thread may be asking for its reader.
	auto reader = std::make_shared<gfcMovieReader>();   // sound stays off
	std::string err;
	if (!reader->open(path, &err)) {
		printf("Track %c: cannot open movie %s for a forced load: %s\n", trackID, path.c_str(), err.c_str());
		return nullptr;
	}
	std::lock_guard<std::mutex> lock(movieReaderMutex_);
	if (!movieReader_) return nullptr;   // cleared meanwhile
	forcedReader_ = reader;
	forcedReaderPath_ = path;
	return reader;
}

std::shared_ptr<gfcMovieReader> gfcSequence::currentMovieReader() const
{
	std::lock_guard<std::mutex> lock(movieReaderMutex_);
	return movieReader_;
}

bool gfcSequence::isMovie() const
{
	return currentMovieReader() != nullptr;
}

const gfcMovieInfo* gfcSequence::movieInfo() const
{
	std::shared_ptr<gfcMovieReader> reader = currentMovieReader();
	return reader ? &reader->info() : nullptr;
}

bool gfcSequence::hasAudio() const
{
	return hasAudio_.load(std::memory_order_relaxed);
}

std::shared_ptr<gfcAudioClip> gfcSequence::audioClip() const
{
	std::lock_guard<std::mutex> lock(audioMutex_);
	return audioClip_;
}

gfcAudioPeaks gfcSequence::audioPeaksSnapshot() const
{
	std::lock_guard<std::mutex> lock(audioMutex_);
	return audioPeaks_;
}

void gfcSequence::setAudioMuted(bool muted)
{
	audioMuted_=muted;
	if (gfcAudioEngineAlive()) audioEngine.setTrackMuted(trackID-'A', muted);
}

uint64_t gfcSequence::audioPeaksVersion() const
{
	std::lock_guard<std::mutex> lock(audioMutex_);
	return audioPeaks_.version;
}

// A fresh clip per load, so a mixer still holding the previous one never sees
// it change under it. The peaks version only grows.
void gfcSequence::resetAudio(int64_t startSample, bool withClip)
{
	std::lock_guard<std::mutex> lock(audioMutex_);
	if (withClip) {
		audioClip_ = std::make_shared<gfcAudioClip>();
		audioClip_->reset(startSample);
		hasAudio_.store(true, std::memory_order_relaxed);
	} else {
		audioClip_.reset();
		hasAudio_.store(false, std::memory_order_relaxed);
	}
	audioPeaks_.reset(startSample);
	++audioPeaks_.version;
}

// Shows the waveform's last partial second (the version otherwise moves at
// most once per second of audio) once a fill pass ends.
void gfcSequence::publishAudioPeaks(const std::shared_ptr<gfcAudioClip>& clip)
{
	std::lock_guard<std::mutex> lock(audioMutex_);
	if (clip && audioClip_ == clip) audioPeaks_.publish();
}

void gfcSequence::setAudioTarget(int timelineFrame)
{
	if ( !hasAudio_.load(std::memory_order_relaxed) || audioRateNum_<=0 || audioRateDen_<=0 )
		return;
	const int64_t source=int64_t(timelineFrame)-1-frameOffset;
	{
		// Stored under the wake mutex so an idle thread between its check and
		// its wait cannot miss it. Uncontended; jumps only, never the tick.
		std::lock_guard<std::mutex> lock(audioWakeMutex_);
		audioTarget_.store(gfcAudioClip::frameStartSample(source, audioRateNum_, audioRateDen_));
	}
	audioWake_.notify_all();
}

void gfcSequence::startAudioThread()
{
	stopAudioThread();
	if ( shuttingDown() )
		return;
	const std::shared_ptr<gfcAudioClip> clip=audioClip();
	if ( !audioReader_ || !clip || audioEnd_<=audioBegin_ )
		return;
	audioCanceled_.store(false);
	audioThread_=new std::thread(&gfcSequence::audioThreadMain, this, audioReader_, clip, audioBegin_, audioEnd_,
	                             audioRateNum_, audioRateDen_);
}

// No lock is held here (nor taken by the thread for longer than one block's
// copy), so the join cannot deadlock; a block is one second of audio.
void gfcSequence::stopAudioThread()
{
	{
		std::lock_guard<std::mutex> lock(audioWakeMutex_);
		audioCanceled_.store(true);
	}
	audioWake_.notify_all();
	if ( audioThread_ ) {
		audioThread_->join();
		delete audioThread_;
		audioThread_=nullptr;
	}
}

// Fills the clip's [begin, end) with the movie's sound in one-second blocks:
// forward from the start, or from a jump target that is not there yet (sought
// to when it is behind the read position or more than 2 s ahead of it), then
// once more from the start for any hole left behind. A hole the source cannot
// fill (no sound there) is stepped over, so a pass always ends; the thread
// then waits for the next jump or its cancel. Each block is decoded with no
// lock held; audioMutex_ is taken only to write it.
void gfcSequence::audioThreadMain(std::shared_ptr<gfcMovieReader> reader, std::shared_ptr<gfcAudioClip> clip,
                                  int64_t begin, int64_t end, int rateNum, int rateDen)
{
#ifdef __APPLE__
	pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);   // below the loaders and the GL thread
#endif
	constexpr int64_t kBlock=gfcAudioClip::kRate;      // 1 s
	constexpr int64_t kNear=2*gfcAudioClip::kRate;     // read through up to 2 s rather than seek
	// The movie frame holding `sample` (floor).
	auto frameOf=[&](int64_t sample) {
		const int64_t den=int64_t(gfcAudioClip::kRate)*rateDen;
		const int64_t num=sample*rateNum;
		return num>=0 ? num/den : -((-num+den-1)/den);
	};
	// The first unwritten sample in [from, end), or -1.
	auto holeFrom=[&](int64_t from) -> int64_t {
		if ( from>=end )
			return -1;
		const int64_t h=clip->firstHoleFrom(from);
		if ( h>=0 )
			return h<end ? h : -1;
		const int64_t e=std::max(from, clip->endSample());
		return e<end ? e : -1;
	};
	std::vector<float> block;
	std::string err;
	int64_t cursor=0;        // where the reader's next samples land (it opens at the file's start)
	int64_t keepFrom=begin;  // samples before this (a seek's pre-roll) are not written
	int64_t pos=begin;       // this pass looks for holes from here
	bool wrapped=false;      // this pass went back to begin already
	bool idle=false;
	int64_t seenTarget=-1;
	while ( !audioCanceled_.load() ) {
		const int64_t target=audioTarget_.load();
		if ( target!=seenTarget ) {
			seenTarget=target;
			if ( target>=begin && target<end && !clip->covers(target, std::min(kNear, end-target)) ) {
				pos=target;
				wrapped=false;
				idle=false;
			}
		}
		if ( idle ) {
			// Woken by a jump (setAudioTarget) or the cancel; the timeout is a safety net.
			std::unique_lock<std::mutex> lock(audioWakeMutex_);
			audioWake_.wait_for(lock, std::chrono::milliseconds(500), [&] {
				return audioCanceled_.load() || audioTarget_.load()!=seenTarget;
			});
			continue;
		}
		const int64_t hole=holeFrom(pos);
		if ( hole<0 ) {
			if ( !wrapped && pos>begin ) {
				pos=begin;
				wrapped=true;
				continue;
			}
			publishAudioPeaks(clip);
			idle=true;
			continue;
		}
		if ( hole<cursor || hole>cursor+kNear ) {
			const int64_t f=std::max<int64_t>(0, frameOf(hole));
			if ( !reader->seekAudio(f, &err) ) {
				printf("Track %c: audio seek failed: %s\n", trackID, err.c_str());
				publishAudioPeaks(clip);
				idle=true;
				continue;
			}
			cursor=gfcAudioClip::frameStartSample(f, rateNum, rateDen);
			keepFrom=std::max(begin, cursor);
		}
		if ( const int delay=g_testAudioBlockDelayMs.load() )
			std::this_thread::sleep_for(std::chrono::milliseconds(delay));
		int64_t first=0;
		const bool read=reader->readAudioUntil(std::min(end, std::max(cursor, hole)+kBlock), &err);
		if ( !read )
			printf("Track %c: audio read failed: %s\n", trackID, err.c_str());
		const bool got=read && reader->takeAudio(block, first);
		if ( !got ) {
			// The end of the file (or a read error): nothing more past here.
			cursor=end;
			pos=end;
		} else {
			const int64_t n=int64_t(block.size()/gfcAudioClip::kChannels);
			{
				std::lock_guard<std::mutex> lock(audioMutex_);
				gfcAppendAudioAt(*clip, first, block.data(), n, end, audioClip_==clip ? &audioPeaks_ : nullptr, keepFrom);
			}
			cursor=first+n;
			// Read past the hole and it is still there: the source has no sound
			// there, so step over it rather than seeking back to it forever.
			pos=(cursor>hole && !clip->covers(hole, 1)) ? cursor : hole;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
}

void gfcSequence::loadFrameRange(int& startFrame, int& endFrame) const
{
	if (params.loadFromTimeline==0)
		startFrame=params.fromFrame-firstFrame;
	else
		startFrame=params.loadFromTimeline;
	if ( startFrame<0 )
		startFrame=0;

	const int framesSize=frames.size();
	endFrame=params.toFrame-firstFrame;
	if ( endFrame>framesSize )
		endFrame=framesSize;
	if (endFrame<0)
		endFrame=0;

	if (startFrame>endFrame)
		startFrame=endFrame;

	if (endFrame>=(int)files.size())
		endFrame=files.size()-1;
}

int gfcSequence::initializeSequence ( gfcLoadParams pparams ) {
	stopAudioThread();   // the old load's sound thread, if any (startLoading already stopped it)
	params = pparams;
	//printf ( "\nInitializing Sequence:\n" );

	loadingFromTimeline=pparams.loadFromTimeline;

	if ( params.append==GFC_SEQERASE || params.append==GFC_SEQREPLACE )
		clearSequence();

	//printf ( "Finding Sequence for %s\n",params.fileName.c_str() );

	//files=findSequence ( &params.fileName[0],label,&firstFrame ); //find the sequence size here and resize the frames vector so that it is ready to process frames from the raw queue.


	prepareSequenceFiles();


	//printf ( " Resizing frame vector to %i frames\n",files.size() );
	frames.resize ( files.size() );

	// Every frame knows how to load itself, so a forced load (render, export)
	// works anywhere in the clip, resident or not.
	{
		const std::shared_ptr<gfcMovieReader> reader=currentMovieReader();
		for ( int i=0;i<(int)files.size();i++ ) {
			frames[i].fileName=files[i];
			frames[i].savedParams=frameLoadParams(i, reader);
		}
	}
	// Victims named before this reset belong to the old load (APPEND/STREAM
	// reach here without clearSequence): drop them with the track's geometry,
	// which setTrackGeometry restores below.
	if ( cacheTrack()>=0 )
		frameCache.clearTrack(cacheTrack());
	frameTable_.reset(frames.size());
	lastDrawnIndex_=-1;   // the old load's frame is not this one's

	int startFrame=0, endFrame=0;
	loadFrameRange(startFrame, endFrame);
	// The trim range: what the track plays, whatever is resident.
	const bool hasFootage=!frames.empty() && endFrame>=startFrame;
	if ( !hasFootage ) {
		rangeBegin=rangeEnd=0;
	} else {
		rangeBegin=startFrame;
		rangeEnd=endFrame;
	}
	cacheFrameBytes_=estimatedFrameBytes();
	cacheFrameBytesChecked_=false;
	const int track=cacheTrack();
	if ( track>=0 ) {
		frameCache.attachTrack(track, &frameTable_);
		frameCache.setTrackGeometry(track, frameOffset, hasFootage ? startFrame : -1, hasFootage ? endFrame : -1, cacheFrameBytes_);
	}
	// The reset cleared the pins: put back the hold mode's.
	heldPin_=-1;
	if ( holdFrameMode==2 )
		pinHoldEdges(true);
	updateHeldFramePin();

	// The clip covers the trim range (frame entry i is movie frame i), in the
	// movie's own 48 kHz timeline. Whether there is one is decided here, by
	// opening the audio-only reader: sound that cannot be decoded means no clip.
	{
		const std::shared_ptr<gfcMovieReader> reader=currentMovieReader();
		std::shared_ptr<gfcMovieReader> audio;
		if ( hasFootage && reader && reader->info().hasAudio && reader->info().rateNum>0 && reader->info().rateDen>0 ) {
			audio=std::make_shared<gfcMovieReader>();
			audio->setAudioOnly(true);
			std::string err;
			if ( !audio->open(files[0], &err) || !audio->info().hasAudio ) {
				if ( !err.empty() )
					printf("Track %c: cannot open the movie's sound: %s\n", trackID, err.c_str());
				audio.reset();
			}
		}
		audioReader_=audio;
		const bool withClip=audio!=nullptr;
		audioRateNum_=withClip ? reader->info().rateNum : 0;
		audioRateDen_=withClip ? reader->info().rateDen : 1;
		audioBegin_=withClip ? gfcAudioClip::frameStartSample(startFrame, audioRateNum_, audioRateDen_) : 0;
		audioEnd_=withClip ? gfcAudioClip::frameStartSample(int64_t(endFrame)+1, audioRateNum_, audioRateDen_) : 0;
		audioTarget_.store(-1);
		resetAudio(audioBegin_, withClip);
	}

	loadingCanceled=false;
	return 0;
}

gfcLoadParams gfcSequence::frameLoadParams(int i, const std::shared_ptr<gfcMovieReader>& reader) const
{
	gfcLoadParams p=params;
	p.forceGFLLoading=forceGFLLoading;
	if ( i>=0 && i<(int)files.size() )
		p.fileName=files[i];
	if (reader) {
		p.movieFrameIndex=i;
		p.movieReader=reader;
	}
	return p;
}

int gfcSequence::cacheTrack() const
{
	const int t=trackIndex();
	if ( t<0 || t>=gfcFrameCache::kMaxTracks )
		return -1;
	return trackManager.getSequence(t)==this ? t : -1;
}

// Puts a frame from the queue into frames[i]. A texture still there is freed
// first (never leaked), and a frame that brings no thumbnail keeps the old one.
void gfcSequence::storeFrame(int i, gfcFrame& f)
{
	gfcFrame& slot=frames[i];
	if ( slot.textureID!=defaultTexture )
		slot.deleteTexture();
	if ( f.thumbnail.w==0 && slot.thumbnail.w>0 )
		f.thumbnail=std::move(slot.thumbnail);
	slot=f;
}

int gfcSequence::loadSequence() {

	int endFrame;
	int framesSize;

	if ( params.stream ) {
		printf ( "Streaming\n" );
	} else {


		//std::vector<std::string> localFiles=files;
		//printf ( "Regular loading\n" );
		//printf ( "First Frame=%i\n",firstFrame );
		int startFrame;
		if (params.loadFromTimeline==0) {
			networkManager.sendSystemChatMessage(filenameGeneric,GFCNETMESSAGETYPE_LOAD);
			g_loadAnnouncements.fetch_add(1);
		}
		loadFrameRange(startFrame, endFrame);
		framesSize=frames.size();

		myGUI->activateAbortButton();
		myGUI->setTrackRange(1,framesSize);
		myGUI->setTotalFramesToLoad(startFrame+1,endFrame-startFrame+1); //these functions consider the first frame to be 1, not 0 as our vectors, so we add 1
		myGUI->setLoadedRange(0);
		myGUI->setTrackLabel(GetFilenameNoPath(filenameGeneric));
		const std::shared_ptr<gfcMovieReader> movieReader=currentMovieReader();
		const int track=cacheTrack();
		printf("***Track %c: loading %i..%i as the cache asks\n",trackID,startFrame,endFrame);
		// The cache coordinator decides what to decode next (playhead first, then
		// ahead, then just behind) and when to stop (the budget). This thread runs
		// until the load is cancelled; between jobs it waits in nextJob.
		while ( !loadingCanceled && track>=0 ) {
			gfcFrameCache::Job job;
			const auto asked=std::chrono::steady_clock::now();
			if ( !frameCache.nextJob(track, job, std::chrono::milliseconds(100)) ) {
				// nextJob returns at once with no footage or a cancel: don't spin.
				if ( std::chrono::steady_clock::now()-asked<std::chrono::milliseconds(20) )
					std::this_thread::sleep_for(std::chrono::milliseconds(20));
				continue;
			}
			if ( loadingCanceled ) {
				frameCache.commit(job, 0, false);
				break;
			}
			const int i=job.sourceIndex;
			gfcFrame tmpFrame;
			if ( const int delay=g_testDecodeDelayMs.load() )
				std::this_thread::sleep_for(std::chrono::milliseconds(delay));
			const int loadResult=tmpFrame.loadFrame ( frameLoadParams(i, movieReader) );
			if (loadResult==GFCFRAME_LOADERROR_NO_MORE_MEMORY)
			{
				printf("No more memory while loading frame, stopping sequence load\n");
				loadingCanceled=true;
			}
			tmpFrame.indexNumber=i;
			// A decoded frame stays Decoding (its bytes reserved) while it waits
			// in the queue; the GL thread commits it when it is uploaded, so
			// Resident always means on the GPU and a queued frame is never a victim.
			if ( !tmpFrame.loaded ) {
				tmpFrame.skipped=true;
				if ( !continueLoadingOnError ) {
					// Loading stops here, and this frame stays Failed (not
					// retried), whatever the coordinator would call it.
					loadingCanceled=true;
					frameTable_.setFailed(i);
				}
				frameCache.commit(job, 0, false);
			}

			{//lock the mutex here to push the frame into the rawFrame stack, which is shared by the main thread, which is the one that generates the textures.
				std::unique_lock<std::mutex> lock ( rawQueueMutex );
				while(rawFrames.size()>sett.maximumFramesInQueue && !loadingCanceled)
					cond.wait(lock);
				rawFrames.push ( tmpFrame );
			}
			gResizeTrigger=true;
		}

	}

	gResizeTrigger=true;
	myGUI->deactivateAbortButton();
	return 0;
}

// int gfcSequence::loadSequence ( std::string fileName, int scale, Rectang* aoi,int filterType, bool append, float gamma, int fromFrame, int toFrame, int loadingFromTimeline, float exposition, float defog, float kneeH, float kneeL, int channel ) {
// 
//     //return 0;
//     char fn[128];
//     gfcFrame tmpFrame;
// 
//     int newSize;
//     int sliderFrameSize;
//     int firstFrame=0;
//     char label[1024]="";
//     //get all filenames in the sequence
//     files=findSequence ( &fileName[0],label,&firstFrame );
//     //fromFrame-=frameOffset;
//     if ( fromFrame<0 )
//         fromFrame=0;
//     printf ( "\n********TRACK %c: Loading Sequence\n\a\a",trackID );
//     //printf ( "Sequence::loadSequence AOI: %i %i %i %i\n",aoi->x,aoi->y, aoi->w, aoi->h );
//     if ( !append ) {  //clearSequence(); //this has already been done in the main thread (in the initSequence method) since it involves clearing gl tex id's
//         sprintf ( label,&fileName[0] );
//         numFrames=toFrame-fromFrame+1;
//     } else {
//         numFrames+=toFrame-fromFrame+1;
//     }
// 
//     //load frames!
// 
//     int n=files.size();
//     int totalSize=n+numFrames;
// 
//     char pbLabel[30]="0%";
// 
// 
// 
//     if ( mainWindowExists ) {
//         mw.timeLine->maximum ( mw.vp->getMaxTrackLenght() );
//         mw.timeLine->damage();
//     }
//     //resizeTimeLine();
//     // double howMuchMemory=getFreeMem();
//     //fromFrame-firstFrame is the index begining.
//     static long startTime, endTime;
//     //startTime=glutGet(GLUT_ELAPSED_TIME);
// 
//     gfcTimer loadSequenceTimer ( "Load Sequence" );
// 
//     //*************this is where streaming and non streaming part************
//     if ( streaming ) {
//         loadSequenceTimer.start();
//         //printf ( "files.size: %i\n",files.size() );
//         mw.playUpToInput->value ( mw.vp->getMaxTrackLenght() );
//         gResizeTrigger=true;
//         mw.timeLine->value ( mw.timeLine->minimum() );
//         printf ( "filename in frame 14 is %s\n",files[14].c_str() );
// 
//         /**create all the support crap needed for the loading and upload of textures:
//          	-PBO buffer based on the fileSize (ping the first file to get the info);
//          	-
//          
//          */
// 
//         //start a thread that will run until the program exits or this same track is started without streaming
//         /*********streaming texture loads******************/
// 
// 
//         //start a thread loadingCanceled=false;to do stream loading for this track.
//         //printf("stream thread=%x\n",trackAStreamingThread);
//         if ( true ) {
//             printf ( "Thread == NULL\n" );
//             StreamingThread=new std::thread ( StreamingThreadStarterFunc );
//         }
//         /*if(trackAStreamingThread!=NULL)
//         {
//         	trackAStreamingThread->join();
//         }*/
// 
//     } else {
//         int indexStart=loadingFromTimeline?fromFrame:fromFrame-firstFrame;
// 
//         //CALCULATE GAMMA TABLE, this table will be passed as a pointer in the load params.
//         float gammaLUT[65536];
//         if ( gamma!=1 ) {
// 
//             //Get the bitdepth from first frame;
//             GFLC_FILE_INFORMATION info ( files[indexStart].c_str() );
//             printf ( "Got File Info In sequence loading: %ix%ix%i\n",info.getHeight(),info.getWidth(),info.getBitsPerComponent() );
//             //calculate the size of the lut based on bitdepth
//             int numberOfEntries=1<<info.getBitsPerComponent();
//             printf ( "Number of Entries for gammaLUT: %i\n",numberOfEntries );
// 
//             //create table, remember to delete de memory allocation at function's end.
//             //gammaLUT=new float[numberOfEntries];
//             //fill the table
//             float gammaExp=1.0/gamma;
//             for ( int i=0;i<numberOfEntries;i++ ) {
//                 gammaLUT[i]=pow ( i/ ( float ) numberOfEntries,gammaExp );
//                 printf ( "LUT: %i=%f\n",i,gammaLUT[i] );
//             }
// 
//         }
// 
// 
//         loadSequenceTimer.start();
//         for ( int i=indexStart; i<files.size();i++ ) {
//             if ( loadingCanceled ) {
//                 abortButton->deactivate();
//                 break;
//             }
//             if ( /*!mw.infinitePlaybackToggle->value() &&*/ maxFramesReached() ) {
//                 abortButton->deactivate();
//                 break;
//             }
// 
// 
//             //printf("\ni:%i, from Frame:%i, toFrame:%i\n",i,fromFrame, toFrame);
//             /*gfcFrame tmpRawFrame;
//             tmpRawFrame.load();
//             gfcRawFrameLoadParams loadParams;
// 
//             loadParams.trackID=trackID;
//             loadParams.name=files[i];
//             loadParams.scale=scale;
//             loadParams.cropX=aoi->x;
//             loadParams.cropY=aoi->y;
//             loadParams.cropW=aoi->w;
//             loadParams.cropH=aoi->h;
//             loadParams.filterType=filterType;
//             loadParams.gamma=gamma;
//             loadParams.lutList=luts;
//             loadParams.compressed=compressed;
//             loadParams.exposition=exposition;
//             loadParams.defog=defog;
//             loadParams.kneeH=kneeH;
//             loadParams.kneeL=kneeL;
//             loadParams.channel=channel;
//             loadParams.gammaLUT=gammaLUT;
// 
// 
// 
// 
// 
//             if ( !loadingFromTimeline )
//             {
// 
//             	if ( files.size() >i )
//             	{
//             		tmpRawFrame.loadFrame ( loadParams );
//             		tmpRawFrame.frameNumber=i+firstFrame;
//             		tmpRawFrame.indexNumber=i;
//             	}
//             	else
//             	{
//             		tmpRawFrame.loaded=false;
//             		tmpRawFrame.frameNumber=i+firstFrame;
//             		tmpRawFrame.indexNumber=i;
//             		break;
//             	}
//             }
//             else
//             {
//             	if ( i>=0 && i<files.size() )
//             		tmpRawFrame.loadFrame ( loadParams );
//             	//tmpRawFrame=tmpFrame.loadFrame(trackID, files[i],scale,aoi->x, aoi->y, aoi->w, aoi->h,filterType,gamma, luts, compressed, exposition, defog, kneeH, kneeL,channel);
//             	else
//             		tmpRawFrame.loaded=false;
// 
// 
//             	tmpRawFrame.frameNumber=i+firstFrame;
//             	tmpRawFrame.indexNumber=i;
// 
//             	if ( !tmpRawFrame.loaded ) //if a frame was not loaded succesfully, then stop loading.
//             	{
//             		printf("*******STOP LOADING!!!***********\n");
//             		break;
//             	}
//             }
//             {//lock the mutex here to push the frame into the rawFrame stack, which is shared by the main thread, which is the one that generates the textures.
//             	std::lock_guard<std::mutex> lock ( rawQueueMutex )
//             		;
//             	rawFrames.push ( tmpRawFrame );
//             	mw.playUpToInput->value ( mw.vp->getMaxTrackLenght() );
//             	//frames.push_back(tmpFrame);
//             }
//             */
//             gResizeTrigger=true;
// 
// 
//         }
//         //endTime=glutGet(GLUT_ELAPSED_TIME);
//     }
//     loadSequenceTimer.stop();
//     printf ( "****************************************************\n\a\a" );
//     loadSequenceTimer.print();
// 
//     //char alertText[1024];
//     //sprintf(alertText,"Load Time was %ims (%fs) (%f per frame for %i frames)",endTime-startTime,(endTime-startTime)/1000.0,float(endTime-startTime)/(toFrame-fromFrame),(toFrame-fromFrame));
//     //printf("%s\n",alertText);
//     //fl_alert(alertText);
//     //printf("Done Loading from HD\nmw.infinitePlaybackToggle:%i\ngOutOfMemory:%i\n",mw.infinitePlaybackToggle->value(),gOutOfMemory);
//     //printf("Size of Raw Frames:%i\n",rawFrames.size());
//     /* if(gammaLUT!=NULL)
//      	delete gammaLUT;*/
// 
//     loadingCanceled=false;
//     abortButton->deactivate();
//     gResizeTrigger=true;
//     return 0;
// }

void gfcSequence::fillFiles ( const char* fileName ) {
	char *label;
	int firstFrame;
	findSequenceFiles(files,false);
	//files=findSequence( fileName,label,&firstFrame );
}

void gfcSequence::unloadAndClear() {
	clearSequence();
	myGUI->clearAllValues();
	previewFrame.clearFrame();
	//Fl::lock();
	//Fl::redraw();
	//Fl::unlock();
}

int gfcSequence::clearSequence() {
	//printf ( "Clearing Sequence %c...\n",trackID );

	stopLoading();

	for (int i=frames.size()-1;i>=0;i--)
		frames[i].clearFrame();


	frames.clear();
	frames.reserve(1);
	files.clear();
	files.reserve(1);
	/*previewFiles.clear();
	previewFiles.reserve(1);*/


	this->clearRawQueue();
	if ( cacheTrack()>=0 )
		frameCache.clearTrack(cacheTrack());   // also drops victims named on this track
	frameTable_.reset(0);
	lastDrawnIndex_=-1;
	heldPin_=-1;   // the reset cleared it

	if (myGUI) {
		myGUI->setLoadedRange(0);
		myGUI->setTotalFramesToLoad(0,0);
		myGUI->setTrackRange(0,0);
	}
	openMovieReaderFor("");
	audioReader_.reset();   // stopLoading joined its thread
	resetAudio(0, false);
	if (gfcAudioEngineAlive()) audioEngine.clearTrack(trackID-'A');   // skipped during static teardown

	rangeBegin=rangeEnd=0;
	//
	//printf ( "done\n" );
	return 0; //return ok
}

gfcFrame gfcSequence::forceLoad ( int frame ) {
	//the frame parameter should already consider offset. The function does not check for range.

	// A forced frame lives until the next forceLoad or cleanForcedLoad. The
	// same frame asked for twice in one draw (a plate and an FX input reading
	// the same track) is served from the one already loaded.
	if (forcedFrame.loaded && forcedFrameIndex == frame)
		return forcedFrame;
	cleanForcedLoad();

	forcedFrame=frames[frame];
	forcedFrameIndex=frame;
	printf("Forced loading: %s\n",forcedFrame.savedParams.fileName.c_str());
	// Movies: never through the loader's reader (it decodes the track's sound).
	if (forcedFrame.savedParams.movieReader) {
		if (std::shared_ptr<gfcMovieReader> r=forcedMovieReader(forcedFrame.savedParams.fileName))
			forcedFrame.savedParams.movieReader=r;
		else
			forcedFrame.savedParams.movieReader.reset();   // the loader opens its own
	}
	forcedFrame.loadFrame();
	forcedFrame.generateTexture();

	return forcedFrame;

}

void gfcSequence::cleanForcedLoad() {
	if ( forcedFrame.loaded ) {
		// deleteTexture also drops the frame's IOSurface reference (or pools
		// the texture), which a bare glDeleteTextures leaked per forced load.
		forcedFrame.deleteTexture();
		forcedFrame.loaded=false;
	}
	forcedFrameIndex=-1;
}

Rectang gfcSequence::getFrameSizeAt ( int frameNo ) {

	static Rectang returnValue;
	return returnValue;

}

// What a plate shows for source frame i (GL thread, every draw: atomics only,
// no lock, no table walk). A loaded frame is shown and becomes the on-screen
// frame, which the cache never evicts. An unloaded one shows the last drawn
// frame while playing in Keep time, unless a jump came in between (the cache
// generation moved); otherwise nothing (black, and the plate labels it
// "Loading" while framePending()). A forced load (render) never substitutes
// and never moves the on-screen pin or the last drawn frame.
const gfcFrame& gfcSequence::displayFrame ( int i, bool pforceLoad, const gfcFrame& emptyFrame ) {
	if ( frames[i].loaded ) {
		if ( !pforceLoad ) {   // a render is not what the plate shows
			lastDrawnIndex_=i;
			lastDrawnGeneration_=frameCache.generation();
			frameTable_.setOnScreen(i);
		}
		return frames[i];
	}
	if ( !pforceLoad && playbackManager.isPlaying() && playbackManager.keepTime()
	     && lastDrawnIndex_>=0 && lastDrawnIndex_<(int)frames.size()
	     && frames[lastDrawnIndex_].loaded
	     && lastDrawnGeneration_==frameCache.generation() )
		return frames[lastDrawnIndex_];
	return emptyFrame;
}

gfcFrame gfcSequence::getFrame ( int frameNo,bool pforceLoad ) {
	gfcFrame emptyFrame;
	/*emptyFrame.loaded=false;
	emptyFrame.fileName="Frame not loaded";*/

	int realFrame=frameNo-frameOffset-1;

	//if there is nothing loaded, and we are not forcing the load, then return the empty frame
	if (residentCount()==0 && !pforceLoad)
		return emptyFrame;

	/*if(loadedFrames.empty())
	return emptyFrame;*/

	switch ( holdFrameMode ) {

	case 0: //don't hold anything

		if ( ( realFrame ) >=frames.size() ) //make sure we are not out of range
			return emptyFrame;
		else {
			if ( pforceLoad && !frames[realFrame].loaded ) {
				return forceLoad (realFrame);
			} else
				return displayFrame ( realFrame, pforceLoad, emptyFrame );
		}
		break;


	case 1: { //Hold current frame, always return the current frame held

				{

					//printf("Returning texID: %i\n",frames[holdFrameCurrent].textureID[slice]);
					if ( holdFrameCurrent-1>=0 && holdFrameCurrent-1<frames.size() ) {
						const int h=holdFrameCurrent-1;
						if ( pforceLoad && !frames[h].loaded )
							return forceLoad ( h );   // a render decodes the held frame
						return frames[h];
					}
					else
						return emptyFrame;
					//return frames[frameNo-1-frameOffset].loaded?frames[frameNo-1-frameOffset].textureID[slice]:0;
				}
			}
			break;

	case 2: { //Hold Edge Frame, if lower than begin, return first, if over last, return last.  Hold first frame, if the frameNumber is less than the offset, return the firstFrame
		//printf("Hold Mode 1, frameNo: %i\nframeOffset: %i\n",frameNo, frameOffset);
		// The edges are the trim range's (pinned while this mode is on); a
		// forced load (render) decodes an edge that is not resident.
		if ( frames.empty() || rangeBegin<0 || rangeEnd>=(int)frames.size() )
			return emptyFrame;
		const int edge = realFrame<=rangeBegin ? rangeBegin : ( realFrame>=rangeEnd ? rangeEnd : -1 );
		if ( edge>=0 ) {
			if ( pforceLoad && !frames[edge].loaded )
				return forceLoad ( edge );
			return frames[edge];
		} else {
			{	//we are within range, return normally
				if ( ( realFrame ) >=frames.size() ) //make sure we are not out of range
					return emptyFrame;
				else {
					if ( pforceLoad && !frames[realFrame].loaded ) {
						return forceLoad ( realFrame);
					} else
						return displayFrame ( realFrame, pforceLoad, emptyFrame );
				}

			}
		}

			}
	}
	return emptyFrame;
}




std::string gfcSequence::framePathAt ( int frameNo ) const {
	const long idx = ( long ) frameNo - 1 - ( long ) frameOffset;
	if ( idx < 0 || idx >= ( long ) frames.size() )
		return std::string();
	return frames[ ( size_t ) idx ].fileName;
}

bool gfcSequence::frameReadyForRender ( int frameNo ) const {
	const long idx = ( long ) frameNo - 1 - ( long ) frameOffset;
	if ( idx < 0 || idx >= ( long ) frames.size() )
		return false;
	// Every frame keeps its load params (savedParams) from initialisation, so
	// a forced load can decode any frame of the clip, resident or not.
	return true;
}

char * gfcSequence::getFilenameatFrame ( int frameNo ) {
	static char tmp[2048];
	/*
	if((frameNo-1-frameOffset)>=frames.size())
	return "frame not found!";
	else{
	if(!frames[frameNo-1-frameOffset].loaded)
	{

	sprintf(tmp,"%s - NOT IN RAM",&frames[frameNo-1-frameOffset].fileName[0]);
	return tmp;
	}
	else
	return &frames[frameNo-1-frameOffset].fileName[0];
	}        */

	switch ( holdFrameMode ) {

	case 0:  //normal hold frame mode
		if ( ( frameNo-1-frameOffset ) >=frames.size() )
			return "frame not found!";
		else {
			if ( !frames[frameNo-1-frameOffset].loaded ) {

				sprintf ( tmp,"%s - NOT IN RAM",&frames[frameNo-1-frameOffset].fileName[0] );
				return tmp;
			} else
				return &frames[frameNo-1-frameOffset].fileName[0];
		}
		break;

	case 1: //Hold first frame, if the frameNumber is less than the offset, return the firstFrame
		// printf("Hold Mode 1, frameNo: %i\nframeOffset: %i\n",frameNo, frameOffset);
		if ( frameNo<=frameOffset ) {
			//printf("Returning texID: %i\n",frames[0].textureID[slice]);
			sprintf ( tmp, "%s (HOLDING FIRST)",&frames[0].fileName[0] );
			return tmp;
		} else { //otherwise play return normally
			if ( ( frameNo-1-frameOffset ) >=frames.size() )
				return "frame not found!";
			else {
				if ( !frames[frameNo-1-frameOffset].loaded ) {

					sprintf ( tmp,"%s - NOT IN RAM",&frames[frameNo-1-frameOffset].fileName[0] );
					return tmp;
				} else
					return &frames[frameNo-1-frameOffset].fileName[0];
			}
			break;

	case 2:
		// printf("Hold Mode 1, frameNo: %i\nframeOffset: %i\n",frameNo, frameOffset);

		{
			//printf("Returning texID: %i\n",frames[0].textureID[slice]);
			if ( holdFrameCurrent-1>=0 && holdFrameCurrent-1<frames.size() ) {
				sprintf ( tmp, "%s (HOLDING %i)",&frames[holdFrameCurrent-1].fileName[0],holdFrameCurrent );
			} else {
				sprintf ( tmp,"NO FRAME TO HOLD" );
			}
			return tmp;
		}

		break;

	case 3: //Hold last frame, if the frameNumber is less than the offset, return the last
		//printf("Hold Mode 3, frameNo: %i\nframeOffset: %i\nframes.size(): %i\n",frameNo, frameOffset,frames.size());
		if ( frameNo-1-frameOffset>=rangeEnd && rangeEnd>=0 && rangeEnd<(int)frames.size() ) {
			sprintf ( tmp, "%s (HOLDING LAST)",&frames[rangeEnd].fileName[0] );
			return tmp;
		} else { //otherwise play return normally
			if ( ( frameNo-1-frameOffset ) >=frames.size() )
				return "frame not found!";
			else {
				if ( !frames[frameNo-1-frameOffset].loaded ) {

					sprintf ( tmp,"%s - NOT IN RAM",&frames[frameNo-1-frameOffset].fileName[0] );
					return tmp;
				} else
					return &frames[frameNo-1-frameOffset].fileName[0];
			}
		}
		break;
		}

	}
	tmp[0] = '\0';
	return tmp;
}

void gfcSequence::setOffset(int offset) {
	frameOffset=offset;
	if ( cacheTrack()>=0 ) {
		frameCache.setTrackOffset(cacheTrack(), offset);
		trackManager.syncCacheTransport(false);   // the loop limits include the offset: sent only if they moved
	}
	if (gfcAudioEngineAlive()) audioEngine.setTrackOffset(trackID-'A', offset);   // sound shifts with the picture
	myGUI->setOffset(offset);
	networkManager.notifyEvent(GFCNETEVENT_OTHER);
	plateManager.setChanged();
}

void startSequenceThread ( gfcSequence* theSequence ) {
	theSequence->loadSequence();
	//printf("Returning from thread starter\n");
}

void gfcSequence::startLoading(int fromTrack,int fromTimeLine) {

	//we get the load params from the myGUI object
	gfcLoadParams tmpParams;
	assert(myGUI);
	//printf("Loading Sequence\n");
	tmpParams=getLoadParamsFromGUI();

	if (fromTimeLine) {
		tmpParams.loadFromTimeline=fromTimeLine-frameOffset;
	} else {
		tmpParams.loadFromTimeline=fromTrack; //if fromTimeline!=0 then the trackWill load from that point on. That only happens when loading from the trackWidget or the timeline.
	}
	if (fromTrack) {//if we are loading from track, override the leave or replace setting, we want to be able to load even if the load window says stay, maybe...
		tmpParams.append=GFC_SEQREPLACE;
	}


	startLoading(tmpParams);



}



/********************************
*Start Loading Sequence
********************************/
void gfcSequence::startLoading ( gfcLoadParams pparams ) {

	if ( pparams.append == GFC_SEQLEAVE) {
		//printf("%c NOT LOADING THIS TRACK BECAUSE LEAVE IS ON\n",trackID);
		return;
	}

	if ( pparams.fileName.empty() || pparams.fileName=="NULL") {
		//printf("%c NOT LOADING THIS TRACK BECAUSE FILENAME IS NULL\n",trackID);

		this->unloadAndClear();

		return;
	}



	stopLoading();
	if ( shuttingDown() )
		return;   // the app is quitting: no new threads

	initializeSequence ( pparams );

	// This load's sound (a fresh clip, filled by the loader thread) replaces the
	// track's in the mixer; a sequence or a silent movie clears it. Frame entry i
	// is movie frame i, so the engine's frame count is the entry count.
	if (gfcAudioEngineAlive()) {
		const std::shared_ptr<gfcAudioClip> clip=audioClip();
		const gfcMovieInfo* info=movieInfo();
		if (clip && info && info->rateNum>0 && info->rateDen>0)
			audioEngine.setTrackClip(trackID-'A', clip, info->rateNum, info->rateDen, int64_t(files.size()));
		else
			audioEngine.clearTrack(trackID-'A');
		audioEngine.setTrackOffset(trackID-'A', frameOffset);
		audioEngine.setTrackMuted(trackID-'A', audioMuted_);
	}

	// A fresh load of a movie sets the player's rate to the movie's exact
	// rational (most recent wins). Reloads from a timeline point keep the user's rate.
	if (pparams.loadFromTimeline==0 && !playbackManager.getKeepRateOnLoad() && isMovie()) {
		int rateNum=0, rateDen=1;
		if (const gfcMovieInfo* info=movieInfo()) { rateNum=info->rateNum; rateDen=info->rateDen; }
		if (rateNum>0 && rateDen>0) playbackManager.setTargetRate(rateNum,rateDen);
	}

	/*if (abortButton)
	abortButton->activate();*/

	// Loading starts at the playhead: the coordinator needs this load's limits.
	trackManager.syncCacheTransport();

	myGUI->activateAbortButton();

	// The sound fills on its own thread, from the playhead first when it is
	// inside the clip, whatever picture loads.
	setAudioTarget(playbackManager.getCurrentFrame());
	startAudioThread();

	//printf ( "Starting thread for %c\n",trackID);
	myThread=new std::thread ( std::bind ( &startSequenceThread,this ) );
	//printf("Started thread for %c\n",trackID);
}





int gfcSequence::getRangeStart() {
	return rangeBegin+this->frameOffset;
}

int gfcSequence::getRangeEnd() {
	return rangeEnd+this->frameOffset;
}

void gfcSequence::stopLoading() {
	//cancel the loading thread. Set under rawQueueMutex, so a loader between
	//its queue-size check and cond.wait() can't miss the notify (the join
	//below would then wait forever: loaders never finish on their own).
	{
		std::lock_guard<std::mutex> lock ( rawQueueMutex );
		loadingCanceled=true;
	}
	// The audio thread winds down in parallel; it is joined after the loader.
	{
		std::lock_guard<std::mutex> lock ( audioWakeMutex_ );
		audioCanceled_.store(true);
	}
	audioWake_.notify_all();
	if ( cacheTrack()>=0 )
		frameCache.cancel(cacheTrack());   // wakes it out of nextJob
	cond.notify_all(); //in case it was waiting for the queue count to go down.
	//wait for the thread to end
	if ( myThread!=NULL ) {
		myThread->join();
		delete myThread;
	}
	myThread=NULL;
	stopAudioThread();
	clearRawQueue();

	if (myGUI)
		myGUI->deactivateAbortButton();

	gResizeTrigger=true;
}

/**
* Pop all the remaining objects in the rawQueue and release their memory.
*/
void gfcSequence::clearRawQueue() {
	int counter=0;
	{//lock the raw queue mutex in case for some inexplicable reason some other thread is using it. Should not happen since we only clear the RawQueue once the loader thread has ended.
		std::lock_guard<std::mutex> lock ( rawQueueMutex )
			;

		while ( !rawFrames.empty() ) {

			counter++;

			gfcFrame tmpRawFrame;
			tmpRawFrame=rawFrames.front();

			rawFrames.pop();

			const int i=tmpRawFrame.indexNumber;
			discardRawFrame(tmpRawFrame);
			// Decoded but never uploaded: back to Unloaded (its reservation goes).
			// Every frame already holds its load params from initializeSequence.
			if ( frameTable_.state(i)==gfcFrameState::Decoding )
				frameTable_.setUnloaded(i);
		}


	}
	// printf("gfcSequence::clearRawQueue: cleared %i raw frames\n",counter);
}





/**
*
* @return The filename of the loaded frame. Returns an empty frame if unsuccesful.
*/
std::string gfcSequence::loadPreview() {
	//clear the previous frame.

	previewFrame.clearFrame();
	if (myGUI->getFilename()=="")
		return "";

	gfcLoadParams params=getLoadParamsFromGUI();

	previewTimer.name="Load Preview";
	//Fl::ready();
	std::string previousSelectedChannel;



	if (!fileExists(params.fileName) && !params.fileName.find("#")!=std::string::npos)
	{
		if(!fileExists(params.fileName) && sett.useSearchPaths)
			params.fileName=findFileInSearchPaths(params.fileName);
		else
			return "";	
	}

	//gfcTimer findSequenceTimer("findSequenceTimer");
	//findSequenceTimer.start();
	findSequenceFiles(previewFiles,true);
	//findSequenceTimer.stop();
	//findSequenceTimer.print();
	if (params.fileName.find("#")!=std::string::npos) {
		printf("Load preview found pound numbering!");
		//if we have pound numbering, then replace the filename in the gui with the first from files
		if (previewFiles.size()>0)
			myGUI->setFilename(previewFiles[0]);
	}





	params.aoi.set(-1,-1,-1,-1);//this is to avoid the preview frame from being cropped.
	params.forceGFLLoading=forceGFLLoading;
	if (gfcIsMovieFile(params.fileName)) {
		// previewFiles[0] is the search-path-resolved movie.
		openMovieReaderFor(previewFiles.empty() ? params.fileName : previewFiles[0]);
		params.movieFrameIndex=0;
		params.movieReader=currentMovieReader();
		if (!previewFiles.empty())
			params.fileName=previewFiles[0];
	}
	previewTimer.start();

	//     currentChannelName=params.channel==-1;

	/*    if(params.channel!=-1)
	previousSelectedChannel=myGUI->getChannelName();
	*/

	previewFrame.loadFrame(params);
	previewFrame.generateTexture();
	myGUI->setChannelOptions(previewFrame.getChannelNames());

	if (params.channel==-1) {
		myGUI->setChannel(0); //if it's the first time, we will usually get a -1 as channel params since we havn't chosen anything yet,
		//so set it to the first by default in that case.
	} else {
		myGUI->setChannel(params.channel); //otherwise set it to what was selected previously
	}

	//     if(previousSelectedChannel!=myGUI->getChannelName()) //this is in case we changed images, and the loaded image does not have the same channels, reset to the first.
	//     	myGUI->setChannel(0);
	previewTimer.stop();
	//previewTimer.print();




	//also setAOI
	this->setAOI(0,0,previewFrame.quadSizeX,previewFrame.quadSizeY);
	//printf("aoi: %i %i %i %i\n",aoi.x,aoi.y,aoi.w,aoi.h);
	/*if (aoi.x==-1 || !myGUI->getCrop())//crop has no size yet, set it to the quadSize
	{
	this->setAOI(0,0,previewFrame.quadSizeX,previewFrame.quadSizeY);
	}
	else
	{
	this->setAOI(0,0,0,0,true); //don't change it, but if it was bigger, it will adjust.
	}*/


	//aoi.set(previewFrame.sizeX/4,previewFrame.sizeY/4,previewFrame.sizeX/2,previewFrame.sizeY/2);
	updateEstimates();
	if (previewFrame.loaded)
		return params.fileName;


	return "";
}


void gfcSequence::setLoadParamsToGUI(gfcLoadParams p) {
	std::stringstream ss;
	ss << p.scale;
	myGUI->setScale(ss.str().c_str());
	myGUI->setFilter(p.filterType);
	myGUI->setFilename(p.fileName);
	aoi=p.aoi;

	myGUI->setAppendOption(p.append);
	myGUI->setCompression(p.compressed);

	this->loadPreview();

	myGUI->setChannel(p.channel);
	if (p.channel>0) { //if the channel is changed, we need to reload it.
		this->loadPreview();
	}

	myGUI->setFromFrame(p.fromFrame);
	myGUI->setToFrame(p.toFrame);

	myGUI->setCrop(p.crop);
}

gfcLoadParams gfcSequence::getDefaultLoadParamsFor(std::string filename)
{
	gfcLoadParams result;
	result.fileName=filename;
	std::vector<std::string> tmpFiles;
	std::string tmpGenericName;
	findSequence(tmpFiles,filename,tmpGenericName,result.fromFrame,result.toFrame);
	return result;
}
/**
* Creates a gfcLoadParams object containing the load parameters according to the sequences GUI.
* @return
*/
gfcLoadParams gfcSequence::getLoadParamsFromGUI() {
	gfcLoadParams result;
	result.stream=false; //myGUI->getStream();
	result.scale=myGUI->getScale();
	result.loadFromTimeline=false; //default to false
	result.gamma=myGUI->getGamma();
	result.fromFrame=myGUI->getFrom();
	result.toFrame=myGUI->getTo();
	result.fileName=myGUI->getFilename();
	result.filterType=myGUI->getFilter();
	result.compressed=myGUI->getCompression();
	result.append=myGUI->getAppendOption();
	result.channel=myGUI->getChannel();
	result.channelName=myGUI->getChannelName();
	result.crop=myGUI->getCrop();
	result.aoi=aoi;
	return result;
}

gfcFrame gfcSequence::getPreviewFrame() {
	return previewFrame;
}

double gfcSequence::getPreviewElapsedSecs() const {
	return previewTimer.getElapsedSecs();
}

/**
* Finds all the filenames that go with the filname in the filename widget. It stores them in the filenames vector. It also sets the GUI
objects that need to be set to the from and to values. It makes the following asumptions:
-the sequence is identified by a succesion of contiguous numbers in a constant place in the file.
-The filename extension contains no numbers
-The extension can be before the file (filename.jpg.0001,filename.jpg.0002...)

* @param filename Filename to use as guide to find the sequence
* @param updateGUI If this is on, we also move the user selected from and to positions. But either way we set the maximum and minimums.

*/
void gfcSequence::findSequenceFiles(std::vector<std::string> &refFiles, int updateGUI) {


	int startNum=0, endNum=0;

	findSequence(refFiles, myGUI->getFilename().c_str(), filenameGeneric, startNum, endNum);

	//

	firstFrame=startNum;
	myGUI->setFromToBounds(startNum,endNum,updateGUI);

	return;

	//     char name[2048]="";
	//     char tmpName[2048]="";
	//     char num[50]="";
	//     char nextString[2048];
	//     int nextNum=0;
	//     char ext[16]="";
	//     int start=0, end=0, padding;
	//     int startNum=0, endNum=0;
	//     bool extensionInverted;
	//     std::vector<std::string> lfiles;
	// 
	//     int filenameLen;
	//     filenameLen=myGUI->getFilename().size();
	//     char *str=new char[filenameLen+1];
	//     sprintf(str,"%s",myGUI->getFilename().c_str());
	// 
	// 
	// 
	//     int i=strlen ( str );
	// 
	//     if ( isdigit ( str[i-1] ) )
	//         extensionInverted=true;
	// 
	//     bool foundLast=false;
	//     lfiles.clear();
	// 
	//     //find the start and end positions of the numeration in the string, the numeration is identified by a digit or by a #
	//     for ( i;i>0;i-- ) {
	//         if ( ( isdigit ( str[i-1] ) || str[i-1]=='#' ) && !foundLast ) {
	//             end=i;
	//             foundLast=true;
	//         } else
	//             if ( !isdigit ( str[i-1] ) && str[i-1]!='#' && foundLast ) {
	//                 start=i+1;
	//                 break;
	//             }
	// 
	//         if ( str[i-1]=='\\' || str[i-1]=='/' ) {
	//             //a backslash was found before the numeration, means that the file is probably not part of a sequence
	//             //therefor simply fill the names vector with one file with that name and return
	//             lfiles.push_back ( str );
	//             filenameGeneric=str;
	//             firstFrame=1;
	//             myGUI->setFromToBounds(1,1,true);
	//             if (str)delete [] str;
	//             return lfiles;
	//         }
	//     }
	// 
	//     padding=end-start+1;
	// 
	//     if ( start!=0 || ! ( start==end && start==0 ) ) {
	//         strncpy ( name,str,start-1 );
	//         strncpy ( ext,&str[end],strlen ( str )-end );
	//         strncpy ( num,&str[start-1],end- ( start-1 ) );
	// 
	//         bool poundNumbering=false;
	// 
	//         if ( num[0]=='#' )
	//             poundNumbering=true;
	// 
	//         if ( poundNumbering ) //if we have a poundNumbering then start the file finding at 0
	//             nextNum=0;
	//         else
	//             nextNum=atoi ( num );
	// 
	//         //printf ( "\n Sequence Info: \nfileName: %s\nFilename numStart/numEnd: %i/%i\nname: %s\next: %s\nnum: %s",str,start,end,name,ext,num );
	// 
	//         //construct the format string for padding
	//         //sould result in someting of the form %s%0paddingi%s
	// 
	//         sprintf ( nextString,"%s%%0%ii%s\0",name,padding,ext );
	//         //construct the filename
	//         sprintf ( tmpName,nextString,nextNum );
	// 
	//         //construct the generic filename with name, padding and ext using #
	//         {
	//             filenameGeneric=name;
	//             for (int i=0;i<padding;i++) {
	//                 filenameGeneric+="#";
	//             }
	//             filenameGeneric+=ext;
	//         }
	//         //	printf("\nConstructed filename: %s\n",tmpName);
	// 
	//         if ( !poundNumbering ) { //if it's not a pound numbering, then start looking under the filename and above the filename
	//             //find file before the specified filename i-1,i-2... while i-x positive
	// 
	//             while ( ( fileExists ( tmpName ) ) && nextNum>=0 ) {
	//                 if ( fileExists ( tmpName ) )
	//                     startNum=nextNum;
	//                 sprintf ( nextString,"%s%%0%ii%s\0",name,padding,ext );
	//                 //construct the filename
	//                 nextNum-=1;
	//                 sprintf ( tmpName,nextString,nextNum );
	//                 //	printf("\nConstructed filename: %s\n",tmpName);
	// 
	// 
	//             }
	// 
	// 
	//             //find file before the specified filename i-1,i-2... while i-x positive
	//             nextNum=atoi ( num ) +1;
	//             sprintf ( tmpName,nextString,nextNum );
	//             //printf("\nConstructed filename: %s\n",tmpName);
	//             endNum=startNum;
	// 
	//             while ( fileExists ( tmpName ) ) {
	//                 endNum=nextNum;
	//                 sprintf ( nextString,"%s%%0%ii%s",name,padding,ext );
	//                 //construct the filename
	//                 nextNum+=1;
	//                 sprintf ( tmpName,nextString,nextNum );
	//                 //printf("\nConstructed filename: %s\n",tmpName);
	//             }
	//         } else {//if it's pound numbering start looking for files at frame 0, after finding the first frame startNum (not always 0) find the last file
	//             // endNum
	//             while ( nextNum<9999999 ) {
	//                 if ( fileExists ( tmpName ) ) {
	//                     startNum=nextNum;
	//                     break;
	//                 }
	//                 sprintf ( nextString,"%s%%0%ii%s",name,padding,ext );
	//                 //construct the filename
	//                 nextNum++;
	//                 sprintf ( tmpName,nextString,nextNum );
	//                 //	printf("\nConstructed filename: %s\n",tmpName);
	// 
	// 
	//             }
	//             while ( nextNum<9999999 ) { //find the last one, that is, till the nextNum file does not exist
	//                 if ( !fileExists ( tmpName ) ) {
	//                     endNum=nextNum;
	//                     break;
	//                 }
	//                 sprintf ( nextString,"%s%%0%ii%s",name,padding,ext );
	//                 //construct the filename
	//                 nextNum++;
	//                 sprintf ( tmpName,nextString,nextNum );
	//                 //	printf("\nConstructed filename: %s\n",tmpName);
	// 
	// 
	//             }
	//         }
	// 
	//         i=lfiles.size();
	//         //printf("startNum/endNum: %i/%i\n",startNum, endNum);
	//         for ( i=startNum;i<=endNum;i++ ) {
	//             sprintf ( tmpName,nextString,i );
	//             lfiles.push_back ( tmpName );
	//             //printf("files[%i]: %s",i,&files[i][0]);
	//         }
	//     }
	//     //printf("findSequenceFiles files.size:%i\n",files.size());
	//     firstFrame=startNum;


}

void gfcSequence::setVisibleFromAndTo(int visibleFrom, int visibleTo) {
	myGUI->setTrackVisibleRange(visibleFrom, visibleTo);
}

int gfcSequence::getOffset() {
	return frameOffset;
}

void gfcSequence::setAOI(int px, int py, int pw, int ph, bool relative) {

	/*if (myGUI->getCrop() && myGUI->getWindowVisible())*/ {
		if (relative) {

			aoi.x+=px;
			aoi.y+=py;
			aoi.w+=pw;
			aoi.h+=ph;
		} else {
			aoi.x=px;
			aoi.y=py;
			aoi.w=pw;
			aoi.h=ph;
		}


		if (aoi.x<0)
			aoi.x=0;
		if (aoi.y<0)
			aoi.y=0;

		if (aoi.x>previewFrame.sizeX)
			aoi.x=previewFrame.sizeX;
		if (aoi.y>previewFrame.sizeY)
			aoi.y=previewFrame.sizeY;

		//prevent negative width or height
		if (aoi.w<0)
			aoi.w=0;
		if (aoi.h<0)
			aoi.h=0;


		//prevent width or height from going out of bounds taking into account the position of the origin.
		if (aoi.x+aoi.w>previewFrame.sizeX)
			aoi.w=previewFrame.sizeX-aoi.x;
		if (aoi.y+aoi.h>previewFrame.sizeY)
			aoi.h=previewFrame.sizeY-aoi.y;

		myGUI->setAOI(aoi);
		updateEstimates();
		//printf("AOI %i %i %i %i\n",aoi.x, aoi.y, aoi.w, aoi.h);
		plateManager.setChanged();
	}
}

gfcRectang gfcSequence::getAOI() {
	static gfcRectang emptyRectang;
	
	if (myGUI)
	{
	
	if (myGUI->getCrop() && myGUI->getWindowVisible()) {

		return aoi;

	}
	else {
		return emptyRectang;
	}
	}
	else {
		return emptyRectang;
	}
}

int gfcSequence::getNumFrames() {
	return files.size();
}

int gfcSequence::getLoadedFrameCount() {
	return residentCount();
}

void gfcSequence::setTestAudioBlockDelayMs(int ms) {
	g_testAudioBlockDelayMs.store(ms<0?0:ms);
}

void gfcSequence::setTestDecodeDelayMs(int ms) {
	g_testDecodeDelayMs.store(ms<0?0:ms);
}

long gfcSequence::loadAnnouncements() {
	return g_loadAnnouncements.load();
}

std::thread::id gfcSequence::loaderThreadId() const {
	return myThread ? myThread->get_id() : std::thread::id();
}

bool gfcSequence::threadsRunning() const {
	return myThread!=nullptr || audioThread_!=nullptr;
}

namespace { std::atomic<bool> g_sequencesShuttingDown{false}; }

void gfcSequence::beginShutdown() { g_sequencesShuttingDown.store(true); }
bool gfcSequence::shuttingDown() { return g_sequencesShuttingDown.load(); }

bool gfcSequence::framePending(int timelineFrame) const {
	int idx = timelineFrame - 1 - frameOffset;
	if ( holdFrameMode==1 )
		idx = holdFrameCurrent - 1;   // the held frame is shown everywhere
	else if ( holdFrameMode==2 && rangeBegin>=0 && rangeEnd>=rangeBegin )
		idx = std::clamp ( idx, rangeBegin, rangeEnd );   // the edge is shown outside
	if ( frames.empty() || idx<rangeBegin || idx>rangeEnd || idx>=(int)frames.size() )
		return false;
	const gfcFrameState st = frameTable_.state(idx);
	return st!=gfcFrameState::Resident && st!=gfcFrameState::Evicting;
}

size_t gfcSequence::estimatedFrameBytes() const {
	if ( !previewFrame.loaded )
		return 0;
	size_t w=previewFrame.sizeX, h=previewFrame.sizeY;
	if ( params.crop && params.aoi.w>0 && params.aoi.h>0 ) {
		w=(size_t)params.aoi.w;
		h=(size_t)params.aoi.h;
	}
	const size_t px=w*h;
	switch ( params.compressed ) {
	case GFC_16BPC:
	case GFC_16HALF:  return px*8;
	case GFC_S3TCDX1: return px;      // the loader uploads DXT5: one byte per pixel
	case GFC_4BPC:    return px*2;
	case GFC_8BPC:
	default:          return px*4;
	}
}

const GfcThumbnail& gfcSequence::getThumbnail(int frameIndex) const {
	static const GfcThumbnail empty;
	if (frameIndex < 0 || frameIndex >= (int)frames.size()) return empty;
	return frames[frameIndex].thumbnail;
}

int gfcSequence::getNumPreviewFrames()
{
	return previewFiles.size();
}

/**
* Calculates various statistic about the sequence, like an estimate load time, aprox memory footprint (per frame and for selected range), and how many frames will fit in the available RAM. It outputs it to the GUI.
*/
void gfcSequence::updateEstimates() {
	char estimate[4096];

	int aproxBytesPerFrame=0;
	float aproxMBytesperFrame=0;
	float aproxMBperSequence=0;

	int sizeX=0;
	int sizeY=0;
	if (previewFrame.loaded) {


		if (myGUI->getCrop()) {
			sizeX=aoi.w;
			sizeY=aoi.h;
		} else {
			sizeX=previewFrame.quadSizeX;
			sizeY=previewFrame.quadSizeY;
		}

		int numOfComponents=previewFrame.originalChannels;

		switch (myGUI->getCompression()) {
		case GFC_8BPC:
			aproxBytesPerFrame=sizeX*sizeY*numOfComponents;
			break;

		case GFC_16HALF:
		case GFC_16BPC:
			aproxBytesPerFrame=sizeX*sizeY*numOfComponents*2;
			break;

		case GFC_4BPC:
			aproxBytesPerFrame=sizeX*sizeY*numOfComponents/2.0;
			break;

		case GFC_S3TCDX1:
			aproxBytesPerFrame=(sizeX/4)*(sizeY/4)*8;
			break;
		}

		float freeRam=memoryManager.getFreeRAM();
		aproxMBytesperFrame=(float)aproxBytesPerFrame/1024.0/1024.0;
		aproxMBperSequence=(float)aproxMBytesperFrame*(float)files.size();
		sprintf(estimate,"%.1f/frame-%.1f/seq (MB)\n%.3f/frame-%.3f/seq (S)\n%.1f%% will fit\n%.1fMB RAM Free",
			aproxMBytesperFrame,(float)aproxMBytesperFrame*(float)previewFiles.size(),previewTimer.getElapsedSecs(),previewTimer.getElapsedSecs()*previewFiles.size(),
			freeRam/aproxMBperSequence>1.0?100.0:100.0*freeRam/aproxMBperSequence,
			freeRam);

		//printf("Free Ram=%f\n",freeRam);
		//printf("%s\n",estimate);
	}

	myGUI->setEstimates(estimate);
}

/**
* True when the track has no frames (nothing initialised for loading),
* whatever is resident.
*/
bool gfcSequence::isEmpty() {
	return frames.empty();
}

void gfcSequence::setRecentlyLoaded(std::vector<std::string> filenames) {
	myGUI->setRecentlyLoaded(filenames);
}

void gfcSequence::clearPreviewFrame() {
	previewFrame.clearFrame();
	std::vector<std::string> empty;
	myGUI->setChannelOptions(empty);
}

void gfcSequence::pinFrame(int index, bool on) {
	// Through the coordinator, so a waiting loader wakes for it.
	const int track=cacheTrack();
	if ( track>=0 )
		frameCache.pin(track, index, on);
	else
		frameTable_.setPinned(index, on);
}

void gfcSequence::pinHoldEdges(bool on) {
	if ( frames.empty() )
		return;
	pinFrame(rangeBegin, on);
	pinFrame(rangeEnd, on);
}

void gfcSequence::updateHeldFramePin() {
	const int index=holdFrameCurrent-1;
	const int want=( holdFrameMode==1 && index>=0 && index<(int)frames.size() ) ? index : -1;
	if ( heldPin_>=0 && heldPin_!=want )
		pinFrame(heldPin_, false);
	if ( want>=0 )
		pinFrame(want, true);
	heldPin_=want;
}

void gfcSequence::setHoldMode(int holdMode, int holdFrame) {
	holdFrameMode=holdMode;
	holdFrameCurrent=holdFrame-frameOffset;
	// The pins share one store: unpin first, then pin, so a held frame that is
	// also a trim edge keeps its pin across a mode change.
	if ( heldPin_>=0 && !( holdFrameMode==1 && heldPin_==holdFrameCurrent-1 ) ) {
		pinFrame(heldPin_, false);
		heldPin_=-1;
	}
	// Hold-edge shows the trim edges, so they are never evicted while it is on.
	pinHoldEdges(holdFrameMode==2);
	// Hold-frame shows one frame: it is never evicted while it is held.
	updateHeldFramePin();
	if (holdFrameMode==1) {
		//TODO: Add some visual indication on the GUI's trackwidget to show that we are holding some frame, maybe grayout the progress bar and only show a green slit around the showing frame.
	}
	networkManager.notifyEvent(GFCNETEVENT_OTHER);
	plateManager.setChanged();

}

int gfcSequence::getHoldMode() {
	return holdFrameMode;
}

void gfcSequence::setForceGFLLoading(bool value) {
	forceGFLLoading=value;
}

void gfcSequence::setContinueLoadingOnError( bool value ) {
	continueLoadingOnError=value;
}

gfcNetTrackStateInfo gfcSequence::getTrackStateInfo() {
	gfcNetTrackStateInfo result;
	result.frameOffset=this->frameOffset;
	result.holdMode=this->holdFrameMode;
	result.holdFrame=this->holdFrameCurrent;
	return result;
}

void gfcSequence::setTrackStateInfo(gfcNetTrackStateInfo info) {
	this->setOffset(info.frameOffset);
	this->setHoldMode(info.holdMode, info.holdFrame);
}




void gfcSequence::saveTrackSessionParameters(XMLNode & trackNode) {
	saveSetting("filename",myGUI->getFilename(),trackNode);
	saveSetting("from",myGUI->getFrom(),trackNode);
	saveSetting("to",myGUI->getTo(),trackNode);
	saveSetting("mode",myGUI->getAppendOption(),trackNode);
	saveSetting("scale",myGUI->getScale(),trackNode);
	saveSetting("filter",myGUI->getFilter(),trackNode);
	saveSetting("crop",myGUI->getCrop(),trackNode);
	saveSetting("aoiX",myGUI->getAOI().x,trackNode);
	saveSetting("aoiY",myGUI->getAOI().y,trackNode);
	saveSetting("aoiW",myGUI->getAOI().w,trackNode);
	saveSetting("aoiH",myGUI->getAOI().h,trackNode);
	saveSetting("depth",myGUI->getCompression(),trackNode);

	saveSetting("offset",getOffset(),trackNode);
	saveSetting("holdMode",getHoldMode(),trackNode);
	saveSetting("holdFrame",this->holdFrameCurrent,trackNode);
	saveSetting("muted",audioMuted_?1:0,trackNode);

}

void gfcSequence::loadTrackSessionParameters(XMLNode & trackNode) {
	std::string tmpFilename;
	readSettingString("filename",tmpFilename,trackNode);
	myGUI->setFilename(tmpFilename);
	//myGUI->setFilename(readAttributeFromNode<std::string>("filename",trackNode,"")); //this would cause filenames with spaces to stop at the first one.
	myGUI->setAppendOption(readAttributeFromNode<int>("mode",trackNode,0));
	myGUI->setScale(readAttributeFromNode<std::string>("scale",trackNode,"100"));
	myGUI->setFilter(readAttributeFromNode<int>("filter",trackNode,0));
	myGUI->setCrop(readAttributeFromNode<int>("crop",trackNode,0));

	myGUI->setCompression(readAttributeFromNode<int>("depth",trackNode,0));

	setOffset(readAttributeFromNode<int>("offset",trackNode,0));
	setHoldMode(readAttributeFromNode<int>("holdMode",trackNode,0),
		readAttributeFromNode<int>("holdFrame",trackNode,0));



	setAudioMuted(readAttributeFromNode<int>("muted",trackNode,0)!=0);

	this->loadPreview();
	//set the from and to limits at the end, after loading the preview, also the AOI
	myGUI->setFromFrame(readAttributeFromNode<int>("from",trackNode,0));
	myGUI->setToFrame(readAttributeFromNode<int>("to",trackNode,0));
	this->setAOI((readAttributeFromNode<int>("aoiX",trackNode,-1)),
		(readAttributeFromNode<int>("aoiY",trackNode,-1)),
		(readAttributeFromNode<int>("aoiW",trackNode,-1)),
		(readAttributeFromNode<int>("aoiH",trackNode,-1)));

}

void gfcSequence::updateTrackWidget() {
	myGUI->updateTrackWidget();
}

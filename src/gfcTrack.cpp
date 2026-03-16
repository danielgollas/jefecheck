

// gfcTrack.cpp: implementation of the gfcTrack class.
//
//////////////////////////////////////////////////////////////////////

#include <glad/glad.h>
#include <functional>
#include "gfcSequence.h"
#include <string.h>
#include <stdio.h>
#include <vector>
#include <ctype.h>
#include <sys/stat.h>
#include <FL/Fl.H>
#include "loadWindow.h"
#include "mainWindow.h"
#include "exrWindow.h"
#include <FL/Fl_Slider.H>
#include <FL/Fl_Progress.H>
#include <FL/fl_ask.H>
#include "GlViewport.h"
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

#include "gfcmemorymanager.h"
extern gfcMemoryManager memoryManager;

#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;


#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

extern LoadWindow lw;
extern MainWindow mw;
extern ExrWindow ew;
extern bool mainWindowExists;
extern bool npotTextures;
extern std::mutex gGLMutex;
extern void* gGLContext;
extern bool gResizeTrigger;
std::vector<int> dummyVectorForLut;

//macro to assiste with PBO texture uploads
#define BUFFER_OFFSET(i) ((char *)NULL + (i))

#ifdef WIN32
#include "glext.h"
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
			endNum=startNum;
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
	for ( int i=0;i<numFrames;i++ ) {
		if ( loadedFrames.empty() )
			return false; //return failure if no frames are loaded

		int lastLoaded=loadedFrames.front();
		loadedFrames.pop();
		for ( int j=0;j<sett.numOfPartitions;j++ )
			glDeleteTextures ( 1,&frames[lastLoaded].textureID );

		if ( frames.size() <=lastLoaded ) //return failure if last loaded is out of range of the frame vector, very unlikely
			return false;
		frames[lastLoaded].loaded=false;
	}
	return true; //return true if succesfull
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
	Fl::check();
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
		//    	printf("gfcSequence::generateTextures: rawQueue not empty\n");
		if (tmpRawFrame.loaded ) {
			//            printf ( "gfcSequence::generateTextures:  generating texture...\n" );
			GLuint tmpTexID=tmpRawFrame.generateTexture();
			//printf ( "texID=%i\n",tmpTexID );
			//check for gl errors.

			//5. Put into the regular frames vector.
			if ( tmpRawFrame.indexNumber<frames.size() && tmpRawFrame.indexNumber>=0 ) {
				//printf("\nFrame inserted into %i\n",tmpRawFrame.indexNumber) ;
				frames[tmpRawFrame.indexNumber]=tmpRawFrame;
				loadedFrames.push ( tmpRawFrame.indexNumber );
				//printf ( "Inserted into Frame vector[%i]:\n",tmpRawFrame.indexNumber );
				//frames[tmpRawFrame.indexNumber].dpxSlice.printInfo();
				rangeBegin=loadedFrames.front();
				if ( loadedFrames.size() >0 )
					rangeEnd=loadedFrames.back();

				myGUI->setLoadedRange(loadedFrames.size());
			} else {
				printf ( "\nOut of sequence bounds! Index: %i frames.size: %i \a\n",tmpRawFrame.indexNumber,frames.size() );
			}
		} else {
			//if the frame is not loaded, then we simply store it with it's parameters for future use.
			//printf("Raw queue not empty but not generating texture, just storing tmpRawFrame\n");


			frames[tmpRawFrame.indexNumber]=tmpRawFrame;
			if (tmpRawFrame.skipped) {
				//if we did not load but we skipped and continued then we are ok and we should push into the loaded frames to keep the display correct.
				// but if we stopped because of cancelation or RAM issues then we should not push into the loaded frames.
				loadedFrames.push ( tmpRawFrame.indexNumber );
			}

			if ( loadedFrames.size() >0 ) {
				rangeEnd=loadedFrames.back();
				myGUI->setLoadedRange(loadedFrames.size());
			}
		}
	}





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
}

int gfcSequence::initializeSequence ( gfcLoadParams pparams ) {
	params = pparams;
	//printf ( "\nInitializing Sequence:\n" );

	loadingFromTimeline=pparams.loadFromTimeline;
	int tmpMaxFrames=maxFramesToLoad;

	if ( params.append==GFC_SEQERASE || params.append==GFC_SEQREPLACE )
		clearSequence();

	maxFramesToLoad=tmpMaxFrames; //this is necesary because clear sequence deletes the maxFrames to load.

	//printf ( "Finding Sequence for %s\n",params.fileName.c_str() );

	//files=findSequence ( &params.fileName[0],label,&firstFrame ); //find the sequence size here and resize the frames vector so that it is ready to process frames from the raw queue.


	prepareSequenceFiles();


	//printf ( " Resizing frame vector to %i frames\n",files.size() );
	frames.resize ( files.size() );

	for ( int i=0;i<files.size();i++ ) {
		frames[i].fileName=files[i];
	}

	loadingCanceled=false;
	return 0;
}

int gfcSequence::loadSequence() {

	int endFrame;
	int framesSize;
	int withinMemoryLimits=true;

	if ( params.stream ) {
		printf ( "Streaming\n" );
	} else {


		//std::vector<std::string> localFiles=files;
		//printf ( "Regular loading\n" );
		//printf ( "First Frame=%i\n",firstFrame );
		int startFrame;
		if (params.loadFromTimeline==0) {
			startFrame=params.fromFrame-firstFrame;
			networkManager.sendSystemChatMessage(filenameGeneric,GFCNETMESSAGETYPE_LOAD);
		} else {
			startFrame=params.loadFromTimeline;
		}
		if ( startFrame<0 )
			startFrame=0;

		endFrame=params.toFrame-firstFrame;
		framesSize=frames.size();
		if ( endFrame>framesSize )
			endFrame=framesSize;
		if (endFrame<0)
			endFrame=0;

		if (startFrame>endFrame)
			startFrame=endFrame;

		if (endFrame>=files.size())
			endFrame=files.size()-1;
		/* if ( mainWindowExists ) {
		mw.timeLine->maximum ( mw.vp->getMaxTrackLenght() );
		mw.playUpToInput->value ( mw.vp->getMaxTrackLenght() );

		}*/

		//myGUI->setTrackWidget();

		myGUI->activateAbortButton();
		myGUI->setTrackRange(1,framesSize);
		myGUI->setTotalFramesToLoad(startFrame+1,endFrame-startFrame+1); //these functions consider the first frame to be 1, not 0 as our vectors, so we add 1
		myGUI->setLoadedRange(0);
		myGUI->setTrackLabel(GetFilenameNoPath(filenameGeneric));
		//load each of the frames we have to load
		bool continueLoading=true;
		withinMemoryLimits=true;
		printf("***Track %c: Loading from %i to %i\n",trackID,startFrame,endFrame);
		for ( int i=startFrame;i<=endFrame;i++ ) {

			{ 	

				if(withinMemoryLimits && !loadingCanceled)
				{
					//only check if we are within memory limits if we are not sure that we are not, if for some reason we again came 
					//within limits, we would end up with some stored frames and some loaded frames, which is bad.
					withinMemoryLimits=memoryManager.withinLimits();
				}

				/*if ( loadingCanceled || !withinMemoryLimits)
				break;*/

				gfcLoadParams tmpParams=params;
				tmpParams.forceGFLLoading=forceGFLLoading;
				//printf("%i\n",i);
				tmpParams.fileName=files[i];
				//printf ( "%c-%i: Loading: %s\n",trackID,i, GetFilenameNoPath(tmpParams.fileName).c_str() );
				gfcFrame tmpFrame;

				if ( loadingCanceled || !withinMemoryLimits) { //if we need to stop loading, we should still store the params on the frame.
					tmpFrame.savedParams=tmpParams;
					//printf("saving saved params in tmpFrame (%i) sizeof: %i\n",i,sizeof(tmpParams));
				} else {
					int loadResult=tmpFrame.loadFrame ( tmpParams );
					if (loadResult==GFCFRAME_LOADERROR_NO_MORE_MEMORY)
					{
						printf("No more memory while loading frame, stopping sequence load\n");
						loadingCanceled=true;
						//withinMemoryLimits=false;
					}

					if (!tmpFrame.loaded) {
						if (!continueLoadingOnError) {
							loadingCanceled=true;
						} else {
							tmpFrame.skipped=true;
						}

					}


				}
				tmpFrame.indexNumber=i;

				{//lock the mutex here to push the frame into the rawFrame stack, which is shared by the main thread, which is the one that generates the textures.
					std::unique_lock<std::mutex> lock ( rawQueueMutex );

					if (rawFrames.size()>sett.maximumFramesInQueue)
					{	//wait for a condition signal that the rawFrames have reduced size
						while(rawFrames.size()>sett.maximumFramesInQueue && !loadingCanceled)
							cond.wait(lock);
					}


					rawFrames.push ( tmpFrame );
					//printf("pushed tmpFrame into raw queue(%i)\n",i);
					//if (rawFrames.size()>sett.maximumFramesInQueue)
					//    continueLoading=false;
					//printf ( "Pushed tmpFrame into raw queue\n" );
				}


				//this while loop is a control method for limiting the number of rawFrames we store in the queue. We dont want to have to many rawFrames in the queue since that means it takes longer to generate textures than it takes to load the raw frames.
				/*while (!continueLoading && !loadingCanceled && sett.maximumFramesInQueue>0) {
				{//lock the mutex here to check if the rawFrames queue is over it's allowed maximum lenght
				std::lock_guard<std::mutex> lock ( rawQueueMutex );

				printf("Waiting for raw queue to go under %i (currently %i)\n",sett.maximumFramesInQueue, rawFrames.size());
				if (rawFrames.size()<sett.maximumFramesInQueue)
				continueLoading=true;
				}
				#ifndef WIN32
				//usleep ( 2 );
				#else
				//Sleep ( 2 );
				#endif
				}*/

				gResizeTrigger=true;

			}
		} //end of mutex scope

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

bool gfcSequence::maxFramesReached() {
	return loadedFrames.size() +rawFrames.size() >=maxFramesToLoad;
}

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


	//printf ( "%c Cleared Files, size is : %i\n",trackID, files.size() );
	while ( !loadedFrames.empty() )
		loadedFrames.pop();


	this->clearRawQueue();

	if (myGUI) {
		myGUI->setLoadedRange(0);
		myGUI->setTotalFramesToLoad(0,0);
		myGUI->setTrackRange(0,0);
	}
	maxFramesToLoad=0;

	rangeBegin=rangeEnd=0;
	//
	//printf ( "done\n" );
	return 0; //return ok
}

void gfcSequence::loadForStreaming() { //this function should be called from an independent thread started in the idle func.

	while ( true ) {

		//printf("monitoring track %c to load new stuff!\n", this->trackID);

		//find out if we need to load a new frame ahead or behind, based on the current position of the timeline, and what frames are loaded

		//load a rawFrame, indicating that it is a streaming frame, and that it should load the stuff into a specific pointer, mainly this sequence's PBO.

		//lock and push raw frame into raw frame queue, it will be converted to a texture in the main thread.


	}
	//start a frameSlice loa

	/*{//lock the mutex here to push the frame into the rawFrame stack, which is shared by the main thread, which is the one that generates the textures.
	std::lock_guard<std::mutex> lock ( rawQueueMutex )
	;
	rawFrames.push ( tmpRawFrame );
	mw.playUpToInput->value ( mw.vp->getMaxTrackLenght() );
	//frames.push_back(tmpFrame);
	}*/

}

gfcFrame gfcSequence::forceLoad ( int frame ) {
	//the frame parameter should already consider offset. The function does not check for range.

	//clear the previous frame.

	forcedFrame=frames[frame];
	printf("Forced loading: %s\n",forcedFrame.savedParams.fileName.c_str());
	forcedFrame.loadFrame();
	forcedFrame.generateTexture();

	return forcedFrame;

}

void gfcSequence::cleanForcedLoad() {
	if ( forcedFrame.loaded ) {
		if ( forcedFrame.textureID )
			glDeleteTextures ( 1,&forcedFrame.textureID );

		forcedFrame.loaded=false;
	}
}

Rectang gfcSequence::getFrameSizeAt ( int frameNo ) {

	static Rectang returnValue;
	return returnValue;

}

gfcFrame gfcSequence::getFrame ( int frameNo,bool pforceLoad ) {
	gfcFrame emptyFrame;
	/*emptyFrame.loaded=false;
	emptyFrame.fileName="Frame not loaded";*/

	int realFrame=frameNo-frameOffset-1;

	//if there is nothing loaded, and we are not forcing the load, then return the empty frame
	if (loadedFrames.size()==0 && !pforceLoad)
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
				return frames[realFrame];
		}
		break;


	case 1: { //Hold current frame, always return the current frame held

				{

					//printf("Returning texID: %i\n",frames[holdFrameCurrent].textureID[slice]);
					if ( holdFrameCurrent-1>=0 && holdFrameCurrent-1<frames.size() )
						return frames[holdFrameCurrent-1];
					else
						return emptyFrame;
					//return frames[frameNo-1-frameOffset].loaded?frames[frameNo-1-frameOffset].textureID[slice]:0;
				}
			}
			break;

	case 2: { //Hold Edge Frame, if lower than begin, return first, if over last, return last.  Hold first frame, if the frameNumber is less than the offset, return the firstFrame
		//printf("Hold Mode 1, frameNo: %i\nframeOffset: %i\n",frameNo, frameOffset);
		int reallyRealFrame=0;

		if ( realFrame<=loadedFrames.front() ) { //return first loaded frame if we are under
			return frames[loadedFrames.front()];
		} else {
			if (realFrame>=loadedFrames.back()) { //we are over, return the last one;
				return frames[loadedFrames.back()];
			} else {	//we are within range, return normally
				if ( ( realFrame ) >=frames.size() ) //make sure we are not out of range
					return emptyFrame;
				else {
					if ( pforceLoad && !frames[realFrame].loaded ) {
						return forceLoad ( realFrame);
					} else
						return frames[realFrame];
				}

			}
		}

			}
	}
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
		if ( frameNo>=loadedFrames.size() +frameOffset ) {
			//printf("Returning texID: %i\n",frames[0].textureID[slice]);
			sprintf ( tmp, "%s (HOLDING LAST)",&frames[loadedFrames.size()-1].fileName[0] );
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
}

void gfcSequence::setOffset(int offset) {
	frameOffset=offset;
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

	initializeSequence ( pparams );

	/*if (abortButton)
	abortButton->activate();*/

	myGUI->activateAbortButton();

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
	//cancel the loading thread
	loadingCanceled=true;
	cond.notify_all(); //in case it was waiting for the queue count to go down.
	//wait for the thread to end
	if ( myThread!=NULL ) {
		myThread->join();
		//printf ( "Thread Joined!\n" );
	}
	myThread=NULL;
	clearRawQueue();

	if (abortButton)
		abortButton->deactivate();

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

			tmpRawFrame.releaseMemory();
			//even though we did not generate any textures and have freed the imageloader memory
			//we still store the rawFrames, marking them as not loaded.
			tmpRawFrame.loaded=false;
			frames[tmpRawFrame.indexNumber]=tmpRawFrame;
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
	fl_cursor(FL_CURSOR_WAIT);
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
	fl_cursor(FL_CURSOR_DEFAULT);
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
* Tells if the track is empty based on the loadedFrames queue
* @return
*/
bool gfcSequence::isEmpty() {
	return loadedFrames.empty();
}

void gfcSequence::setRecentlyLoaded(std::vector<std::string> filenames) {
	myGUI->setRecentlyLoaded(filenames);
}

void gfcSequence::clearPreviewFrame() {
	previewFrame.clearFrame();
	std::vector<std::string> empty;
	myGUI->setChannelOptions(empty);
}

void gfcSequence::setHoldMode(int holdMode, int holdFrame) {
	holdFrameMode=holdMode;
	holdFrameCurrent=holdFrame-frameOffset;
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

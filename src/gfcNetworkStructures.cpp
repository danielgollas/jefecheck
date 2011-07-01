#include "gfcNetworkStructures.h"

#include <stdio.h>
#include "mainWindow.h"
#include "RakPeerInterface.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <stdlib.h> // For atoi
#include <cstring> // For strlen
#include "Rand.h"
#include "RakNetStatistics.h"
#include "MessageIdentifiers.h"
#include <stdio.h>
#include "GetTime.h"
#include "RakAssert.h"
#include "RakSleep.h"
#include "BitStream.h"
#include "StringCompressor.h"
#include "gfcStructures.h"
#include "mainWindow.h"
#include "gfcfx.h"
#include "xmlParser.h"
#include <fstream>
#include <FL/fl_ask.h>
#ifdef _WIN32
#include <windows.h> // Sleep
#else
#include <unistd.h> // usleep
#include <cstdio>
#endif

#include "gfcfxmanager.h"
extern gfcFXManager fxManager;

#include "gfclutmanager.h"
extern gfcLUTManager lutManager;


void serializeLUT ( CubeLUT* theLUT, RakNet::BitStream* bs ) {
    //read the lut file (whatever format it is) and send the filename w/o path, and then send the binary file.
    FILE *fp;
    long len;
    char *buf;
    fp=fopen ( theLUT->filename,"rb" );
    fseek ( fp,0,SEEK_END ); //go to end
    len=ftell ( fp ); //get position at end (length)
    fseek ( fp,0,SEEK_SET ); //go to beg.
    buf=new char[len]; //malloc buffer
    fread ( buf,len,1,fp ); //read into buffer
    fclose ( fp );

    std::string filenameNoPath=GetFilenameNoPath ( theLUT->filename );
    //printf ( "Filename is %s (%i bytes)\n",filenameNoPath.c_str(), filenameNoPath.size() );

    //write the size of the name and then the filename (w/o path)
    bs->Write ( ( int ) filenameNoPath.size() +1 );
    StringCompressor::Instance()->EncodeString ( filenameNoPath.c_str(),filenameNoPath.size() +1,bs );

    //write the size of the file and then write the file
    bs->Write ( ( int ) len );
    bs->Write ( ( char* ) buf,len );
}

void unserializeLUT ( RakNet::BitStream* bs ) {

    int fileNameLen=0;
    char *filenameNoPathBuff;
    int fileLen=0;
    char *fileBuff;

    //READ FROM BITSTREAM
    /************/
    {
        bs->Read ( fileNameLen );
        filenameNoPathBuff=new char[fileNameLen];
        StringCompressor::Instance()->DecodeString ( filenameNoPathBuff,fileNameLen,bs );

        bs->Read ( fileLen );
        printf ( "Got %i bytes from fileLen\n",fileLen );
        fileBuff=new char[fileLen];
        bs->Read ( ( char* ) fileBuff,fileLen );
    }
    /*************/

    std::ofstream outFile;

    std::string lutFilename=sett.receivedPath+filenameNoPathBuff;

    //write lut file
    outFile.open ( lutFilename.c_str(),std::ostream::binary );
    //outFile<<fileBuff;
    outFile.write ( fileBuff, fileLen );
    outFile.close();


    lutManager.loadLUT(lutFilename);
    //theLUT->load ( lutFilename.c_str() );
}

void serializeFX ( const gfcFX* theFX, RakNet::BitStream* bs ) {


	
	//TODO: FUCK IT SEND THE WHOLE FILE IN A STRING AND LET THE RECEIVER PARSE IT, this way we can also save the received fxs to a predetermined folder.
    
	//WE NEED TO CONVERT THE READ STUFF TO C-STRINGS SO WE CAN TRANSMIT THEM CORRECTLY!... this is so that line breaks
	//are correctly translated by c on each side... mac, windows or linux.... http://www.editpadpro.com/tricklinebreak.html
    
	//todo: maybe we can read as binary and just send like we do with luts

	
	

	//1. read the whole jfx
	std::string theJfx=ReadTextFileIntoString(theFX->filename);
	printf("theJfx size: %i\n theJfx:\n %s\n",theJfx.length(),theJfx.c_str());
	//2. write the jfx filename
    StringCompressor::Instance()->EncodeString ( GetFilenameNoPath ( theFX->filename ).c_str(),GFCNET_MAX_TEXT_LENGHT,bs );
    //3. write the size of the file and then write the file
    bs->Write ( ( int ) theJfx.length()+1 );
    StringCompressor::Instance()->EncodeString ( theJfx.c_str(),theJfx.length()+1,bs );
    
	//4. get the shaders filenames from the jfx file so we can read them and send them
	XMLResults xmlResults;
	XMLNode xMainNode=XMLNode::parseString ( theJfx.c_str(), NULL, &xmlResults );
	if (xmlResults.error==eXMLErrorNone)
		//printf ( "****Parsed String\n" );
	{} else {
		printf("Error parsing XML file: %s\nFile:\n%s\n",XMLNode::getError(xmlResults.error),theJfx.c_str());
	}
	XMLNode xNode=xMainNode.getChildNode ( "root" ).getChildNode ( "shaders" );
	std::string vertFilename;
	std::string fragFilename;
	std::string shaderPath=GetPathFromFilenameRegular ( theFX->filename );
	
	vertFilename=shaderPath;
	vertFilename+="/";
	vertFilename+=xNode.getAttribute ( "vertex" );

	fragFilename=shaderPath;
	fragFilename+="/";
	fragFilename+=xNode.getAttribute ( "fragment" );

	printf ( "****Got shader paths:\n%s\n%s\n",vertFilename.c_str(),fragFilename.c_str());
    
	//5. Read the shaders
	std::string theVertexShader=ReadTextFileIntoString(vertFilename);
	std::string theFragmentShader=ReadTextFileIntoString(fragFilename);
    
	printf("theVertexShader:\n %s\n",theVertexShader.c_str());
	printf("theFragmentShader:\n %s\n",theFragmentShader.c_str());

	//6. Send the shaders (filename... shader lenght... shader)
	StringCompressor::Instance()->EncodeString ( GetFilenameNoPath ( vertFilename ).c_str(), GFCNET_MAX_TEXT_LENGHT,bs );
    bs->Write ( ( int ) theVertexShader.length()+1 );
    StringCompressor::Instance()->EncodeString ( theVertexShader.c_str(),theVertexShader.length()+1,bs );
    
	StringCompressor::Instance()->EncodeString ( GetFilenameNoPath ( fragFilename ).c_str(), GFCNET_MAX_TEXT_LENGHT,bs );
	bs->Write ( ( int ) theFragmentShader.length()+1 );
	StringCompressor::Instance()->EncodeString ( theFragmentShader.c_str(),theFragmentShader.length()+1,bs );
}

void unserializeFX ( RakNet::BitStream* bs ) {
    int jfxLen=0, fragmentLen=0, vertexLen=0;
    int jfxFilenameLen=0,fragmentFilenameLen=0,vertexFilenameLen=0;
    std::string filenameComplete;
    char *filenameNoPathBuff, *fragmentFilenameNoPathBuff, *vertexFilenameNoPathBuff;
    //char jfxBuff[GFCNET_MAX_TEXT_LENGHT], fragmentBuff[GFCNET_MAX_TEXT_LENGHT], vertexBuff[GFCNET_MAX_TEXT_LENGHT];
    char *jfxBuff, *fragmentBuff, *vertexBuff;
    //printf ( "Stream bytes: %i\n",bs->GetNumberOfBytesUsed() );

    //READ FROM BITSTREAM
    /************/
    {	//bs->Read ( jfxFilenameLen );
        filenameNoPathBuff=new char[GFCNET_MAX_TEXT_LENGHT];
        StringCompressor::Instance()->DecodeString ( filenameNoPathBuff,GFCNET_MAX_TEXT_LENGHT,bs );

        bs->Read ( jfxLen );
        jfxBuff=new char[jfxLen+1];
        StringCompressor::Instance()->DecodeString ( jfxBuff,jfxLen,bs );
    }
    /*************/
    /************/
    { 	//bs->Read ( vertexFilenameLen );
        vertexFilenameNoPathBuff=new char[GFCNET_MAX_TEXT_LENGHT];
        StringCompressor::Instance()->DecodeString ( vertexFilenameNoPathBuff,GFCNET_MAX_TEXT_LENGHT,bs );

        bs->Read ( vertexLen );

        vertexBuff=new char[vertexLen+1];
        StringCompressor::Instance()->DecodeString ( vertexBuff,vertexLen,bs );
        printf("\nUnserializer : Vertex Shader: %i bytes %s\n",vertexLen,vertexFilenameNoPathBuff/*,vertexBuff*/);
    }
    /*************/

    /************/
    {	//bs->Read ( fragmentFilenameLen );
        fragmentFilenameNoPathBuff=new char[GFCNET_MAX_TEXT_LENGHT];
        StringCompressor::Instance()->DecodeString ( fragmentFilenameNoPathBuff,GFCNET_MAX_TEXT_LENGHT,bs );


        bs->Read ( fragmentLen );
        fragmentBuff=new char[fragmentLen+1];
        StringCompressor::Instance()->DecodeString ( fragmentBuff,fragmentLen,bs );

        printf("\nUnserializer : Fragment Shader: %i bytes %s\n",fragmentLen, fragmentFilenameNoPathBuff/*,fragmentBuff*/);
    }
    /*************/

    //printf("\nDeserialized/Stream %i/%i\n\n",jfxLen+vertexLen+fragmentLen+jfxFilenameLen+vertexFilenameLen+fragmentFilenameLen, bs->GetNumberOfBytesUsed());

    //SAVE TO FILES
    std::ofstream outFile;

    std::string jfxFileName=sett.receivedPath+filenameNoPathBuff;
    std::string vertexFileName=sett.receivedPath+vertexFilenameNoPathBuff;
    std::string fragmentFileName=sett.receivedPath+fragmentFilenameNoPathBuff;

    //write jfxFile
    outFile.open ( jfxFileName.c_str(),std::ostream::out );
    outFile<<jfxBuff;
    outFile.close();

    //write fragmentFile
    outFile.open ( fragmentFileName.c_str(),std::ostream::out );
    outFile<<fragmentBuff;
    outFile.close();

    //write vertexFile
    outFile.open ( vertexFileName.c_str(),std::ostream::out );
    outFile<<vertexBuff;
    outFile.close();

    delete [] fragmentBuff;
    delete [] vertexBuff;
    delete [] jfxBuff;

    //LOAD THE FX FROM THE FILES
    fxManager.loadFX(jfxFileName);
}

std::string gfcChatLogEntry::getFormattedString() {
    
    std::string tmp="( ";
    tmp+=time;
    
    
    switch( type ){
    	case GFCNETMESSAGETYPE_NORMAL:
    		tmp+=") ";
    		tmp+=sender;
    		tmp+=" says: ";
    		tmp+=message;
    		break;
    	
    	case GFCNETMESSAGETYPE_SYSTEM:
    		tmp+=" ";
    		tmp+=message;
    		tmp+=")";
    		break;
    	
    	case GFCNETMESSAGETYPE_LOAD:
    		tmp+=") ";
    		tmp+=sender;
    		tmp+=" loaded ";
    		tmp+=message;
    		break;
    }
    
    return tmp;
}

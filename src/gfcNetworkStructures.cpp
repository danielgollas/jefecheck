#include "gfcNetworkStructures.h"

#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <stdlib.h> // For atoi
#include <cstring> // For strlen
#include <cctype>
#include <filesystem>
#include "gfcStructures.h"
#include "gfcfx.h"
#include "xmlParser.h"
#include <fstream>
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

extern gfcSettings sett;

// =====================================================================
// jefe::wire overloads (JEF-23). Same field sequences as the legacy
// RakNet-serializer versions (deleted in Task 4); see gfcNetworkStructures.h for the sanctioned wire
// deltas (u32-prefixed strings, dropped redundant length fields, LUT
// body as raw length-prefixed bytes).
// =====================================================================

void serializeLUT ( CubeLUT* theLUT, jefe::wire::Writer& w ) {
    //read the lut file (whatever format it is) and send the filename w/o path, and then send the binary file.
    FILE *fp;
    long len;
    char *buf;
    fp=fopen ( theLUT->filename,"rb" );
    if ( !fp ) {
        // Legacy crashed here (unchecked fopen). Emit an empty-but-well-
        // formed payload instead so the stream cannot desync.
        printf ( "serializeLUT: could not open %s\n",theLUT->filename );
        w.writeString ( GetFilenameNoPath ( theLUT->filename ) );
        w.writeBytes ( nullptr, 0 );
        return;
    }
    fseek ( fp,0,SEEK_END ); //go to end
    len=ftell ( fp ); //get position at end (length)
    fseek ( fp,0,SEEK_SET ); //go to beg.
    buf=new char[len]; //malloc buffer
    fread ( buf,len,1,fp ); //read into buffer
    fclose ( fp );

    std::string filenameNoPath=GetFilenameNoPath ( theLUT->filename );

    //write the filename (w/o path), then the file body as raw bytes
    w.writeString ( filenameNoPath );
    w.writeBytes ( ( const unsigned char* ) buf, ( size_t ) len );
    delete [] buf;
}

bool unserializeLUT ( jefe::wire::Reader& r ) {

    std::string filenameNoPath;
    std::vector<unsigned char> fileBytes;

    //READ FROM WIRE — all fields before any side effect
    if ( !r.readString ( filenameNoPath ) ) return false;
    if ( !r.readBytes ( fileBytes ) ) return false;

    std::ofstream outFile;

    // SECURITY (JEF-28): the filename is peer-supplied. Reduce it to its bare
    // basename so a hostile peer can't escape receivedPath via "../.." or an
    // absolute path. filename() strips all directory + ".." components:
    // "../../evil" -> "evil", "/etc/passwd" -> "passwd". Skip empty results
    // (e.g. a name that was only a directory) — never write.
    std::string safeName=std::filesystem::path(filenameNoPath).filename().string();
    if ( safeName.empty() ) {
        printf ( "unserializeLUT: rejected empty/invalid filename '%s'\n",filenameNoPath.c_str() );
        return false;
    }

    std::string lutFilename=sett.receivedPath+safeName;

    //write lut file
    outFile.open ( lutFilename.c_str(),std::ostream::binary );
    outFile.write ( ( const char* ) fileBytes.data(), ( std::streamsize ) fileBytes.size() );
    outFile.close();

    lutManager.loadLUT(lutFilename);
    return true;
}

void serializeFX ( const gfcFX* theFX, jefe::wire::Writer& w ) {

    //1. read the whole jfx
    std::string theJfx=ReadTextFileIntoString(theFX->filename);
    //2. write the jfx filename, then the jfx body (writeString carries the length)
    w.writeString ( GetFilenameNoPath ( theFX->filename ) );
    w.writeString ( theJfx );

    //3. get the shaders filenames from the jfx file so we can read them and send them
    XMLResults xmlResults;
    XMLNode xMainNode=XMLNode::parseString ( theJfx.c_str(), NULL, &xmlResults );
    if (xmlResults.error!=eXMLErrorNone) {
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

    //4. Read the shaders
    std::string theVertexShader=ReadTextFileIntoString(vertFilename);
    std::string theFragmentShader=ReadTextFileIntoString(fragFilename);

    //5. Send the shaders (filename then shader body, same order as legacy)
    w.writeString ( GetFilenameNoPath ( vertFilename ) );
    w.writeString ( theVertexShader );

    w.writeString ( GetFilenameNoPath ( fragFilename ) );
    w.writeString ( theFragmentShader );
}

bool unserializeFX ( jefe::wire::Reader& r ) {
    std::string jfxFilenameNoPath, theJfx;
    std::string vertexFilenameNoPath, theVertexShader;
    std::string fragmentFilenameNoPath, theFragmentShader;

    //READ FROM WIRE — all fields before any side effect
    if ( !r.readString ( jfxFilenameNoPath ) ) return false;
    if ( !r.readString ( theJfx ) ) return false;
    if ( !r.readString ( vertexFilenameNoPath ) ) return false;
    if ( !r.readString ( theVertexShader ) ) return false;
    if ( !r.readString ( fragmentFilenameNoPath ) ) return false;
    if ( !r.readString ( theFragmentShader ) ) return false;

    printf("\nUnserializer : Vertex Shader: %i bytes %s\n",(int)theVertexShader.length(),vertexFilenameNoPath.c_str());
    printf("\nUnserializer : Fragment Shader: %i bytes %s\n",(int)theFragmentShader.length(),fragmentFilenameNoPath.c_str());

    //SAVE TO FILES
    std::ofstream outFile;

    // SECURITY (JEF-28): every filename here is peer-supplied. Reduce each to
    // its bare basename (filename() strips directories + ".." components) so a
    // hostile peer can't write outside receivedPath. Skip the whole FX if any
    // component sanitizes to empty — a partial write can't be loaded anyway.
    std::string jfxSafe=std::filesystem::path(jfxFilenameNoPath).filename().string();
    std::string vertexSafe=std::filesystem::path(vertexFilenameNoPath).filename().string();
    std::string fragmentSafe=std::filesystem::path(fragmentFilenameNoPath).filename().string();
    if ( jfxSafe.empty() || vertexSafe.empty() || fragmentSafe.empty() ) {
        printf ( "unserializeFX: rejected empty/invalid filename (jfx='%s' vert='%s' frag='%s')\n",
                 jfxFilenameNoPath.c_str(), vertexFilenameNoPath.c_str(), fragmentFilenameNoPath.c_str() );
        return false;
    }

    std::string jfxFileName=sett.receivedPath+jfxSafe;
    std::string vertexFileName=sett.receivedPath+vertexSafe;
    std::string fragmentFileName=sett.receivedPath+fragmentSafe;

    //write jfxFile
    outFile.open ( jfxFileName.c_str(),std::ostream::out );
    outFile<<theJfx;
    outFile.close();

    //write fragmentFile
    outFile.open ( fragmentFileName.c_str(),std::ostream::out );
    outFile<<theFragmentShader;
    outFile.close();

    //write vertexFile
    outFile.open ( vertexFileName.c_str(),std::ostream::out );
    outFile<<theVertexShader;
    outFile.close();

    //LOAD THE FX FROM THE FILES
    fxManager.loadFX(jfxFileName);
    return true;
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

std::string shortTime(const std::string& asctimeStr) {
    // Find the first "NN:NN" run and return those 5 chars.
    for (size_t i = 0; i + 4 < asctimeStr.size(); ++i) {
        if (std::isdigit((unsigned char)asctimeStr[i]) &&
            std::isdigit((unsigned char)asctimeStr[i + 1]) &&
            asctimeStr[i + 2] == ':' &&
            std::isdigit((unsigned char)asctimeStr[i + 3]) &&
            std::isdigit((unsigned char)asctimeStr[i + 4])) {
            return asctimeStr.substr(i, 5);
        }
    }
    return asctimeStr;   // fallback: unrecognized format
}

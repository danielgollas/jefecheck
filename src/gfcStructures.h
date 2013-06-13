#ifndef GFCSTRUCTURES_H
#define GFCSTRUCTURES_H

#include <string>
#include <vector>
#include <queue>
#include <FL/filename.H>
#include <FL/Fl_Preferences.H>
#include "glew.h"
#include <FL/gl.h>
#include "UIConstants.h"
#include "xmlParser.h"

#include "gfcrenderparams.h"
#define JEFE_VERSION "1.5.3" 
#include <sstream> //for stingstream
#include <boost/filesystem/path.hpp>

class gfcPlaylistItemProgramState;

//#include "gfcfxstack.h"
#pragma warning( disable: 4275 4305 4244 )
#include <botan/botan.h>
#include <botan/look_pk.h>
#include <botan/dsa.h>

/*#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif*/

#ifdef WIN32
#include <Iphlpapi.h> //for macAddress in windows
//#include <Winsock2.h> //for hostname in windows
#include <windows.h> //for HightPerformanceCounter structures
#endif

//

#define DEBUG

#ifdef  DEBUG
#define glPrintError()        for(GLuint err = glGetError(); err; err = glGetError()) { fprintf(stderr, "glError: %s caught at %s:%u\n", (char*)gluErrorString(err), __FILE__, __LINE__); }

#define glCheckFBO(errors)                                                                                                                                                                          \
                {  \
                        glPrintError(); \
                        GLuint err = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT); \
                        if(0x8cd5 != err)\
                        {\
                        	errors=1;\
                                switch(err) \
                                { \
                                        case 0x8cd6: fprintf(stderr, "glCheck: GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT caught at %s:%u\n",__FILE__, __LINE__); break; \
                                        case 0x8cd7: fprintf(stderr, "glCheck: GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT caught at %s:%u\n",__FILE__, __LINE__); break;\
                                        case 0x8cd9: fprintf(stderr, "glCheck: GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT caught at %s:%u\n",__FILE__, __LINE__); break;\
                                        case 0x8cda: fprintf(stderr, "glCheck: GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT caught at %s:%u\n", __FILE__, __LINE__); break; \
                                        case 0x8cdb: fprintf(stderr, "glCheck: GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT caught at %s:%u\n",__FILE__, __LINE__); break;\
                                        case 0x8cdc: fprintf(stderr, "glCheck: GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT caught at %s:%u\n",__FILE__, __LINE__); break;\
                                        case 0x8cdd: fprintf(stderr, "glCheck: GL_FRAMEBUFFER_UNSUPPORTED_EXT caught at %s:%u\n",__FILE__, __LINE__); break;\
                                        case 0x0506: fprintf(stderr, "glCheck: GL_INVALID_FRAMEBUFFER_OPERATION_EXT caught at %s:%u\n",__FILE__, __LINE__); break;\
                                }  \
                        } \
                }
#else
#define glError()
#define        glCheckFBO()
#endif

#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"
#include "boost/filesystem/convenience.hpp"
#define FILE_EXISTS(name) boost::filesystem::exists(boost::filesystem::path(name))
#define GFC_MAX_FAVORITE_STACKS 5
#define GFC_MAX_FAVORITE_COLOR_CORRECTION_STACKS 5



enum GFC_FX_MENU_CONTROLS{FX_MENU_CLOSE=1,FX_MENU_SAVE_STACK, FX_MENU_LOAD_STACK, FX_MENU_CLEAR_ALL,FX_MENU_MANAGER,FX_MENU_LUT_MANAGER,FX_MENU_RECENT, FX_MENU_LOAD_FAVORITES_0,FX_MENU_LOAD_FAVORITES_1,FX_MENU_LOAD_FAVORITES_2,FX_MENU_LOAD_FAVORITES_3,FX_MENU_LOAD_FAVORITES_4, 
FX_MENU_SAVE_FAVORITES_0,FX_MENU_SAVE_FAVORITES_1,FX_MENU_SAVE_FAVORITES_2,FX_MENU_SAVE_FAVORITES_3,FX_MENU_SAVE_FAVORITES_4,
FX_MENU_APPEND_FAVORITES_0,FX_MENU_APPEND_FAVORITES_1,FX_MENU_APPEND_FAVORITES_2,FX_MENU_APPEND_FAVORITES_3,FX_MENU_APPEND_FAVORITES_4};

enum FXTYPES{JEFECHECKFX=0,JEFECHECKCM, JEFECHECKCOMPOSITINGFX};

enum GFC_PL_MENU_CONTROLS{PL_MENU_CLOSE=1, PL_MENU_RECENT,PL_MENU_LOAD_PLAYLIST, PL_MENU_SAVE_PLAYLIST,PL_MENU_CLEAR_ALL,PL_MENU_LOAD_WINDOW,PL_MENU_SCALE_OVERRIDE, PL_MENU_SHOW_FULL_PATHS,PL_MENU_SHOW_COMPACT_VIEW};
enum GFC_PL_GUI_TYPE{PL_GUI_UNKNOWN=0, PL_GUI_LOAD,PL_GUI_DELETE,PL_GUI_MOVEUP, PL_GUI_MOVEDOWN};

void setMacExecutablePath(std::string thePath);
std::string asciiTime ( bool noDate );
std::string GetPathFromFilename( const std::string& filename );
std::string GetPathFromFilenameRegular( const std::string& filename );
std::string GetFilenameNoPath( const std::string& filename );
std::string GetFilenameNoFilePrefix ( const std::string& filename );
int removeFile(std::string theFilename);
std::string RemoveNewLine( const std::string& filename );
std::string getApplicationDataPath();
std::string ReadTextFileIntoString(std::string filename);
std::string findFileInSearchPaths(std::string theFile);
std::string getFirstSequenceInDirectory(std::string thePath);
bool fileExists (const std::string &name );

gfcPlaylistItemProgramState getCurrentProgramState();
void setCurrentProgramState(gfcPlaylistItemProgramState state);

std::vector<std::string> GetFilenamesFromPastedText(const std::string& str);
std::vector<std::string> TokenizeString(const std::string& str, const std::string& delimiters = " ");

std::string getOS();
std::vector<std::string> getMACAddress();
std::string getHostname();

std::string stripVersion(std::string fullVersion);
float maximumVersionForThisLicense(float theVersion);

std::string getSHA1(std::string message);
Botan::SecureVector<Botan::byte> b64_decode(const std::string& in);
std::string hex_encode(const std::string& in);
Botan::SecureVector<Botan::byte> hex_decode(const std::string& in);

int checkValidEmail(std::string email);

std::string upperCase(std::string s);
std::string lowerCase(std::string s);

std::string AppendExtensionToFilename(std::string filename, std::string ext);

std::string CreateRenderFilename ( gfcRenderParams params );

std::string GetExtension(const std::string& filename);

std::string GetMD5Hash(std::string theString);

void ReplaceWindowsBackslash(std::string &filename);
void ReplaceWindowsSlash(std::string &filename);

void AddMenuSlash(std::string &path); //fltk menus convert pathnames with slashes into submenu hierarchies, adding a / at the beginning prevents this.
void RemoveMenuSlash(std::string &path);

int confirmQuit();

void UpdateRecentBrowsedButtons(int alreadyReplacedSlashes=0);
void UpdateRecentIPsButtons();
void refreshSearchPathsBrowser();

struct pixelRGB16Bit{
  short r;
  short g;
  short b;
};

struct pixelRGB8Bit{
  unsigned char r;
  unsigned char  g;
  unsigned char  b;
};

struct pixelRGBA16Bit{
  short r;
  short g;
  short b;
  short a;
};

struct pixelRGBA8Bit{
  unsigned char r;
  unsigned char g;
  unsigned char b;
  unsigned char a;
};

class Rectang{

	public:
		Rectang(){x=y=w=h=0;};
		Rectang(int px, int py, int pw, int ph):x(px),y(py),w(pw),h(ph){};
		void 	set(int X, int Y, int W, int H)
		{
			x=X;
			y=Y;
			w=W;
			h=H;
		}
		
		int x;
		int y;
		int w;
		int h;
	
};

class gfcLogToLinParams
{
public:
 bool active;
 int whiteRef;
 int blackRef;
 float displayGamma;
 
};

class gfcTimer
{
private:
	long long startTime;
	long long elapsed;
	int _initialized;
	long long _getCurrentTime();

	double PCFreq;


public:
	//gfcTimer();
	gfcTimer(std::string name="timer");
	void update();
	void initialize();
	void print();
	void start();
	void stop();
	void reset(); //stops and restarts
	long getElapsed(bool update=false);
	double getElapsedSecs(bool update=false);
	std::string name;
}; 

class gfcSettings
{
public:
    gfcSettings()
    {
        startFullscreen=false;
        playbackOnLoad=false;
        bgColor=0.149019;
        bool enableCrashRecoverySession=true;
        defaultBrowsePath=getApplicationDataPath();
        framingMode=FRAMINGSINGLE_ID;
        loopMode=LOOPMODEONCE_ID;
        openLoadWindowAtStartup=true;
        numOfPartitions=1;
        loopPriority=1;
        forcePBO=0;
        for(int i=0;i<16;i++)
        {
	    char tmpPath[600];
	    sprintf(tmpPath,"~/tmp%i",i+1);
            mirrorPaths[i]=tmpPath;
        }

		exrIgnoreDisplayWindow=0;
		exrIgnoreHeadersAspectRatio=0;
		exrExposure=0;
		exrDefog=0;
		exrGamma=2.2;
		exrKneeLow=0;
		exrKneeHigh=5;

        renderingEngine=0; //0==3D, 1==2D
        chatFadeDelay=20;
        chatFontSize=20;
        chatOpacity=0.5;
        chatAutoFade=true;
        chatTextBG=true;
        chatDisplayLines=8;
        remotePointerFadeDelay=2;
        maxRecentFXStacks=5;
		maxRecentFXs=5;
        maxRecentSessions=5;
        maxRecentBrowsed=10;
        maxRecentIPs=5;
        maximumFramesInQueue=5;
        loopMode=0;
        vsync=1;
        searchPathsRecursive=false;
        useSearchPaths=false;
		


		balanceReads=1;

		sendRemoteLoadRequests=false;
		autoAcceptRemoteLoadRequests=false;
		
		playlistShowCompactView=1;
		playlistShowFullPaths=0;
		
		aspectBarsOpacity=1.0;

		processorPriority=1.0;

		remotePointerColor=fl_rgb_color(128,128,128);

		feedbackMessageFadeDelay=2.0;
		feedbackMessageOn=1;
		feedbackMessageSize=12;
    
	}
    int startFullscreen;
    int playbackOnLoad;
    float bgColor;
    int enableCrashRecoverySession;
    std::string defaultBrowsePath;
	std::string defaultBrowsePathBackup;
    int framingMode; ///1, 2, 3(vertical pair) or 4
    int openLoadWindowAtStartup;
    int loopMode; ///1 once, 2 loop, 3 bounce;
    int vsync; //1 on, 0 off.
    int maximumFramesInQueue; //determines if the raw queue should limit how many frames are loaded before they are converted to textures, 0 means no limiting.
    int numOfPartitions; ///how many mirrors will be used to strip and load images
    int balanceReads; //if this is on, then all tracks share a mutex for read operations. 
	std::string mirrorPaths[16]; ///the paths to the mirrors for parallel loading, up to 16
    
    std::vector<std::string> searchPaths;
    bool searchPathsRecursive;
    bool useSearchPaths;

    int loopPriority; //0 == give loop priority to minimum length track, 1 means to the maximum length track.
    int filterMin; //filtering modes for minificaion and maxification
    int filterMax;
    bool textureRectangles;
    bool textureCompression;
	float aspectBarsOpacity;
	int processorPriority;
    bool glsl;
    bool fbo;
    int fp16;
    int forcePBO;
    int renderingEngine;
    int renderingEngineFlag; //since the renderingEngine can't be changed on the fly, it has to be changed on application start. this flag is copied to the renderingEngine on start, and it is modified when the program starts in the initial settings read.
    std::string lutPath; //path where the standard luts are to be found and loaded at the begining.
    std::string receivedPath;
    std::vector<bool> lutAutoload;
    std::vector<bool> fxAutoload;

	int exrIgnoreDisplayWindow;
	int exrIgnoreHeadersAspectRatio;
	int exrEnableExposureTransformOnLoad;
	float exrExposure;
	float exrDefog;
	float exrGamma;
	float exrKneeLow;
	float exrKneeHigh;

    int maxRecentFXStacks;
	int maxRecentFXs;
    int maxRecentSessions;
    int maxRecentBrowsed;
    int maxRecentIPs;

    std::vector<std::string> recentFXStacks;
    std::vector<std::string> recentFXs;
    std::vector<std::string> recentSessions;
    std::vector<std::string> recentBrowsed;
    std::vector<std::string> recentIPs;
    
	std::string defaultLUTName;
	std::string defaultLUTNameBackup; //stores the lutname read from the settings or set in the GUI, which can be overriden by the JEFECHECK_DEFAULT_LUT env var. Always save this one to save users preferences. 
    void addToRecentFXs(std::string pname);
    
 
	//playlistWindow options
	int playlistShowCompactView;
	int playlistShowFullPaths;

	//feedback Message options
	float feedbackMessageFadeDelay;
	int feedbackMessageOn;
	int feedbackMessageSize;

    //Online options
    float chatFadeDelay;
    int chatAutoFade;
    int chatTextBG;
    int chatFontSize;
    float chatOpacity;
    int chatDisplayLines;
    float remotePointerFadeDelay;
    std::string nickName;
    std::string serverNickname;
    int clientPort;
    int serverPort;
	int sendRemoteLoadRequests;
	int autoAcceptRemoteLoadRequests;
	int remotePointerColor;
	std::string licensePath;
	
};

std::string ftos(float value,int decimals=5);

template <class T> std::string toString(T value);
template <class T> 
std::string toString(T value)
{
	std::stringstream ss;
	ss<<value;
	return ss.str();
}
template <class T> void saveSetting(std::string name,T value, XMLNode &node);
template <class T> void saveSetting(std::string name,T value, XMLNode &node) {
    std::stringstream ss;
    ss<<value;
    //cout << "Saving setting "<<name<<"="<<ss.str() << " (from "<< value <<")\n";
	
    node.addAttribute(name.c_str(),ss.str().c_str());
}

void saveSettingString(std::string name, std::string& result, XMLNode &node);
template <class T> void readSetting(std::string name, T& result, XMLNode &node);
template <class T>
void readSetting(std::string name, T& result, XMLNode &node) {
    if (node.getAttribute ( name.c_str() )!=NULL) {
        std::stringstream ss;
        ss << node.getAttribute ( name.c_str() );
        //std::cout << name <<": " <<ss.str() << std::endl;
        {
            ss>>result;
        }
		
    }
}

void readSettingString(std::string name, std::string& result, XMLNode &node);
template <class T> void setWidgetFromNode(std::string name, T* widget, XMLNode node);
template <class T>
void setWidgetFromNode(std::string name, T* widget, XMLNode node) {
    if (node.getAttribute ( name.c_str() )!=NULL) {
        widget->value(atof(node.getAttribute( name.c_str() )));
		
    }
}
template <class T> T readAttributeFromNode(std::string name,XMLNode &node, T defaultValue);
template <class T>
T readAttributeFromNode(std::string name, XMLNode &node, T defaultValue)
{
	std::stringstream ss;
	if (node.getAttribute(name.c_str())!=NULL)
	{
		ss << node.getAttribute(name.c_str());
	}
	
	
	if(ss.str().empty())
	{	
		return defaultValue;
	}
	else
	{
		T returnValue;
		ss >> returnValue;
		return returnValue;
	}
}

void readSettings(gfcSettings &sett);
void saveSettings(const gfcSettings *sett);

void loadLUTsFromPath(std::string path);

void loadFXsFromPath(std::string path);

int getFreeRam(); //returns free ram in MB


struct globals
{
        bool npotTextures;
};


int getNextDivisibleBy4(int value);

int getNextPOT(int value);

void
swabUInt16(unsigned short* wp);

void
swabUInt32(unsigned int* lp);

void
swabArrayOfUInt16(unsigned short* wp, register size_t n);

void
swabArrayOfUInt32(register unsigned int* lp, register size_t n);

void
swabFloat(float *fp);

void
swabArrayOfFloat(float *fp, size_t n);

void
swabDouble(double *dp);

void
swabArrayOfDouble(double* dp, register size_t n);

#define MSBImportOctets(scanline,packed_u32) \
{ \
  packed_u32.octets[0]=*scanline++; \
  packed_u32.octets[1]=*scanline++; \
  packed_u32.octets[2]=*scanline++; \
  packed_u32.octets[3]=*scanline++; \
}

#define LSBImportOctets(scanline,packed_u32) \
{ \
  packed_u32.octets[3]=*scanline++; \
  packed_u32.octets[2]=*scanline++; \
  packed_u32.octets[1]=*scanline++; \
  packed_u32.octets[0]=*scanline++; \
}

#if defined(WORDS_BIGENDIAN)
#define LSBOctetsToPackedU32Word(scanline,packed_u32) \
{ \
  LSBImportOctets(scanline,packed_u32); \
}
#define MSBOctetsToPackedU32Word(scanline,packed_u32) \
{ \
  MSBImportOctets(scanline,packed_u32); \
}
#else
#define LSBOctetsToPackedU32Word(scanline,packed_u32) \
{ \
  MSBImportOctets(scanline,packed_u32); \
}
#define MSBOctetsToPackedU32Word(scanline,packed_u32) \
{ \
  LSBImportOctets(scanline,packed_u32); \
}
#endif

typedef unsigned int U32;
typedef unsigned int sample_t ;
typedef union _PackedU32Word {
    U32 word;
    unsigned char octets[4];
} PackedU32Word;

/*
  WordStreamLSBRead support
*/
typedef struct _ReadWordU32State {
    const unsigned char *words;
}
ReadWordU32State;

//static unsigned long ReadWordU32BE (void *state);

//static unsigned long ReadWordU32LE (void *state);




 static const unsigned int BitAndMasks[32] =
    {
      /*
        Same as (~(~0 << retrieve_bits))
      */
      0x00000000U, 0x00000001U, 0x00000003U, 0x00000007U, 0x0000000fU,
      0x0000001fU, 0x0000003fU, 0x0000007fU, 0x000000ffU, 0x000001ffU,
      0x000003ffU, 0x000007ffU, 0x00000fffU, 0x00001fffU, 0x00003fffU,
      0x00007fffU, 0x0000ffffU, 0x0001ffffU, 0x0003ffffU, 0x0007ffffU,
      0x000fffffU, 0x001fffffU, 0x003fffffU, 0x007fffffU, 0x00ffffffU,
      0x01ffffffU, 0x03ffffffU, 0x07ffffffU, 0x0fffffffU, 0x1fffffffU,
      0x3fffffffU, 0x7fffffffU
    };

  /*
    Bit stream reader "handle"
  */
  typedef struct _BitStreamReadHandle
  {
    const unsigned char  *bytes;
    unsigned int          bits_remaining;
  } BitStreamReadHandle;

  /*
    Initialize Bit Stream for reading
  */
  static inline void BitStreamInitializeRead(BitStreamReadHandle *bit_stream,
                                             const unsigned char *bytes)
  {
    bit_stream->bytes          = bytes;
    bit_stream->bits_remaining = 8;
  }

  /*
    Return the requested number of bits from the current position in a
    bit stream. Stream is read in most-significant bit/byte "big endian"
    order.

    bit_stream      - already initialized bit stream.
    requested_bits  - number of bits to read
  */
  static inline unsigned int BitStreamMSBRead(BitStreamReadHandle *bit_stream,
                                              const unsigned int requested_bits)
  {
    register unsigned int
      remaining_quantum_bits,
      quantum;

    remaining_quantum_bits = requested_bits;
    quantum = 0;

    while (remaining_quantum_bits > 0)
      {
        register unsigned int
          octet_bits;

        octet_bits = remaining_quantum_bits;
        if (octet_bits > bit_stream->bits_remaining)
          octet_bits = bit_stream->bits_remaining;

        remaining_quantum_bits -= octet_bits;
        bit_stream->bits_remaining -= octet_bits;

        quantum = (quantum << octet_bits) |
          ((*bit_stream->bytes >> (bit_stream->bits_remaining))
           & BitAndMasks[octet_bits]);

        if (bit_stream->bits_remaining == 0)
          {
            bit_stream->bytes++;
            bit_stream->bits_remaining=8;
          }
      }
    return quantum;
  }


 /*
    Word reading function.

    read_func_state  - state to pass to word reading function.
  */
  typedef unsigned long (*WordStreamReadFunc) (void *read_func_state);
  
  /*
    Word stream word reader "handle"
  */
  typedef struct _WordStreamReadHandle
  {
    unsigned int      word;
    unsigned int         bits_remaining;
    WordStreamReadFunc   read_func;
    void                *read_func_state;
  } WordStreamReadHandle;
  
  /*
    Initialize Word Stream for reading

    word_stream     - stream to initialize.
    read_func_state - state to pass to read_func.
    read_func       - function to retrieve the next word.
  */
  static inline void WordStreamInitializeRead(WordStreamReadHandle *word_stream,
                                              void *read_func_state,
                                              WordStreamReadFunc read_func)
  {
    word_stream->word            = 0;
    word_stream->bits_remaining  = 0;
    word_stream->read_func       = read_func;
    word_stream->read_func_state = read_func_state;
  }
  
  /*
    Return the requested number of bits from the current position in a
    32-bit word stream. Stream is read starting with the least significant
    bits of the word.

    word_stream     - an initialized word reader stream.
    requested_bits  - number of bits to retrieve from the stream.
  */
  static inline unsigned int WordStreamLSBRead(WordStreamReadHandle *word_stream,
                                               const unsigned int requested_bits)
  {
    register unsigned int
      remaining_quantum_bits,
      quantum;
    
    remaining_quantum_bits = requested_bits;
    quantum = 0;
    
    while (remaining_quantum_bits > 0)
      {
        register unsigned int
          word_bits;
        
        if (word_stream->bits_remaining == 0)
          {
            word_stream->word=word_stream->read_func(word_stream->read_func_state);
            word_stream->bits_remaining=32;
          }
        
        word_bits = remaining_quantum_bits;
        if (word_bits > word_stream->bits_remaining)
          word_bits = word_stream->bits_remaining;
        
        quantum |= (((word_stream->word >> (32-word_stream->bits_remaining))
                     & BitAndMasks[word_bits]) << (requested_bits-remaining_quantum_bits));
        
        remaining_quantum_bits -= word_bits;
        word_stream->bits_remaining -= word_bits;
      }
    return quantum;
  }



static unsigned long ReadWordU32BE (void *state)
{
  unsigned int value;
  ReadWordU32State *read_state=(ReadWordU32State *) state;
  value =  *read_state->words++ << 24;
  value |= *read_state->words++ << 16;
  value |= *read_state->words++ << 8;
  value |= *read_state->words++;
  return value;
}

static unsigned long ReadWordU32LE (void *state)
{
  unsigned int value;
  ReadWordU32State *read_state=(ReadWordU32State *) state;
  value = *read_state->words++;
  value |= *read_state->words++ << 8;
  value |= *read_state->words++ << 16;
  value |=  *read_state->words++ << 24;
  return value;
}

#endif

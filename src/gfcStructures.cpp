#include "gfcStructures.h"
#include "ui/IApplication.h"
namespace { jefe::ui::IApplication& app() { return jefe::ui::IApplication::instance(); } }
#include "UIConstants.h"
#include "gfcfx.h"
#include "gfcfxstack.h"
#include "trilerp.h"
#include <vector>
#include <cstdint>
#include <fstream>
#include <sstream> //for stingstream
#include "xmlParser.h"


#include <cstdlib> //for getenv
#include <iostream>


#include <sys/stat.h> //for stat

#include <cctype> // for toupper
#include <string>
#include <algorithm>


//STUFF TO GET THE MAC ADDRESS on linux
#ifdef linux
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#endif

//STUFF TO GET THE MAC ADDRESS on OSX
#ifdef __APPLE__
#include "osxGetPrimaryMacAddress.h"
#endif

#ifdef WIN32
#include <WinSock2.h>
#include <windows.h>
#else
#include <sys/time.h>
#endif



extern bool npotTextures;
//extern std::vector<CubeLUT> lutArray;
//extern std::vector<gfcFX> fxArray;
//extern std::map<std::string, int> fxHashMap;

std::string gMacExecutablePath="";

#include "gfctrackmanager.h"
extern gfcTrackManager trackManager;

#include "gfcplaybackmanager.h"
extern gfcPlaybackManager playbackManager;

#include "gfcfxmanager.h"
extern gfcFXManager fxManager;

#include "gfclutmanager.h"
extern gfcLUTManager lutManager;

#include "gfcmemorymanager.h"
extern gfcMemoryManager memoryManager;

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

#include "gfcsessionmanager.h"
extern gfcSessionManager sessionManager;

#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;

void UpdateRecentIPsButtons();

#include <time.h>
std::string asciiTime ( bool noDate ) {
    time_t rawtime;
    struct tm * timeinfo;
	
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );
    char returnValue[80];
    strcpy ( returnValue,asctime ( timeinfo ) );
    returnValue[strlen ( returnValue )-1]='\0';
	
    if ( noDate ) {
        strcpy ( returnValue,strstr ( returnValue," " ) +1 );
        //strcpy(returnValue,strstr(returnValue," ")+1);
        //strcpy(returnValue,strstr(returnValue," ")+1);
    }
    return returnValue;
}

gfcTimer::gfcTimer(std::string pname) {
    name=pname;
	_initialized=0;
}

long long gfcTimer::_getCurrentTime(){
	//return current time (either wall or process in ms)
	#ifdef WIN32
		LARGE_INTEGER li;
		QueryPerformanceCounter(&li);
		return li.QuadPart/PCFreq;
	#else
		struct timeval time;
		gettimeofday(&time, NULL);
		return (time.tv_usec+time.tv_sec*1000000)/PCFreq;
	#endif
}

void gfcTimer::initialize(){
	#ifdef WIN32
		LARGE_INTEGER li;
		if(!QueryPerformanceFrequency(&li))
			printf("QueryPerformanceFrequency failed!\n");
		PCFreq = double(li.QuadPart)/1000.0;
	#else
		PCFreq = 1000; //usecs per msec
		printf("No timer\n");
	#endif


	_initialized=1;
}

void gfcTimer::start() {
	//OK, all of this will have to be remade for specific OSs so we can get rid of the glut dependencies. 
	//On WIN32 we should use QueryPerformanceCounters
	//On *nix we should use gettimeofday, apparently it is accurate to the usecond, even though it might drift if the system time changes

	if(!_initialized){
		initialize();
	}
	
	startTime=_getCurrentTime();
	
//    startTime=glutGet(GLUT_ELAPSED_TIME);
}

void gfcTimer::stop() {
    //glFinish();
	update();
//    elapsed=glutGet(GLUT_ELAPSED_TIME)-startTime;
}

void gfcTimer::reset() {
    stop();
    start();
}

long gfcTimer::getElapsed(bool update_) {
	if (update_)
	{
		update();
	}
    return elapsed;
}

void gfcTimer::update() {
	elapsed = _getCurrentTime()-startTime;
}

double gfcTimer::getElapsedSecs(bool update_) {
	if (update_)
	{
		update();
	}
    return (double)elapsed/1000.0;
}

void gfcTimer::print() {
	update();
	double tmpSecs=elapsed*(1.0/(double)1000.0);
    printf("%s timer: %ims (%.3f s)\n",name.c_str(),elapsed,tmpSecs);
	printf("%.3fs\n",name.c_str(),elapsed,tmpSecs);
}


int confirmQuit()
{
	std::string message="Do you really want to quit?";

	int answer=1;
	switch ( answer ) {
		case 0:
			return 0;
			break;
		case 1:
			return 1;
			break;
	}
	return 0;
}

bool dirExists(const std::string &name){
	return std::filesystem::is_directory(name);
}

bool fileExists (const std::string &name ) {
    
	
	//return std::filesystem::exists(std::filesystem::path(name));
	
	//OLD METHOD, SEEMS SLOWER
	static struct stat aBuffer;
    static int i;
	i = stat ( name.c_str(), &aBuffer );
	return !i;
    
}


gfcPlaylistItemProgramState getCurrentProgramState(){
	//this function interacts with some of the managers and gets the current program state, not including the load window state.
	
	gfcPlaylistItemProgramState tmp;
	tmp.layout=plateManager.getFramingMode();
	tmp.playbackInfo=playbackManager.getPlaybackInfo();
	tmp.plateStateInfo=plateManager.getPlateStateInfo();
	tmp.trackStateInfo=trackManager.getTrackStateInfo();
	
	return tmp;
	
}

void setCurrentProgramState(gfcPlaylistItemProgramState state)
{
	plateManager.setFramingMode(state.layout);
	playbackManager.setPlaybackInfo(state.playbackInfo);
	plateManager.setPlateStateInfo(state.plateStateInfo);
	trackManager.setTrackStateInfo(state.trackStateInfo);
}

void setMacExecutablePath(std::string thePath)
{
	gMacExecutablePath=thePath;
}

std::string getApplicationDataPath() {

#ifdef linux
    // Tilde-expansion via $HOME, replacing the previous fl_filename_expand.
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::string(home) + "/.JefeCorp/JefeCheck/JefeCheck/";
    }
    return "./.JefeCorp/JefeCheck/JefeCheck/";
#endif

#ifdef WIN32
    // %APPDATA% is the user's Roaming folder — same effective location
    // Fl_Preferences::getUserdataPath returned. SHGetFolderPath would be
    // more rigorous but the existing FLTK call wasn't either.
    const char* appdata = std::getenv("APPDATA");
    if (appdata && *appdata) {
        return std::string(appdata) + "\\JefeCorp\\JefeCheck\\JefeCheck\\";
    }
    return ".\\JefeCorp\\JefeCheck\\JefeCheck\\";
#endif
	
#ifdef __APPLE__

	// Prefer the actual loaded bundle's Resources path when running as a
	// .app — robust against symlinks and chdir, and the canonical Cocoa
	// path. Falls through to the argv[0]-derived path for raw-binary
	// dev builds (USE_QT=OFF without bundling, or running the binary
	// directly out of build_qt/jefecheck.app/Contents/MacOS/).
	{
		extern std::string getMacBundleResourcePath();
		std::string bundlePath = getMacBundleResourcePath();
		if (!bundlePath.empty()) return bundlePath;
	}

	std::filesystem::path tmpPath(gMacExecutablePath);
	tmpPath=tmpPath.parent_path().parent_path()/"Resources";
	return tmpPath.string()+"/";
	/*
#ifdef DEMO_VERSION
	 return "/Applications/JefeCheck_Demo.app/Contents/Resources/";
#else
	 return "/Applications/JefeCheck.app/Contents/Resources/";
#endif
	 
	 //char tmpPath[32000];
	 //fl_filename_expand ( tmpPath,"~/Library/Preferences/JefeCorp/CheckMate/CheckMate_1_5/" );
	 //return tmpPath;*/
	 
#endif
}



std::string ftos(float value,int precision)
{
	std::stringstream ss;
	ss.setf(std::ios::fixed,std::ios::floatfield);
	ss.precision(precision);
	ss<<value;
	return ss.str();
}





template void saveSetting<char>(std::string name, char, XMLNode&);

void saveSettings ( const gfcSettings *sett ) {
}







void readSettingString(std::string name, std::string& result, XMLNode &node) {
    if (node.getAttribute ( name.c_str() )!=NULL) {
        result=node.getAttribute ( name.c_str() );
        //std::cout << name <<": " <<result << std::endl;
    }
}


std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
	std::stringstream ss(s);
	std::string item;
	while(std::getline(ss, item, delim)) {
		elems.push_back(item);
	}
	return elems;
}


std::vector<std::string> split(const std::string &s, char delim) {
	std::vector<std::string> elems;
	return split(s, delim, elems);
}

void loadLUTsFromPath(std::string thePath) {
}

void loadFXsFromPath(std::string thePath) {
}

void gfcSettings::addToRecentFXs(std::string pname)
{
	//check if it is already in there
	std::vector<std::string>::iterator found=std::find(recentFXs.begin(),recentFXs.end(),pname);
	if ( found!=recentFXs.end())
	{
		//the FX is already in here, remove it and push it to the top
		std::string tmp = *found;
		recentFXs.erase(found);
		recentFXs.push_back(tmp);
	}
	else
	{	//not in there, max limit reached?
		const size_t kMaxRecentFXs = 5; // matches the old gfcSettings::maxRecentFXs default
		if (recentFXs.size()<kMaxRecentFXs)
		{
			//not in limit, just add it.
			recentFXs.push_back(pname);
		}
		else
		{
			//erase the the oldest, push back new one.
			recentFXs.erase(recentFXs.begin());
			recentFXs.push_back(pname);
		}
	}
}

void readSettings ( gfcSettings &sett ) {
}

std::string GetPathFromFilename ( const std::string& filename ) {
	
	
    int pos=filename.find_last_of("/\\'");
    if (pos!=std::string::npos)
        return filename.substr(0,pos+1); //+1 to include the actual slash
    else
        return filename;
}

std::string GetPathFromFilenameRegular ( const std::string& filename ) {
#ifdef WIN32
    return filename.substr ( 0, filename.rfind ( "/" ) +1 );
#else
    return filename.substr ( 0, filename.rfind ( "/" ) +1 );
#endif
}

//replace the windows slash to two \\ in order to be used with an fltk menu.

void ReplaceWindowsBackslash(std::string &filename)
{
	//find all the \ and replace with /
	while ( filename.find ( '\\' ) !=std::string::npos ) {
		filename.replace ( filename.find ( '\\' ),1,"/" );
	}
}

void ReplaceWindowsSlash ( std::string &filename ) {
#ifdef WIN32
	
    // check to see if the slashes are already altered by looking for adjacent "\\".
    if ( filename.find ( "\\\\" ) !=std::string::npos )
        return;
	
	
    //find all the \ and replace with *
    while ( filename.find ( '\\' ) !=std::string::npos ) {
        filename.replace ( filename.find ( '\\' ),1,"*" );
        //printf("Inserting * instead of \\ \n");
    }
	
    //find all the / and replace with *
    while ( filename.find ( '/' ) !=std::string::npos ) {
        filename.replace ( filename.find ( '/' ),1,"*" );
        //printf("Inserting * instead of / \n");
        //printf("Temp filename: %s\n",filename.c_str());
    }
	
    //printf("Replacing * for \\\\ in %s\n",filename.c_str());
    while ( filename.find ( '*' ) !=std::string::npos ) {
        filename.replace ( filename.find ( '*' ),1,"\\\\",2 );
        //printf("Replacing * with \\\\ \n");
    }
    //printf("Adding extra escape slashes");
	
    //printf("Converted filename: %s\n",filename.c_str());
#endif
}

std::string upperCase(std::string s)
{
	
	/*for (int i=0; i<s.length(); ++i)
{
  		s[i]=toupper(s[i]);
} */
 	
 	std::transform(s.begin(), s.end(), s.begin(), 
				   (int(*)(int)) std::toupper);
 	return s;
}

std::string lowerCase(std::string s)
{
	
	/*for (int i=0; i<s.length(); ++i)
{
  		s[i]=tolower(s[i]);
} */
 	std::transform(s.begin(), s.end(), s.begin(), 
				   (int(*)(int)) std::tolower);;
				   return s;
}

std::string GetExtension ( const std::string& filename ) {
#ifndef WIN32
    if ( filename.size() >filename.substr ( filename.rfind ( "/" ) ).size() )
#else
		// if(filename.size()>filename.substr(filename.rfind("\\")).size())
#endif
        return filename.substr ( filename.rfind ( "." ) +1 );
}

std::string GetFilenameNoFilePrefix ( const std::string& filename ) {
	
	
	
    if (filename.size()>7 && filename.find("file://")==0)
        return filename.substr(7);
    else
        return filename;
	
	
}

int removeFile(std::string theFilename)
{
	//boost::system::error_code error;
	//std::filesystem::remove(std::filesystem::path(theFilename),error);
	//printf("DeleteFile: %s (%i): %s\n",theFilename.c_str(),error.value(),error.message().c_str());
	//return error.value();
	std::filesystem::remove(std::filesystem::path(theFilename));
	return 0;
}

std::string RemoveNewLine( const std::string& filename ) {
	
	
    return filename.substr(0,filename.find_first_of("\n\r"));
	
}

std::string GetFilenameNoPath ( const std::string& filename ) {
    return std::filesystem::path(filename).filename().string();
}

// Simple hash function for FX/LUT caching (not cryptographic)
std::string GetMD5Hash ( std::string theString ) {
    // Use std::hash as a simple non-cryptographic hash for cache keys
    std::size_t h = std::hash<std::string>{}(theString);
    char buf[17];
    snprintf(buf, sizeof(buf), "%016zx", h);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// Portable content digest (JEF-28 Task 1) — FNV-1a 64-bit over raw bytes.
// Deterministic + identical across builds/platforms for the same bytes (the
// old GetMD5Hash used std::hash, whose result is implementation-defined). The
// P2P LUT/FX sync dedup depends on peers agreeing on the digest, so it MUST be
// a fixed algorithm over the actual file bytes, not std::hash and not parsed
// values. FNV-1a is not cryptographic; it is only used for content dedup.
namespace jefe {

static const uint64_t kFnvOffset = 14695981039346656037ULL;
static const uint64_t kFnvPrime  = 1099511628211ULL;

std::string contentHash(const unsigned char* data, size_t len) {
    uint64_t h = kFnvOffset;
    for (size_t i = 0; i < len; ++i) {
        h ^= (uint64_t)data[i];
        h *= kFnvPrime;
    }
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return std::string(buf);
}

std::string contentHashString(const std::string& s) {
    return contentHash((const unsigned char*)s.data(), s.size());
}

// Streaming FNV-1a so we never materialize a whole file (LUTs can be MBs).
// Feeds each file's bytes into a running hash; returns "" if `any` is false
// after all files (i.e. nothing was readable) so callers can detect failure.
static std::string contentHashFilesImpl(const std::vector<std::string>& paths,
                                        bool requireAny) {
    uint64_t h = kFnvOffset;
    bool any = false;
    char chunk[65536];
    for (const std::string& path : paths) {
        if (path.empty()) continue;
        std::ifstream f(path.c_str(), std::ios::binary);
        if (!f.is_open()) continue;
        any = true;
        while (f.good()) {
            f.read(chunk, sizeof(chunk));
            std::streamsize got = f.gcount();
            for (std::streamsize i = 0; i < got; ++i) {
                h ^= (uint64_t)(unsigned char)chunk[i];
                h *= kFnvPrime;
            }
        }
    }
    if (requireAny && !any) return std::string();
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return std::string(buf);
}

std::string contentHashFile(const std::string& path) {
    return contentHashFilesImpl({path}, /*requireAny=*/true);
}

std::string contentHashFiles(const std::vector<std::string>& paths) {
    return contentHashFilesImpl(paths, /*requireAny=*/true);
}

}  // namespace jefe

void UpdateRecentIPsButtons() {
}

void UpdateRecentBrowsedButtons ( int alreadyReplacedSlashes ) {
}


int getNextDivisibleBy4(int value) {
    //printf("\nInput %i\n",value);
	
    if (value<=4) {
        value=4;
    } else {
        if (value%4) {
			
            value= 4*((int)value/4 + 1);
        }
    }
    //printf("Output %i (%%4=%i)\n\n",value,value%4);
	
    return value;
}

int getNextPOT(int value) {
    //printf("In value %i\n",value);
    value--;
	
    for (int i=1;i<=16;i=i*2) {
        value = (value | (value >> i));
    }
	
    value++;
    //printf("Out value %i\n",value);
    return value;
}


int getFreeRam() {
    using namespace std;
#ifdef linux
	
	// http://www.devx.com/tips/Tip/30173
	
    //TODO: This needs to take into account cached memory as well
    std::ifstream meminfo("/proc/meminfo");
	
    if ( ! meminfo.is_open() ) {
        return -1;
    }
	
    char szTmp[256];
    char szMem[256];
	
    string s0("MemFree:");
    string s1("kB");
	
    while ( ! meminfo.eof() ) {
        meminfo.getline( szTmp, 256 );
		
        string s2(szTmp);
		
        string::size_type pos0 = s2.find(s0);
		
        if ( pos0 != string::npos ) {
            string::size_type pos1  = s2.find(s1);
			
            if ( pos1 != string::npos ) {
                string s3 = s2.substr( pos0 + s0.size(), pos1 - (pos0+s0.size()) );
                strncpy(szMem, s3.c_str(), s3.size() );
                int sizeInMB=atoi(szMem);
                //return (atoi(szMem)*1024*1024) ;
                return sizeInMB;
            }
        }
    }
	
	
#endif
	
#ifdef WIN32
	
	// http://msdn2.microsoft.com/en-us/library/aa366589.aspx
	
#include <windows.h>
	
	
	
    MEMORYSTATUSEX statex;
	
    statex.dwLength = sizeof (statex);
	
    GlobalMemoryStatusEx (&statex);
	
    int sizeInMB=statex.ullAvailPhys/1024/1024;
	
    return sizeInMB;
	
#endif
	
#ifdef __APPLE__
	
    // http://lists.apple.com/archives/Darwin-development/2004/Jul/msg00050.html
    // http://net-snmp.sourceforge.net/wiki/index.php/Memory_HAL
	
    /*
		Here's a snippet that should point you in the right direction:
	 
#import <mach/host_info.h>
#import <mach/mach_host.h>
	 
#define moniker(x) ( (x) >= 1024 ? ( (x) < 1048576 ? 'M' : 'G' ) : 'K'
					 )
#define truesize(x) ( (x) >= 1024 ? ( (x) < 1048576 ? (x)/1024 :
									  (x)/1048576 ) : (x) )
	 
	 vm_statistics_data_t page_info;
	 vm_size_t pagesize;
	 mach_msg_type_number_t count;
	 kern_return_t kret;
	 ...
	 
	 pagesize = 0;
	 kret = host_page_size (mach_host_self(), &pagesize);
	 
	 // vm stats
	 count = HOST_VM_INFO_COUNT;
	 kret = host_statistics (mach_host_self(), HOST_VM_INFO,
							 (host_info_t)&page_info, &count);
	 if (kret == KERN_SUCCESS){
		 unsigned int pw, pa, pi, pf;
		 float pu;
		 
		 // Calc kilobytes
		 pw = page_info.wire_count*pagesize / 1024;
		 pa = page_info.active_count*pagesize / 1024;
		 pi = page_info.inactive_count*pagesize / 1024;
		 pf = page_info.free_count*pagesize / 1024;
		 
		 pu = pw+pa+pi;
		 
		 tmp = [NSString stringWithFormat:
			 @"%u%c wired, %u%c active, %u%c inactive, %.2f%c used, %u%c
	 free, %d pageouts",
			 truesize(pw), moniker(pw), truesize(pa), moniker(pa),
			 truesize(pi), moniker(pi),
			 truesize(pu), moniker(pu), truesize(pf), moniker(pf),
			 page_info.pageouts];
		 */
#endif
	
    return -1;
	
	 }

void
swabUInt16(unsigned short* wp) {
    register unsigned char* cp = (unsigned char*) wp;
    unsigned char t;
	
    t = cp[1];
    cp[1] = cp[0];
    cp[0] = t;
}

void
swabUInt32(unsigned int* lp) {
    register unsigned char* cp = (unsigned char*) lp;
    unsigned char t;
	
    t = cp[3];
    cp[3] = cp[0];
    cp[0] = t;
    t = cp[2];
    cp[2] = cp[1];
    cp[1] = t;
}

void
swabArrayOfUInt16(unsigned short* wp, register size_t n) {
    register unsigned char* cp;
    register unsigned char t;
	
    while (n-- > 0) {
        cp = (unsigned char*) wp;
        t = cp[1];
        cp[1] = cp[0];
        cp[0] = t;
        wp++;
    }
}

void
swabArrayOfUInt32(register unsigned int* lp, register size_t n) {
    register unsigned char *cp;
    register unsigned char t;
	
    while (n-- > 0) {
        cp = (unsigned char *)lp;
        t = cp[3];
        cp[3] = cp[0];
        cp[0] = t;
        t = cp[2];
        cp[2] = cp[1];
        cp[1] = t;
        lp++;
    }
}

void
swabFloat(float *fp) {
    swabUInt32((unsigned int *) fp);
}

void
swabArrayOfFloat(float *fp, size_t n) {
    swabArrayOfUInt32((unsigned int *) fp, n);
}

void
swabDouble(double *dp) {
    register unsigned int* lp = (unsigned int*) dp;
    unsigned int t;
	
    swabArrayOfUInt32(lp, 2);
    t = lp[0];
    lp[0] = lp[1];
    lp[1] = t;
}

void
swabArrayOfDouble(double* dp, register size_t n) {
    register unsigned int* lp = (unsigned int*) dp;
    register unsigned int t;
	
    swabArrayOfUInt32(lp, n + n);
    while (n-- > 0) {
        t = lp[0];
        lp[0] = lp[1];
        lp[1] = t;
        lp += 2;
    }
}

/**
* Appends extension to filename if it does not already have it, otherwise it just returns filename untouched.
* @param filename The filename to append the extension to.
* @param ext Should contain the "." (".jpg, .txt etc")
* @return returns the filename with the extension appended if it does not already have it on.
*/
std::string AppendExtensionToFilename(std::string filename, std::string ext) {
    if (ext.size()>0 && ext.find_first_of(".")!=0) { //add a "." at begining of ext if not there
        ext.insert(ext.begin(),'.');
    }
	
    if (filename.find(ext)==std::string::npos) { //special case, if ext is nowhere to be found there, then just append it and return.
        return filename+ext;
    }
	
	//     std::cout << filename.substr(filename.rfind(ext));
	//     std::cout << " "<<filename.substr(filename.rfind(ext)).size();
	//     std::cout<< " " << ext.size() <<std::endl;
	
    if ( filename.substr(filename.rfind(ext)).size()==ext.size() ) { //if the size of the last "ext" of the filename is equal to ext then the extension is there and return the filename, otherwise, append the ext and return that.
        return filename;
    } else {
        return filename+ext;
    }
}

void AddMenuSlash(std::string &path) {
#ifdef WIN32
    path.insert(0,"/");
#endif
}

void RemoveMenuSlash(std::string &path) {
#ifdef WIN32
    std::string tmp;
    if (path.size()>0)
        path=path.substr(1);
#endif
}


std::string CreateRenderFilename ( gfcRenderParams params ) {
	
    std::string result=params.path;
    result+=params.prefix;
    char tmpStringForPadding[5];
    sprintf ( tmpStringForPadding,"%%0%ii",params.padding );
    char tmpNumbering[10];
    sprintf ( tmpNumbering,tmpStringForPadding,params.frame );
    result+=tmpNumbering;
    result+=params.postfix;
    result+=".";
    result+=params.formatString;
    return result;
}

#define GFC_MAX_READ_LINE_LENGHT 1240


void refreshSearchPathsBrowser() {
}

std::vector<std::string> GetFilenamesFromPastedText(const std::string& str)
{
	std::vector<std::string> result;
	result=TokenizeString(str,"\n"); //tokenize using line break;
	
	//remove the file:// prefix
	std::vector<std::string>::iterator it=result.begin(), itend=result.end();
	for (it; it!=itend; it++)
	{
		*it=GetFilenameNoFilePrefix(*it);
	}
	
	return result;
}

std::vector<std::string> TokenizeString(const std::string& str, const std::string& delimiters)
{
	std::vector<std::string> tokens;
	// Skip delimiters at beginning.
	std::string::size_type lastPos = str.find_first_not_of(delimiters, 0);
	// Find first "non-delimiter".
	std::string::size_type pos     = str.find_first_of(delimiters, lastPos);
	while (std::string::npos != pos || std::string::npos != lastPos)
	{
		// Found a token, add it to the vector.
		tokens.push_back(str.substr(lastPos, pos - lastPos));
		// Skip delimiters.  Note the "not_of"
		lastPos = str.find_first_not_of(delimiters, pos);
		// Find next "non-delimiter"
		pos = str.find_first_of(delimiters, lastPos);
	}
	
	return tokens;
	
}



/**
* Recursive function that finds a file in a folder and subfolders.
* @param filename
* @param thePath
* @param result
* @return
*/
int findFileInPath(std::string filename, std::filesystem::path thePath, bool recursive, std::string &result) {
	//     //1. If thePath is the filename fill in result and then return 1, make sure the filename is also not a directory
	// 	std::cout << "FindFileInPath :"<<filename<<std::endl << "thePath: "<<thePath.string()<<std::endl<<std::endl;
	// 	if (thePath.leaf()==filename && !std::filesystem::is_directory(thePath)) {
	//         result=thePath.string();
	//         return 1;
	//     } else {
	//         //2. If recursive and path is a directory also try inside each child that is a directory.
	//         if (std::filesystem::is_directory(thePath)) {
	//         //TODO: Iterate correctly over the path children. 
	//             std::filesystem::directory_iterator pathI(thePath);
	//             std::filesystem::directory_iterator endI;
	//             for(pathI; pathI!=endI;pathI++) {
	//                 int childResult=0;
	//                 if (std::filesystem::is_directory(*pathI)) {
	//                     if (recursive)
	//                     	//std::cout<<*pathI<<std::endl;
	//                         childResult=findFileInPath(filename,(*pathI),recursive,result);
	//                     } else {
	//                     childResult=findFileInPath(filename,(*pathI),recursive,result);
	//                 }
	// 
	//                 if (childResult!=0) {
	//                     return 1;
	//                 }
	//                 
	//             }
	//             return 0;
	//         }
	//         return 0;
	//     }
	
	//construct a path with the path and the filename, if it exists, then	we found the result, 
	//otherwise, if the path is a directory, iterate through all it's children doing the same thing but ONLY for folders.
	std::filesystem::path testPath=thePath/filename;
	if(std::filesystem::exists(testPath))
	{
		//we found it!
		std::cout << "Found the file! "<<testPath.string() << std::endl;
		result=testPath.string();
		return 1;
	}
	else
	{
		//not in this folder, maybe in a subfolder?
		if(recursive)
		{
			std::filesystem::directory_iterator pathI(thePath);
			std::filesystem::directory_iterator endI;
			int childResult=0;
			for(pathI; pathI!=endI;pathI++) {
				
				childResult=0;
				if (std::filesystem::is_directory(*pathI)) {
					childResult=findFileInPath(filename,(*pathI),recursive,result);
					if(childResult==1)
					{
						//one of our descendents found it, return 1, the result is already in result.
						return 1;
					}
				}
			}
			//if we got here, then we did not find it.
			return 0;
		}
		else
		{
			//not in this folder and should not look in subfolders, return 0, not found.
			return 0;
		}
	}
}

std::string findFileInSearchPaths(std::string theFile) {
	
    //1. Iterate through all each search path, navigating into subfolders if required.
    int folderCount=sett.searchPaths.size();
    std::string result=theFile;
    std::filesystem::path tempPath(theFile);
	//std::string theFileLeaf=tempPath.leaf().string();
	
//	std::string theFileLeaf=tempPath.filename().string();

//From when we had different versions of boost
#ifdef WIN32
	std::string theFileLeaf=tempPath.filename().string();
#else
	std::string theFileLeaf=tempPath.filename();
#endif

	std::cout << "Trying to find in search paths thefileLeaf: " << theFileLeaf << std::endl;
    for(int i=0;i<folderCount;i++)
    {
    	std::filesystem::path thePath(sett.searchPaths[i]);
    	if(std::filesystem::exists(thePath) && !std::filesystem::is_empty(thePath))
    	{
    		if(findFileInPath(theFileLeaf, thePath, sett.searchPathsRecursive,result))
    			return result;
    	}
    }
    return result;
	
}

int isSupportedType(std::string theFilename)
{
	static std::map<std::string,int> extensionMap;
	static int mapInitialized=0;
	if (!mapInitialized)
	{
		extensionMap["jpg"]=1;
		extensionMap["jpeg"]=1;
		extensionMap["dpx"]=1;
		extensionMap["cin"]=1;
		extensionMap["exr"]=1;
		extensionMap["png"]=1;
		extensionMap["tga"]=1;
		extensionMap["tif"]=1;
		extensionMap["tiff"]=1;
		extensionMap["bmp"]=1;
		extensionMap["rgb"]=1;
		extensionMap["sgi"]=1;
		
		mapInitialized=1;
	}
	
	
	if (extensionMap.count(lowerCase(GetExtension(theFilename)))>0)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

int getFirstSequenceInDirectoryR(std::filesystem::path thePath, std::string &result) {

	if (isSupportedType(thePath.string()))
	{
		result=thePath.string();
		return 1;
	}
	else
	{
		if (std::filesystem::is_directory(thePath))
		{
			//iterate all children calling recursively if this is a directory.
			std::filesystem::directory_iterator pathI(thePath);
			std::filesystem::directory_iterator endI;
			for(pathI; pathI!=endI;pathI++)
			{				
				
				std::filesystem::path child;
				std::filesystem::path newPath= (pathI->path());
				if(getFirstSequenceInDirectoryR(newPath, result))
				{
					return 1;
				}
			}		
		}		
	}

	return 0;
}

std::string getFirstSequenceInDirectory(std::string thePath)
{
	std::string result=thePath;
	if(std::filesystem::exists(thePath) && !std::filesystem::is_empty(thePath))
	{
		if(getFirstSequenceInDirectoryR(thePath, result))
			return result;
	}

	return result;
}

std::string ReadTextFileIntoString(std::string filename) {
	
    std::ifstream fs(filename.c_str(), std::ifstream::in);
    std::string result;
    char tmpReadArray[GFC_MAX_READ_LINE_LENGHT];
	
    while (fs.good()) {
        fs.getline(tmpReadArray,GFC_MAX_READ_LINE_LENGHT);
        result+=tmpReadArray;
        if (fs.good()) {
            result+="\n";
        }
    }
    return result;
	
}

void instantiationForcingFunction() {
    XMLNode test;
	
    //this function forces the compiler to instanciate a template function for these types.
    saveSetting("test",(bool)1,test);
    saveSetting("test",(char)1,test);
    std::string testString;
    float testFloat;
    int testInt;
    bool testBool;
    char testChar;
	
    readSetting("test",testString,test);
    readSetting("test",testFloat,test);
    readSetting("test",testInt,test);
    readSetting("test",testBool,test);
    readSetting("test",testChar,test);
	
}

int validEmailCharacters(std::string s)
{
	//check vaild alpha numeric
	for (int i=s.length()-1;i>=0;i--)
	{
		char c=s[i];
		//printf("%c\n",c);
		if (isalnum(c)==0 && c!='.' && c!='_' && c!='-' && c!='+')
		{
			return 0;
		}
	}
	
	return 1;
	
}

int checkValidEmail(std::string email)
{
	//check for 1 and only 1 "@"
	int atCount=0;
	atCount=std::count(email.begin(),email.end(),'@');
	if(atCount!=1)
	{
		printf("To many or not at least one at @ in email\n");
		return 0;
	}
	
	//check for valid username and domain
	char *emailChar=new char[email.length()+1];
	strcpy(emailChar,email.c_str());
	
	std::string username;
	std::string domain;
	char *pch=strtok(emailChar,"@");
	if (pch!=NULL)
	{	
		if (!validEmailCharacters(pch))
		{
			printf("Invalid characters in email user\n");
			return 0;
		}
		
		pch = strtok (NULL, "@");
		if (pch!=NULL)
		{	
			if (strlen(pch)==0 || !validEmailCharacters(pch))
			{
				printf("Invalid characters in email domain\n");
				return 0;
			}
		}
		else
		{
			printf("Invalid email: No domain!\n");
			return 0;
		}
		
	}
	else
	{
		printf("Invalid email: no local name!\n");
		return 0;
	}
	
	return 1;
	
}



std::string getOS() {
#ifdef __APPLE__
	return "Apple";
#endif
#ifdef WIN32
	return  "Win32";
#endif
#ifdef linux
	return "Linux";
#endif
}

std::vector< std::string> getMACAddress() {
	
	using namespace std;
	std::vector<std::string> returnVal;
	
	
#ifdef __APPLE__
	
	io_iterator_t  intfIterator;
	UInt8    MACAddress[ kIOEthernetAddressSize ];
	kern_return_t  kernResult = KERN_SUCCESS; // on PowerPC this is an int (4 bytes)
	
	kernResult = FindEthernetInterfaces(&intfIterator);
	
	char tmp[17]={'\0'};
	sprintf(tmp,"%s","00:00:00:00:00:00");
	char tmp2[2];
	if (KERN_SUCCESS != kernResult)
	{
		printf("FindEthernetInterfaces returned 0x%08x\n", kernResult);
		returnVal.push_back(lowerCase(tmp));
	}
	else {
		kernResult = GetMACAddress(intfIterator, MACAddress);
		
		if (KERN_SUCCESS != kernResult)
		{
			printf("GetMACAddress returned 0x%08x\n", kernResult);
			returnVal.push_back(lowerCase(tmp));
		}
		else
		{
			//we got it!
			int s=0;
			for( s = 0; s < 6; s++ )
			{
				//printf("%.2X ", (unsigned char)buffer.ifr_hwaddr.sa_data[s]);
				if(s<5)
				{
					sprintf(&tmp[s*3],"%.2X:", (unsigned char)MACAddress[s]);
				}
				else{
					sprintf(&tmp[s*3],"%.2X", MACAddress[s]);
				}
				
			}
			//		printf("\n");
			//		printf("New method:%s\n",lowerCase(tmp).c_str());
			returnVal.push_back(lowerCase(tmp));
			//returnVal=lowerCase(tmp);
		}
	}
	
	(void) IOObjectRelease(intfIterator);  // Release the iterator.
	
#endif
	
#ifdef linux
	
	// 	system("/sbin/ifconfig > /tmp/jefechecktmpIP");
	// 
	// 	ifstream tmpIfConfig("/tmp/jefechecktmpIP");
	// 	tmpIfConfig.seekg (0, ios::end);
	// 	int length = tmpIfConfig.tellg();
	// 	tmpIfConfig.seekg (0, ios::beg);
	// 
	// 	// allocate memory:
	// 	char *ifConfig = new char [length];
	// 	char ipaddress[18]="                ";
	// 	for (int i=0;i<strlen(ipaddress);i++)
	// 		ipaddress[i]='\0';
	// 	//*/
	// 
	// 	tmpIfConfig.read(ifConfig,length);
	// 	char *pch=strstr(ifConfig,"HWaddr");
	// 	strncpy(ipaddress,pch+7,17);
	// 	ipaddress[17]='\0';
	// 	printf("\n\nMac Address %s\n",ipaddress);
	// 	// printf("The file read was> \n%s",ifConfig);
	// 	system("rm /tmp/jefechecktmpIP");
	// 	//return ipaddress;
	
	
	
	
	int s;
	
	struct ifreq buffer;
	s = socket(PF_INET, SOCK_DGRAM, 0);
	memset(&buffer, 0x00, sizeof(buffer));
	strcpy(buffer.ifr_name, "eth0");
	ioctl(s, SIOCGIFHWADDR, &buffer);
	close(s);
	char tmp[18]={'\0'};
	char tmp2[2];
	for( s = 0; s < 6; s++ )
	{
		//printf("%.2X ", (unsigned char)buffer.ifr_hwaddr.sa_data[s]);
		if(s<5)
		{
			sprintf(&tmp[s*3],"%.2X:", (unsigned char)buffer.ifr_hwaddr.sa_data[s]);
		}
		else{
			sprintf(&tmp[s*3],"%.2X", (unsigned char)buffer.ifr_hwaddr.sa_data[s]);
		}
		
	}
	//	printf("\n");
	//	printf("New method:%s\n",lowerCase(tmp).c_str());
	returnVal.push_back(lowerCase(tmp));
	//returnVal=lowerCase(tmp);
	
	
	
#endif
	
	
#ifdef WIN32
	IP_ADAPTER_INFO AdapterInfo[16];       // Allocate information
										   // for up to 16 NICs
	DWORD dwBufLen = sizeof(AdapterInfo);  // Save memory size of buffer
	
	DWORD dwStatus = GetAdaptersInfo(      // Call GetAdapterInfo
										   AdapterInfo,                 // [out] buffer to receive data
										   &dwBufLen);                  // [in] size of receive data buffer
	assert(dwStatus == ERROR_SUCCESS);  // Verify return value is
										// valid, no buffer overflow
	
	PIP_ADAPTER_INFO pAdapterInfo = AdapterInfo; // Contains pointer to
												 // current adapter info
	
	int counter=0;
	int adapterCount=0;
	do {
		char tmp[10]={'\0'};
		std::string anAddress;
		//pAdapterInfo->AdapterName.String();
		//strncpy(tmp,(const char*)&pAdapterInfo->Address[0],8);
		for (UINT i = 0; i < pAdapterInfo->AddressLength; i++) {
			
			sprintf(tmp,"%02x", pAdapterInfo->Address[i]);
			anAddress+=tmp;
			
			if (i!=pAdapterInfo->AddressLength-1)
				anAddress+=":";
			
			//tmp[i]=pAdapterInfo->Address[i];
			strncat(tmp,(const char*)&pAdapterInfo[i],1);
		}
		//printf("tmp: %s\n",anAddress.c_str());
		//PrintMACaddress(pAdapterInfo->Address); // Print MAC address
		pAdapterInfo = pAdapterInfo->Next;    // Progress through
											  // linked list
		adapterCount++;
		returnVal.push_back(anAddress);
	} while (pAdapterInfo /*&& adapterCount < 1 */);                  // Terminate if last adapter
#endif
	
	/*std::cout << "Get MAC Address: "<<std::endl;;
	for (int i=0;i<returnVal.size();i++)
	{
		std::cout<<returnVal[i]<<std::endl;
	}*/
	
	return returnVal;
};

std::string getHostname() {
	using namespace std;
	std::string returnVal;
	//return ""; //WE ARE NO LONGER TAKING THE HOSTNAME INTO ACCOUNT!
#ifdef __APPLE__
	
	system("/bin/hostname > /Users/Shared/tmphostname");
	
	ifstream tmp("/Users/Shared/tmphostname");
	tmp.seekg (0, ios::end);
	int length = tmp.tellg();
	tmp.seekg (0, ios::beg);
	
	// allocate memory:
	char *tmpChar = new char [length];
	//char *hostname=new[150];
	tmp.get(tmpChar,length);
	system("rm /Users/Shared/tmphostname");
	printf("hostname is %s\n",tmpChar);
	return tmpChar;
#endif
	
	
#ifdef linux
	
	system("/bin/hostname > /tmp/jefechecktmpHost");
	
	ifstream tmp("/tmp/jefechecktmpHost");
	tmp.seekg (0, ios::end);
	int length = tmp.tellg();
	tmp.seekg (0, ios::beg);
	
	// allocate memory:
	char *tmpChar = new char [length];
	//char *hostname=new[150];
	tmp.get(tmpChar,length);
	system("rm /tmp/jefechecktmpHost");
	std::string result=tmpChar;
	if (tmpChar)
		delete [] tmpChar;
	return result;
	
	
#endif
	
	
#ifdef WIN32


	WORD wVersionRequested;
	WSADATA wsaData;
	char name[255];
	wVersionRequested = MAKEWORD( 2, 0 );
	
	if ( WSAStartup( wVersionRequested, &wsaData ) == 0 ) {
		
		if ( gethostname ( name, 255) == 0) {
			returnVal=name;
		} else {
			returnVal="";
		}
		
		WSACleanup( );
	}
	
#endif
	//printf("HostName: %s\n",returnVal.c_str());
	return returnVal;
};

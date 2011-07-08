// gfcSequence.h: interface for the gfcSequence class.
//
//////////////////////////////////////////////////////////////////////

#include "gfcsequencegui_fltk.h"

#if !defined(AFX_GFCSEQUENCE_H__B640892A_60B9_471B_86DB_D6EE198B1304__INCLUDED_)
#define AFX_GFCSEQUENCE_H__B640892A_60B9_471B_86DB_D6EE198B1304__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <map>
#include <vector>
#include <queue>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#  include <GL/glut.h>
#endif

#include <FL/Fl_Box.H>
#include "UIConstants.h"
#include "gfcPlate.h"
#include "trackwidget.h"

#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>

#include <Fl/Fl_Value_Slider.H>
#include "gflC.h"
#include "dpxslice.h"
#include <vector>
#include "gfcStructures.h"
#include "gfcloadparams.h"
#include "gfcimageloader.h"
#include "gfcframe.h"

#include "gfcNetworkStructures.h"

class gfcFrame;
class RawFrame;

class ExrChannelInfo;
extern gfcSettings sett;
void findSequence(std::vector<std::string> &refFiles, const char *str,char *label, int *firstFrame,Fl_Value_Slider *loadFromSpinner,Fl_Value_Slider *loadToSpinner);
std::vector<ExrChannelInfo> getExrChannels(const char fileName[]);
void resizeTimeLine();
void resizeAllSldrs();

class Rectang;

/*******Imf**********/

#include <OpenEXR/ImfRgba.h>
#include <OpenEXR/ImfArray.h>

class ExrChannelInfo
{

public:

    char prefix[64];
    char components[16][64]; //include the . in the component, ie. .r .g .b, or .x .y .z
    int numOfComponents;
};

class ImageView
{
public:

    ImageView (int x, int y,
               int w, int h,            // display window width and height
               const char label[],
               const Imf::Rgba pixels[/* w*h */],
               int dw, int dh,		// data window width and height
               int dx, int dy,		// data window offset
               float exposure,
               float defog,
               float kneeLow,
               float kneeHigh);
    ImageView(); //default constructor
    void set
        (int x, int y,
                int w, int h,
                const char label[],
                const Imf::Rgba pixels[],
                int dw, int dh,
                int dx, int dy,
                float exposure,
                float defog,
                float kneeLow,
                float kneeHigh);

    virtual void        setExposure (float exposure);
    virtual void	setDefog (float defog);
    virtual void	setKneeLow (float low);
    virtual void	setKneeHigh (float high);
    Imf::Array<unsigned char>	_screenPixels;
    virtual void        updateScreenPixels ();

public:


    void		computeFogColor ();

    float		_exposure;
    float		_defog;
    float		_kneeLow;
    float		_kneeHigh;
    const Imf::Rgba *	_rawPixels;
    float		_fogR;
    float		_fogG;
    float		_fogB;
    int			_dw;
    int			_dh;
    int			_dx;
    int			_dy;

private:
};








class gfcPlate;



class gfcSequenceInfo
{

public:

    gfcSequenceInfo()
    {
        strcpy(fileName,"No Filename");
        strcpy(fileFormat,"No Format");
    }

    char fileName[1024];
    char theString[4096];
    int numFrames;
    int sizeX;
    int sizeY;
    int channels;
    int bpp;
    char fileFormat[2048];
    std::string dpxMetadata;

    const char* getString();
};



void startTrackAStreamLoadingThread();


class gfcRawFrameQueue
{
private:
std::queue<gfcFrame> frames;

public:

};


class gfcSequence
{

private:

    gfcLoadParams params;
    
    std::vector<gfcFrame> frames;
    std::queue<gfcFrame> rawFrames;
	boost::condition cond; //this condition is used to wait for the queue to go to the correct level.
	
    std::queue<int> loadedFrames;
    gfcFrame previewFrame;
    gfcTimer previewTimer;
    gfcRectang aoi;
    void updateEstimates();
    
    /***********************
    File sequence attributes
    /**********************/
    std::vector<std::string> files;
	std::vector<std::string> previewFiles; //files loaded from the preview to determine range.
    int sequenceStartFrame;
    int sequenceEndFrame; ///Stores the results of findSequence
    int firstFrame;
    
    
   /* std::vector<gfcFrame> frames;
    std::vector<std::string> files;
    std::queue<gfcFrame> rawFrames;
    int offset;
    int holdMode;
    int holdFrame;
	
public:
    
    
    //boost::try_mutex rawQueueMutex;
    //bool loadingCanceled;
    
    gfcFrame getFrame(int frameNumber, bool forceLoad=false);
    void peek(gfcLoadParams params, gfcPeekInfo *results);
    void generateTextures(int howMany=1);
    
    int numOfFrames();
   
    void setHoldMode(int holdMode);
    void setHoldFrame(int holdFrame);*/
   
   
   public:
   gfcSequence();
    virtual ~gfcSequence();
    
    boost::thread* myThread;
    gfcSequenceInfo info;
    int numFrames;
    int compressed;
    
	static gfcLoadParams getDefaultLoadParamsFor(std::string filename);

	gfcLoadParams getLoadParamsFromGUI();
	
	void setLoadParamsToGUI(gfcLoadParams);

    void setAOI(int x, int y, int w, int h, bool relative=false);
    gfcRectang getAOI();
    void findSequenceFiles(std::vector<std::string> &files, int updateGUI=false);
	void prepareSequenceFiles();
    
    void fillFiles(const char* fileName); //fills the files vector;
    
   
    
    void setRecentlyLoaded(std::vector<std::string> filenames);
    float preCache; //percent of frames behind the current frame to load
    int frameCacheSize; // how many frames we can currently load into memory
    int frameSizeKB; //the size in KB of one of the frames in this sequence. It remembers the last frame that was loaded.
    int frameOffset; //how much the track will offset in relation to the timeline
    int holdFrameMode; //What hold frame mode is on. 0-None, 1-First, 2-Current, 3-Last. Will afect what frame ID is returned with getTexIDatFrame calls.
    int holdFrameCurrent; //when holdFrameMode is 2 (current) this is the frame that will be returned. Whenever current is put into selection, the current frame will be captured and stored in this variable.
    GLuint pbo; //each sequence has it's own PBO
    int pboX; //the size of the PBO. 
    int pboY;
    
    TrackWidget *sldr;
    Fl_Button *abortButton;
    char trackID;
    char label[5000];
    int loadSequence(std::string fileName, int scale=100, Rectang* aoi=NULL, int filterType=FILTERLANCZOS_ID, bool append=false, float gamma=1, int fromFrame=0, int toFrame=0, int loadingFromTimeLine=0, float exposition = 1, float defog = 0, float kneeH =0, float kneeL=0, int channel=-1);
    
    //new ones************
    int loadSequence();
    int initializeSequence(gfcLoadParams params);
    int generateTextures(int howMany=1);
    int getRangeStart();
    int getRangeEnd();
    int getNumFrames();
	int getNumPreviewFrames();
    void startLoading(gfcLoadParams params);
    void startLoading(int fromTrack=0,int fromTimeLine=0);
    void clearRawQueue();
    void stopLoading();
    bool isEmpty();
    void unloadAndClear();
    void clearPreviewFrame();
    void setForceGFLLoading(bool value);
    void setContinueLoadingOnError(bool value);
    gfcNetTrackStateInfo getTrackStateInfo();
    void setTrackStateInfo(gfcNetTrackStateInfo info);
    
    void updateTrackWidget();
    std::string loadPreview();
    gfcFrame getPreviewFrame();
    
    gfcSequenceGUI* myGUI;
    std::string filenameGeneric; ///contains the generic for of the sequence name , like /path/to/filename.####.dpx. It is obtained in findSequenceFiles
    void saveTrackSessionParameters(XMLNode & trackNode);
    void loadTrackSessionParameters(XMLNode & trackNode);
    //end of new ones***********
    
    
    void initSequence(std::string fileName, int scale=100, Rectang* aoi=NULL,int filterType=FILTERLANCZOS_ID, int append=GFC_SEQREPLACE, float gamma=1, int fromFrame=0, int toFrame=0, int loadFromTimeline=0, int compressed=0,float exposition = 1, float defog = 0, float kneeH =0, float kneeL=0, int channel=-1);
    void initSequence(gfcLoadParams params);
    void loadSequenceThread();
    int clearSequence();
    gfcFrame getFrame(int frameNo, bool forceLoad=false); //forcing the load loads that frame and returns it to the caller, but does not store it in the vector.
    gfcFrame forceLoad(int frame);
    //GLuint getTexIDatFrame(int frameNo, int slice=0);
    //GFLC_BITMAP* getBitmapAtFrame(int frameNo, int slice=0); //used when there are not NPOTs
    //int getSizeXAtFrame(int frameNo,int slice=0);
    //int getSizeYAtFrame(int frameNo,int slice=0);
    char * getFilenameatFrame(int frameNo);
   
    Rectang getFrameSizeAt(int frameNo);
    void resizeSldr(void);
    bool isActive;
    int quadrant;
    //now it's a global for each sequence boost::try_mutex rawMutex; //mutex for the raw sequence, controls access to the raw queue for loading and generating textures.
    bool generateTexture(RawFrame *pRawFrame=NULL, gfcFrame *pFrame=NULL); //locks the raw frame queue, checks if it's empty, if not, pops first element and generates glTexture.
    
    //Hard drive streaming variables
    std::vector<GLuint> cacheTextures; //stores x number amount of cache textures, these textures are used to do a glTexSubImage upload.
    bool streaming; //determines if this is a streaming track.
    void loadForStreaming();
    boost::thread* StreamingThread;
    void (*StreamingThreadStarterFunc)(void);
    
     void setOffset(int offset);
     void setHoldMode(int holdMode, int holdFrame=0);
     int getHoldMode();
     int getOffset();
     void setVisibleFromAndTo(int visibleFrom, int visibleTo); ///sets the GUIs tracks widget to the correct visible range.
     
    bool freeFrames(int numFrames); //removes the first numFrames frames from the loadedFrames queue.
    //thread variables start with "t"
    std::string tfileName;
    int tscale;
    int tfilterType;
    bool tappend;
    float tgamma;
    int tfromFrame;
    int ttoFrame;
    int loadingFromTimeline;
    float texposition;
    float tdefog;
    float tkneeH;
    float tkneeL;
    int tchannel;
    Rectang *taoi;
    boost::try_mutex rawQueueMutex;
    bool loadingCanceled;
    Fl_Value_Slider *loadFromSpinner;
    Fl_Value_Slider *loadToSpinner;
    int rangeBegin;
    int rangeEnd;
    int maxFramesToLoad; //how many frames should be loaded for this sequence, assigned dynamicaly depending on how many tracks are being loaded and how many fit in ram.
    bool maxFramesReached();
    std::vector<int> luts;
#include "gfcframe.h"
    gfcFrame forcedFrame;
    void cleanForcedLoad();
    
    bool forceGFLLoading;
    bool continueLoadingOnError;
    
    
};

void startSequenceThread(gfcSequence* theSequence);

#endif // !defined(AFX_GFCSEQUENCE_H__B640892A_60B9_471B_86DB_D6EE198B1304__INCLUDED_)

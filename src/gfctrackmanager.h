#ifndef GFCTRACKMANAGER_H
#define GFCTRACKMANAGER_H

#include "glew.h"
#include <vector>
#include <string>
#include <map>
#include "gfcloadparams.h"
#include "gfcSequence.h"
#include "gfcplaylistitem.h"
#include "gfcNetworkStructures.h"


#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>

#ifndef GFC_MAX_SEQUENCES
#define GFC_MAX_SEQUENCES 4
#endif

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/
class gfcTrackManager{
public:
    gfcTrackManager();

    ~gfcTrackManager();
	
	static const int MAX_SEQUENCES=GFC_MAX_SEQUENCES;

	void startLoadingSequence(int whichOne, gfcLoadParams params);
	void startLoadingSequence(int whichOne);
	
	void stopLoadingAll();
	void stopLoadingSequence(int whichOne);
	void startLoadingSequenceAt(int whichOne, int startFrame);
	
	
	void startLoadingAll(std::vector<gfcLoadParams> params);
	void startLoadingAll();
	void startLoadingAllAt(int startFrame);
	
	void setForceGFLLoading(bool value);
	void setContinueLoadingOnError(bool value);
	
	void generateTextures();
	void updateTrackWidgets();
	
	void cleanForcedLoaded();
	
	void clearAllSequences();
	
	void loadPreviewFrame(int whichOne);
	
	int getTrackIDfromWidget(void* widget);
	
	void loadFromFilename(int whichOne, gfcLoadParams params);
	
	void addToRecentlyLoaded(std::string filename);
	
	gfcSequence* getSequence(int whichOne);
	
	std::vector<gfcNetTrackStateInfo> getTrackStateInfo();
	void setTrackStateInfo(std::vector<gfcNetTrackStateInfo> info);
	
	int getMaxTrackLength();
	int getMinTrackLength();
	
	void saveTrackSessionParameters(XMLNode &tracksNode);
	void loadTrackSessionParameters(XMLNode &tracksNode);
	
	//auxiliary functions used by the playbackManager to determine loops
	int getFirstFirstLoaded();
	int getLastFirstLoaded();
	int getFirstLastLoaded();
	int getLastLastLoaded();
	
	void setScaleOverride(int scale);

	gfcPlaylistItem getPlaylistItem();
	void setPlaylistItem(gfcPlaylistItem,bool fromNetwork=false);
	
	void setRecentBrowsed(std::vector<std::string> precentBrowsed);
	void initializeWidgets(); //uses the global mainWindow instance, if the GUI changes, then this will probably have to change to, 
	
	void updateTrackWidgetsFromAndTo(int visibleFrom, int visibleTo); //the link from timeline size events to the track widgets
	void updateTrackWidgetsCurrentFrame(int currentFrame); //the link from the timeline position to track widgets
	std::vector<std::string> recentBrowsed; //all tracks share this attribute, so it is stored in the track manager.	

	void setAutoAcceptRemoteLoadRequests(int value);
	int getAutoAcceptRemoteLoadRequests();
	
	boost::try_mutex readMutex;
	int ioBusy;
private:
gfcSequence sequences[GFC_MAX_SEQUENCES];
int numOfSequences;
int autoAcceptRemoteLoadRequests;

bool checkBounds(int whichOne, std::string functionName="");

int scaleOverride; //used when loading a playlistItem. It overrides the scale.

long currentFrame;

gfcSequence dummySequence; //used to return when an out of range sequence is requested, should rarely be used, but prevents us from breaking in certain extreme situations.

};


#endif

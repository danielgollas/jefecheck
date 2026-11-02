// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef GFCTRACKMANAGER_H
#define GFCTRACKMANAGER_H

#include <glad/glad.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <string>
#include <map>
#include "gfcloadparams.h"
#include "gfcSequence.h"
#include "gfcplaylistitem.h"
#include "gfcNetworkStructures.h"



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
	bool anyTrackHasAudio() const; ///true if any track has a sound clip; cheap, safe from the GL thread.
	
	std::vector<gfcNetTrackStateInfo> getTrackStateInfo();
	void setTrackStateInfo(std::vector<gfcNetTrackStateInfo> info);
	
	int getMaxTrackLength();

	// Rolling cache. Resident frames over all tracks.
	int residentFrameCount() const;
	// True when every track with footage at this timeline frame has it
	// resident (true when no track has footage there).
	bool timelineFrameResident(int frame) const;
	// The same for count frames from `from`, stepping by direction (+1/-1).
	bool timelineFramesResident(int from, int count, int direction) const;
	// Hands the coordinator the playhead, loop mode, limits and rate for a load.
	// Sends the player transport to the frame cache: always (a load), or only when it changed (force=false).
	void syncCacheTransport(bool force=true);
	// A jump: each movie track's audio thread fills the sound at this timeline
	// frame first when it is not there yet. Atomic stores only.
	void retargetAudio(int timelineFrame);
	int getMinTrackLength();
	
	void saveTrackSessionParameters(XMLNode &tracksNode);
	void loadTrackSessionParameters(XMLNode &tracksNode);
	
	//auxiliary functions used by the playbackManager to determine loops. They
	//read each non-empty track's trim range (getRangeStart/End, offset applied),
	//not its resident frames, so the loop limits do not move as frames are evicted.
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

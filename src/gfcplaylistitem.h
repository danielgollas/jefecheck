#ifndef GFCPLAYLISTITEM_H
#define GFCPLAYLISTITEM_H

#include <string>
#include <vector>
#include "gfcloadparams.h"
#include "gfcfxstack.h"

#include "xmlParser.h"

class gfcPlaylistItemProgramState{

public:
	gfcPlaylistItemProgramState();
	~gfcPlaylistItemProgramState();


	void saveStateToNode(XMLNode node) const;
	void loadStateFromNode(XMLNode node);
	gfcNetPlaybackInfo playbackInfo;
	std::vector<gfcNetPlateStateInfo> plateStateInfo;
	std::vector<gfcNetTrackStateInfo> trackStateInfo;
	int layout;

};

class gfcPlaylistItem
{
public:
	gfcPlaylistItem(void);
	~gfcPlaylistItem(void);
	

	bool operator==(const gfcPlaylistItem &that);

	std::vector<gfcLoadParams> loadParams;
	std::vector<gfcFXStack> fxstacks;
	
	gfcPlaylistItemProgramState programState;
	
	void savePlaylistItemParameters(XMLNode &entryNode) const;
	std::string asString() const;
	// matchByName is passed to gfcFXStack::loadStackFromNode: true for files,
	// false for items received from a peer (exact effect match only).
	void loadPlaylistItemParameters(XMLNode &entryNode, bool matchByName=true);
	void appendTracks(const std::vector<std::string> &files);
	void fixWindowsPaths();
	
	
	short selected;
	int index;

};


#endif

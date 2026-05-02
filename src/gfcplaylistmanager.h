#ifndef PLAYLISTMANAGER_H
#define PLAYLISTMANAGER_H

#include "gfcplaylistitem.h"
#include "gfcStructures.h"  // GFC_PL_GUI_TYPE
// Stand-in for the FLTK build's playlistwindow.h class. Same shape; we
// just don't pull the FLTK widget hierarchy along with it.
class PlaylistParamInfo {
public:
    PlaylistParamInfo() {}
    PlaylistParamInfo(int pplIndex, GFC_PL_GUI_TYPE pType)
        : plIndex(pplIndex), type(pType) {}
    int plIndex;
    GFC_PL_GUI_TYPE type;
};
#include <vector>
#include <map>

class gfcPlaylistManager
{
public:
	gfcPlaylistManager(void);
public:
	~gfcPlaylistManager(void);

	int addItemlist(gfcPlaylistItem, int noRepeat=0);
	gfcPlaylistItem getItem(int index);
	
	gfcPlaylistItem createPlaylistItemFrom(std::vector<std::string> filenames);

	std::vector<gfcPlaylistItem> *getPlaylist();
	//direction 1(down) or -1(up)
	void movePlaylistItem(int index, int direction) ;
	void moveSelection(int direction);
	void removePlaylistItem(int index);
	void clearPlaylist(int networkNotify=1);
	// Widget callback API. Handle is opaque so the header doesn't pull
	// FLTK; the .cpp casts back to Fl_Widget* inside USE_FLTK gating.
	void addPLIGUIInfo(PlaylistParamInfo, void* widgetHandle);
	void handlePLIGUICB(void* widgetHandle, void* data);
	void setSelectedItem(int);
	void refreshSelectedItem();
	
	void appendTracksToItem(const std::vector<std::string> &files, int index);

	 void savePlaylistParameters(XMLNode &plNode);
	 void loadPlaylistParameters(XMLNode &plNode, int replace=0, int noRepeats=0);

	 std::string getPlaylistAsString();
	 void setPlaylistFromString(std::string s, int replace=0);
	
	

	 void mergePlaylist(std::string s);

	 void loadPlaylist(std::string filename);
	 void savePlaylist(std::string filename);

	 void handlePlaylistEventOther(gfcNetPlaylistEvent theEvent);

	int selectedItem;
	
private:
	std::vector<gfcPlaylistItem> entries;
	std::map<void*, PlaylistParamInfo> guiToPlaylistItem; //maps each widget (opaque handle) to info about the playlist item it belongs to.

};

#endif
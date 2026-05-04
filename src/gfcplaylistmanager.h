#ifndef PLAYLISTMANAGER_H
#define PLAYLISTMANAGER_H

#include "gfcplaylistitem.h"
#include "playlistwindow.h"
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
	void addPLIGUIInfo(PlaylistParamInfo, Fl_Widget*);
	void handlePLIGUICB(Fl_Widget*, void* data);
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
	std::map<Fl_Widget*,PlaylistParamInfo> guiToPlaylistItem; //maps each widget to info about the playlist item it belong to.
	
};

#endif
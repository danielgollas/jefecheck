#ifndef PLAYLISTWINDOW_H
#define PLAYLISTWINDOW_H

class gfcPlaylistItem;
class PlaylistWindow;
class gfcPlaylistWindowWindow;

#include <glad/glad.h>
#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include "gfcplaylistwindowwindow.h"
#include <FL/Fl_Scroll.H>
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Pack.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Value_Input.H>
#include <FL/Fl_Input_Choice.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Tile.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Browser.H>
#include <FLU/Flu_Collapsable_Group.h>
#include "gfcStructures.h"
#include "gfcplaylistitem.h"



#include <vector>
#include <map>
#include <string>


/**
@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/

//defines a variable name belonging to a certaing effect.
/*
class gfcFX;
class gfcFXStack;
*/
class PlaylistParamInfo{
public:
	PlaylistParamInfo(){};
	PlaylistParamInfo(int pplIndex, GFC_PL_GUI_TYPE  pType){
		plIndex=pplIndex;
		type=pType;
	};
	int plIndex;
	GFC_PL_GUI_TYPE type;
};

class PlaylistItemWidgetBrowser: public Fl_Browser
{
public:
	PlaylistItemWidgetBrowser(int X, int Y, int W, int H, const char* L=0);
	int handle(int e);
	int index;
	PlaylistWindow* parentWindow;
	int get_full_height();
	int get_full_width();
private:
	
};

class PlaylistItemWidget: public Fl_Group
{
public:
	PlaylistItemWidget(int X, int Y, int W, int H, const char* L=0);
	int handle(int e);
	void setItem(gfcPlaylistItem item);
	PlaylistWindow* parentWindow;

private:
	Fl_Browser *fixedBox;
	PlaylistItemWidgetBrowser *browser;
};

class PlaylistScroll: public Fl_Scroll{
	int nchild;
	std::vector<PlaylistItemWidget*> items;
	Fl_Pack *packGroup;
public:
	PlaylistScroll(int X, int Y, int W, int H, const char* L=0);

	void reset();

	void resize(int X, int Y, int W, int H);
	
	virtual int handle(int e);

	// Append new PlaylistItemWidget to bottom
	//     Note: An Fl_Pack would be a good way to do this, too
	//
	void AddItem(gfcPlaylistItem item);
	PlaylistWindow* parentWindow;
};

class PlaylistWindow{
public:
	PlaylistWindow();

	~PlaylistWindow();

	void createMenu();
	void fillPlaylistPane(int scrollPos=0);
	void fillPlaylistScroll();
	void createWindow();
	void updateWindow();
	
	int handle ( int e );


	gfcPlaylistWindowWindow *theWindow;
	Fl_Input_Choice* scaleOverrideMenu;
	Fl_Check_Button* scaleOverrideCheckbox;
	Fl_Tile* tile;
	Fl_Box* bgBox;
	Fl_Menu_Bar* menuBar;
	Fl_Scroll *scroll;
	Fl_Scroll* bottomPaneScroll;
	Fl_Pack* itemPacker;
	Fl_Scroll* topPaneScroll;
	int prevScrollY;
	
	PlaylistScroll *plScroll;

	std::vector<gfcPlaylistItem> *thePlaylist;
	void scheduleWindowUpdate();


	//std::map<Fl_Widget*,fxParamInfo> guiToFX[4]; //from each GUI Widget, we can know what effect it belongs too and what name the variable has.
private:
	//gfcFXStack* theStack;
	int windowUpdateScheduled;
	int itemCount;
	int pliCounter;
	Fl_Group* createPLIEntry(gfcPlaylistItem &pli);
};

#endif

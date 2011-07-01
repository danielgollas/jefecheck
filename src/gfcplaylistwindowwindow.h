#ifndef GFCPLAYLISTWINDOWWINDOW_H
#define GFCPLAYLISTWINDOWWINDOW_H

#include <FL/Fl_Double_Window.H>
#include "playlistwindow.h"

class gfcPlaylistWindowWindow :
	public Fl_Double_Window
{
public:
	gfcPlaylistWindowWindow(int X, int Y, int W, int H, const char* title=0);
	virtual ~gfcPlaylistWindowWindow(void);
	
	void setParentWindow(PlaylistWindow *theWindow);
	int handle(int e);
	


	PlaylistWindow *parentWindow;
};

#endif
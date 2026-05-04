#include "gfcplaylistwindowwindow.h"

#include "gfcplaylistmanager.h"
extern gfcPlaylistManager playlistManager;

#include "playlistwindow.h"
extern PlaylistWindow plw;

#include "gfctrackmanager.h"
extern gfcTrackManager trackManager;


gfcPlaylistWindowWindow::gfcPlaylistWindowWindow(int X, int Y, int W, int H, const char* title):Fl_Double_Window(X,Y,W,H,title)
{

}

gfcPlaylistWindowWindow::~gfcPlaylistWindowWindow(void)
{
}

void gfcPlaylistWindowWindow::setParentWindow(PlaylistWindow *theWindow)
{
	parentWindow=theWindow;
}

int gfcPlaylistWindowWindow::handle(int e)
{

	/*static int pastedXPos;
	static int pastedYPos;*/
	int ret = Fl_Double_Window::handle ( e );

	switch(e)
	{

	
	case FL_KEYDOWN:
		{
			switch (Fl::event_key()) {

				case FL_Enter:

					trackManager.setPlaylistItem(playlistManager.getItem(playlistManager.selectedItem));
					return 1;
					break;

				case FL_BackSpace:
				case FL_Delete:
					playlistManager.removePlaylistItem(playlistManager.selectedItem);
					plw.scheduleWindowUpdate();
					return 1;
					break;

				case FL_Up:
					if (Fl::event_shift())
					{
						playlistManager.movePlaylistItem(playlistManager.selectedItem,1);	
					}
					else
					{
						playlistManager.moveSelection(1);
					}

					plw.scheduleWindowUpdate();
					return 1;
					break;

				case FL_Down:
					if (Fl::event_shift())
					{
						playlistManager.movePlaylistItem(playlistManager.selectedItem,-1);
					}
					else
					{
						playlistManager.moveSelection(-1);
					}
					plw.scheduleWindowUpdate();
					return 1;
					break;
			}
		}
		break;
	}

	
	return ret;
}

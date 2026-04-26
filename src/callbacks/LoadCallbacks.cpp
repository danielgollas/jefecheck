// File-loading callbacks. Extracted from UICallbacks.cpp.
#include "CallbacksInternal.h"
#include <FL/Fl_Menu_Button.H>

bool isItemInMenu ( const char *text, Fl_Choice *menu ) {
	for ( int i=menu->size()-2;i>=0;i-- ) {
		if ( strcmp ( text,menu->text ( i ) ) ==0 ) {
			return true;
		}
	}

	return false;
}



void loadCB ( Fl_Widget* o , void* v ) {

	static std::vector<ExrChannelInfo> exrChannelList;
	static bool firstTimeBrowseOpen=true;
	plateManager.setChanged();
	if ( ( long ) v==LOADREDUCE_ID ) {
		if ( strcmp ( lw.reduceButton->label(),"@8UpArrow" ) ==0 ) {
			lw.loadWindow->size ( 30,28 );
			lw.reduceButton->label ( "@2UpArrow" );
			lw.loadWindow->border ( 0 );
			lw.reduceButton->position ( 0,0 );
			//lw.reduceButton->size ( 30,18 );
			printf ( "Minimized load\n" );
		} else {
			lw.loadWindow->size ( 785,405 );
			//lw.reduceButton->size ( 30,18 );
			lw.reduceButton->position ( 753,1 );
			lw.reduceButton->label ( "@8UpArrow" );
			//lw.loadWindow->border ( 1 );
			printf ( "Maximize load\n" );
		}
	}

	switch ( ( long ) v ) {


	case LOADSTART_ID: {
		int trackID=trackManager.getTrackIDfromWidget(o);
		if (trackID!=-1) {
			trackManager.startLoadingSequence(trackID);
		}
					   }
					   break;

	case LOADADDTOQUEUE_ID: {
		printf("Add to Queue\n");
		playlistManager.addItemlist(trackManager.getPlaylistItem());
		if (plw.theWindow->visible()) {
			plw.scheduleWindowUpdate();
		}
							}
							break;

	case LOADOPENPLAYLIST_ID: {
		Fl::first_window(mw.mainWindow);
		plw.theWindow->show();
		plw.scheduleWindowUpdate();
							  }
							  break;

	case LOADLOAD_ID: {
		sessionManager.writeCrashSession();
		trackManager.startLoadingAll();
		lw.loadWindow->hide();
		plateManager.updateAllFromGUI();
		sessionManager.writeCrashSession();

		/*playlistManager.addItemlist(trackManager.getPlaylistItem());
		if (plw.theWindow->visible()) {
		plw.updateWindow();
		}
		networkManager.sendPlaylistItem(trackManager.getPlaylistItem());*/
					  }
					  break;

	case LOADDONE_ID: {
		lw.loadWindow->hide();
		plateManager.updateAllFromGUI();

					  }
					  break;

	case LOADRECENT_ID: {
		sessionManager.writeCrashSession();
		//When a new filename comes in, we need to find the sequence and update the GUI. But when we start loading we need to find the sequence without updating the GUI.
		int trackID=trackManager.getTrackIDfromWidget(o);
		if (trackID!=-1) {
			std::string selectedName=((Fl_Menu_Button*)o)->text();
			RemoveMenuSlash(selectedName);

			trackManager.getSequence(trackID)->myGUI->setFilename(selectedName);
			trackManager.loadPreviewFrame(trackID);
		}
		sessionManager.writeCrashSession();
						}
						break;

	case LOADUNLOADANDCLEAR_ID: {
		sessionManager.writeCrashSession();
		int trackID=trackManager.getTrackIDfromWidget(o);
		if (trackID!=-1) {
			trackManager. getSequence(trackID)->unloadAndClear();
		}
		sessionManager.writeCrashSession();
								}
								break;

	case LOADBROWSE_ID: {
		sessionManager.writeCrashSession();
		fc->callback ( save_input_file );
		fc->preview ( 0 );
		fc->type ( Fl_File_Chooser::SINGLE );
		fc->label("Select a single file in the sequence");

		fc->filter ( "Image Files (*.{jpg,jpeg,gif,dpx,tif,tiff,iff,bmp,tga,exr,rgb,sgi,png,cin})\t DPX (*.dpx) \t TIF (*.{tif,tiff}) \t IFF (*.iff) \t OpenEXR (*.exr) \t JPEG (*.{jpg,jpeg})" );

		if (firstTimeBrowseOpen) {
			firstTimeBrowseOpen=false;
			fc->directory(sett.defaultBrowsePath.c_str());
		}

		fc->show();
		while ( fc->shown() )
			app().waitForEvents();


		int trackID=trackManager.getTrackIDfromWidget(o);
		if (trackID!=-1) {
			if (strcmp(gFilename,"") && strcmp(gFilename," ")) {
				trackManager.getSequence(trackID)->myGUI->setFilename(gFilename);
			}
			trackManager.loadPreviewFrame(trackID);

		}
		sessionManager.writeCrashSession();
						}
						break;


	case LOADFROM_ID:
	case LOADTO_ID:
		//TODO: Set the range so that FROM is not smaller than TO etc.
		break;

	case LOADUPDATEPREVIEW_ID: {
		sessionManager.writeCrashSession();
		int trackID=trackManager.getTrackIDfromWidget(o);
		if (trackID!=-1) {

			trackManager.loadPreviewFrame(trackID);
		}
		sessionManager.writeCrashSession();
							   }
							   break;

	default:

		//printf ( "Unmanaged loadWindow callback! ID: %i\n", ( long ) v );

		break;
	}
}

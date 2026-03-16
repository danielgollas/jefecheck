#ifdef WIN32
#include <windows.h>
#endif 

#include "playlistwindow.h"
#include <string>
#include <map>
#include <vector>
#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Pack.H>
#include <FL/Fl_Tile.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Value_Input.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_File_Chooser.H>
#include "xmlParser.h"
#include "gfcStructures.h"
//#include "network.h"
#include "gfcloadparams.h"

void playlistItemCB(Fl_Widget* o, void* data);

#include "gfcfxmanager.h"
extern gfcFXManager fxManager;

#include "lutWindow.h"
extern LutWindow lutw;

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

#include "gfcplaylistmanager.h"
extern gfcPlaylistManager playlistManager;

#include "gfclutmanager.h"
extern gfcLUTManager lutManager;

#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;

#include "gfctrackmanager.h"
extern gfcTrackManager trackManager;

#include "gfcsessionmanager.h"
extern gfcSessionManager sessionManager;

#include "mainWindow.h"
extern MainWindow mw;

#include "loadWindow.h"
extern LoadWindow lw;

#include "fxcontrolwindow.h"
extern FXControlWindow fxControlWindow1;

extern gfcSettings sett;
extern Fl_File_Chooser *fc;
extern PlaylistWindow plw;
extern void save_input_file(Fl_File_Chooser *w, void *userdata);
extern char gFilename[300];

#ifndef max 
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

void generalPLIGUICB(Fl_Widget* o, void* data);

PlaylistWindow::PlaylistWindow() {

}


PlaylistWindow::~PlaylistWindow() {}


PlaylistItemWidgetBrowser::PlaylistItemWidgetBrowser(int X, int Y, int W, int H, const char* L): Fl_Browser(X,Y,W,H,L)
{
	this->scrollbar_width(10);

	index = 0;
}

int PlaylistItemWidgetBrowser::get_full_height()
{
	return full_height();
}
int PlaylistItemWidgetBrowser::get_full_width()
{
	int maxWidth=0;
	
	return full_width();
}

PlaylistItemWidget::PlaylistItemWidget(int X, int Y, int W, int H, const char* L) : Fl_Group(X,Y,W,H,L) {
	begin();
	const int defaultHeight = 25;
	const int fixedWidth = 50;
	
	fixedBox = new Fl_Browser(X,Y,fixedWidth,defaultHeight,L);
	fixedBox->box(FL_FLAT_BOX);
	fixedBox->color(fl_rgb_color(75,75,75));
	fixedBox->labelcolor(FL_WHITE);
	fixedBox->textcolor(FL_WHITE);
	fixedBox->labelsize(12);
	fixedBox->align(FL_ALIGN_RIGHT);
	fixedBox->end();

	// Stretchy box
	browser = new PlaylistItemWidgetBrowser(X+fixedWidth+2,Y,W-fixedWidth-2,defaultHeight);
	browser->box(FL_FLAT_BOX);
	browser->color(fl_rgb_color(75,75,75));
	browser->textcolor(FL_WHITE);
	browser->textsize(12);
	resizable(browser);
	browser->end();
	end();
}

void PlaylistItemWidget::setItem(gfcPlaylistItem item)
{
	browser->clear();
	int vsize=0;
	const int rowSize=20;
	const int highlightGray=100;

	for (int i=0;i<item.loadParams.size();i++)
	{

		gfcLoadParams tmp=item.loadParams[i];
		if(tmp.fileName!=""){
		char track[2]={static_cast<char>('A'+i),0};
		std::string tmpString="@b";
		tmpString+="Track ";
		tmpString+=track;
		tmpString+=":  ";
		if (sett.playlistShowFullPaths==0)
		{
			tmpString+=GetFilenameNoPath(tmp.fileName);
		}
		else
		{
				tmpString+=tmp.fileName;
		}
		
		browser->add(tmpString.c_str());
		vsize+=rowSize;

		if (sett.playlistShowCompactView==0 || item.selected)
		{
			std::stringstream ss;
			ss.str("");
			ss<<"\tRange:"<<tmp.fromFrame<<"-"<<tmp.toFrame<<" | Total Frames: "<<tmp.toFrame-tmp.fromFrame+1<<" | Scale:"<<tmp.scale<<"%";

			browser->add(ss.str().c_str());
			vsize+=rowSize;
		}

		}
	}
	
	fixedBox->labelfont(FL_HELVETICA);
	
	if (item.selected)
	{
		browser->color(fl_rgb_color(highlightGray,highlightGray,highlightGray));
		fixedBox->color(fl_rgb_color(highlightGray,highlightGray,highlightGray));
		fixedBox->labelfont(FL_HELVETICA_BOLD);
	}
	
	browser->index=item.index;

	char tmpTitle[20];
	sprintf(tmpTitle,"@b@m@c%i",item.index+1);
	fixedBox->copy_label(tmpTitle);
	fixedBox->add(tmpTitle);
	fixedBox->callback((Fl_Callback*)playlistItemCB, (void*)(item.index));
	browser->callback((Fl_Callback*)playlistItemCB, (void*)(item.index));
	
	fixedBox->when(FL_WHEN_RELEASE_ALWAYS);
	const int scrollbarHeight=10;
	vsize=browser->get_full_height();
	//printf("full width=%i, w=%i\n",browser->get_full_width(),w());
	//if (browser->get_full_width()>=w())
	{	
		vsize+=scrollbarHeight;
	}
	this->resize(x(), y(), w(),vsize); //add room for scrollbar
}

int PlaylistItemWidgetBrowser::handle(int e)
{	
	
	switch(e)
	{
	case FL_DND_ENTER:
		printf("Entering DND in browser widget\n");
		return 1;
		break;

	case FL_DND_LEAVE:
		return 1;
		break;

	case FL_DND_DRAG:
	case FL_DND_RELEASE:
		std::cout << "DND_DRAG or DND_RELEASE on browser widget\n";
		return 1;
		break;

	case FL_MOUSEWHEEL:
		return 0;
		break;

	case FL_PASTE:
		{
			// *****HANDLE DRAG AND DROP******** //
			std::string pastedText=Fl::event_text();
			std::cout<<"pasted text into browser widget!: "<<GetFilenameNoFilePrefix(RemoveNewLine(pastedText))<<std::endl<<"nextLine"<<std::endl;
			//TODO: Create a playlist item with the track, loading all default values, but we need to calculate the range at least.
			std::vector<std::string> filenames=GetFilenamesFromPastedText(pastedText);
			if (filenames.size()==1 && lowerCase(GetExtension(filenames[0]))=="jpl")
			{
				printf("Dropped playlist! %s\n",filenames[0].c_str());
				playlistManager.loadPlaylist(filenames[0]);
			}
			else
			{
			playlistManager.appendTracksToItem(filenames,this->index);
			}

			((PlaylistItemWidget*)(this->parent()))->parentWindow->scheduleWindowUpdate();

			return 1;
		}
		break;
	}
	//if(this)
	int returnVal=Fl_Browser::handle(e);
	  return returnVal;
	//else
	  //return 1;
}

int PlaylistItemWidget::handle(int e)
{	
	
	switch(e)
	{
	/*case FL_DND_ENTER:
		printf("Entering DND in widget\n");
		return 1;
		break;

	case FL_DND_LEAVE:
		return 1;
		break;

	case FL_DND_DRAG:
	case FL_DND_RELEASE:
		std::cout << "DND_DRAG or DND_RELEASE on widget\n";
		return 1;
		break;*/
	
	case FL_MOUSEWHEEL:
		return 0;
		break;

	/*case FL_PASTE:
		{
			// *****HANDLE DRAG AND DROP******** //
			std::string pastedText=Fl::event_text();
			std::cout<<"pasted text into widget!: "<<GetFilenameNoFilePrefix(RemoveNewLine(pastedText))<<std::endl<<"nextLine"<<std::endl;
			//TODO: Create a playlist item with the track, loading all default values, but we need to calculate the range at least.
			std::vector<std::string> filenames;
			filenames.push_back(GetFilenameNoFilePrefix(RemoveNewLine(pastedText)));
			//playlistManager.appendTracksToItem(filenames,this->theItem.index);
			//playlistManager.addItemlist(playlistManager.createPlaylistItemFrom(filenames));
			//playlistManager.addItemlist(trackManager.getPlaylistItem());
			/*if (this->visible()) {
				((gfcPlaylistWindowWindow*)parent())->parentWindow->updateWindow();
			}*/
		/*	return 1;
		}
		break;*/
	}
	int returnVal=Fl_Group::handle(e);
	return returnVal;
}

PlaylistScroll::PlaylistScroll(int X, int Y, int W, int H, const char* L) : Fl_Scroll(X,Y,W,H,L) {
	type(Fl_Scroll::VERTICAL_ALWAYS);
	packGroup = new Fl_Pack(X,Y,W-30,H-5,L);
	packGroup->spacing(3);
	packGroup->end();
	add(packGroup);
	nchild = 0;
}

void PlaylistScroll::AddItem(gfcPlaylistItem item) {
	
	const int fixedWidth = 50;
	const int defaultHeight = 25;
	int X = x() + 1,
		Y = y() - yposition() + (nchild*defaultHeight) + 1,
		W = w() - 20,                           // -20: compensate for vscroll bar
		H = defaultHeight;
	
	PlaylistItemWidget *plW=new PlaylistItemWidget(X,Y,W,H );
	plW->parentWindow=parentWindow;
	plW->setItem(item);

	packGroup->add(plW);
	items.push_back(plW);
	init_sizes();
	redraw();
	nchild++;
}

void PlaylistScroll::reset()
{
	items.clear();
	packGroup->clear();
	nchild=0;
}


void PlaylistScroll::resize(int X, int Y, int W, int H) {
	// Tell children to resize to our new width
	/*for ( int t=0; t<children(); t++ ) {
		printf("resizing child %i\n",t);
		Fl_Widget *w = child(t);
		w->resize(w->x(), w->y(), W-20, w->h());    // W-20: leave room for scrollbar
	}*/

	packGroup->resize(packGroup->x(), packGroup->y(), W-30, packGroup->h());
	
	// Tell scroll children changed in size
	init_sizes();
	Fl_Scroll::resize(X,Y,W,H);
}

int PlaylistScroll::handle(int e)
{
	
	switch(e)
	{
	case FL_DND_ENTER:
	case FL_DND_DRAG:
	case FL_DND_RELEASE:
		return 1;
		break;

	case FL_PASTE:
		{
			// *****HANDLE DRAG AND DROP******** //
			std::string pastedText=Fl::event_text();
			std::cout<<"pasted text: "<<GetFilenameNoFilePrefix(RemoveNewLine(pastedText))<<std::endl<<"nextLine"<<std::endl;
			//TODO: Create a playlist item with the track, loading all default values, but we need to calculate the range at least.
			std::vector<std::string> filenames=GetFilenamesFromPastedText(pastedText);

			if (filenames.size()==1 && lowerCase(GetExtension(filenames[0]))=="jpl")
			{
				printf("Dropped playlist! %s\n",filenames[0].c_str());
				playlistManager.loadPlaylist(filenames[0]);
			}
			else
			{
				playlistManager.addItemlist(playlistManager.createPlaylistItemFrom(filenames));
			}

			if (this->visible()) {
				((gfcPlaylistWindowWindow*)parent())->parentWindow->scheduleWindowUpdate();
			}
			


			//AddItem(GetFilenameNoFilePrefix(RemoveNewLine(pastedText)));

			return 1;
		}
		break;

	}

	int returnVal =Fl_Scroll::handle(e);
	return returnVal;

}

void updateRecentlyLoadedPlaylists(std::string pfileName)
{
	/*if (sett.recentFXStacks.size()<sett.maxRecentFXStacks) {
	//check if the stack is not in the vector already.
	bool alreadyInRecent=false;
	for (int i=0;i<sett.recentFXStacks.size();i++) {
	if (sett.recentFXStacks[i]==pfileName) {
	alreadyInRecent=true;
	break;
	}
	}

	if (!alreadyInRecent)
	sett.recentFXStacks.push_back(pfileName);
	} else {
	{
	bool alreadyInRecent=false;
	for (int i=0;i<sett.recentFXStacks.size();i++) {
	if (sett.recentFXStacks[i]==pfileName) { //if the stack is full, and the name already exists, then delete it from where it was and push it at the top.
	alreadyInRecent=true;
	sett.recentFXStacks.erase(sett.recentFXStacks.begin()+i);
	sett.recentFXStacks.push_back(pfileName);
	break;
	}
	}

	if (!alreadyInRecent) { //if it's not in the recent, then erase the first one and push the new one.
	sett.recentFXStacks.erase(sett.recentFXStacks.begin());
	sett.recentFXStacks.push_back(pfileName);
	}
	}
	}*/
}

void playlistItemCB(Fl_Widget* o, void* data){
	
	//printf("playlistItemCB %i\n",(long)data);
	if(Fl::event_clicks()){
		//printf("clicked on playlist item %i\n",(int)data);
		trackManager.setPlaylistItem(playlistManager.getItem((long)data));
		playlistManager.setSelectedItem((long)data);
		plw.scheduleWindowUpdate();
		fxControlWindow1.scheduleUpdateWindow(fxControlWindow1.quadrant);
		
		
	}
	else
	{
		playlistManager.setSelectedItem((long)data);
		plw.scheduleWindowUpdate();
	}

}



void generalPLIGUICB(Fl_Widget* o, void* data) {
	
	playlistManager.handlePLIGUICB(o,data);
	
}


void playlistMenuCB(Fl_Widget* o, void* data) {
	//printf("PLAYLIST MENU CALLBACK!!!!!!\n");

	{//manage the other menus under control/
		switch ((long)data) {
		
		case PL_MENU_SHOW_COMPACT_VIEW:
			{
				sett.playlistShowCompactView=((Fl_Menu_*)o)->mvalue()->value();
				printf("Compact View: %i\n",sett.playlistShowCompactView);
				plw.scheduleWindowUpdate();
			}
			break;

		case PL_MENU_SHOW_FULL_PATHS:
			sett.playlistShowFullPaths=((Fl_Menu_*)o)->mvalue()->value();
			printf("Show full paths: %i\n",sett.playlistShowFullPaths);
			plw.scheduleWindowUpdate();
			break;

		case PL_MENU_SCALE_OVERRIDE:
				if (plw.scaleOverrideCheckbox->value())
				{
					trackManager.setScaleOverride(atoi(plw.scaleOverrideMenu->value()));
				}
				else
				{
					trackManager.setScaleOverride(-1);
				}
				
			break;

		case PL_MENU_CLOSE:
			//printf("Close the FX Window\n");
			//fxControlWindow1.theWindow->hide();
			break;

		case PL_MENU_RECENT: {
			//printf("menu clicked: %s\n",((Fl_Menu_*)o)->text());
			/*fxControlWindow1.loadStack(((Fl_Menu_*)o)->text());
			fxControlWindow1.scheduleUpdateWindow(fxControlWindow1.quadrant);*/

			/*std::string pfileName=((Fl_Menu_*)o)->text();
			RemoveMenuSlash(pfileName);
			plateManager.loadStackFromFile(quadrant,pfileName);
			updateRecentlyLoadedStacks(pfileName);
			fxControlWindow1.scheduleUpdateWindow(quadrant);*/
							 }
							 break;

		case PL_MENU_LOAD_PLAYLIST: {

			fc->callback(save_input_file);
			fc->preview(0);
			fc->filter("JefeCheck Playlists (*.jpl)");
			fc->label("Select a JefeCheck Playlist File");
			fc->type(Fl_File_Chooser::SINGLE);
			fc->show();
			while (fc->shown())
				Fl::wait();

			if (fc->count()) {

				std::string pfileName=fc->value(0);
				playlistManager.loadPlaylist(pfileName);
				plw.scheduleWindowUpdate();
				/*
				plateManager.loadStackFromFile(quadrant,pfileName);
				updateRecentlyLoadedStacks(pfileName);
				fxControlWindow1.scheduleUpdateWindow(quadrant);*/
			}

								 }
								 break;

		case PL_MENU_SAVE_PLAYLIST: {
			fc->callback(save_input_file);
			fc->preview(0);
			fc->filter("JefeCheck Playlists (*.jpl)");
			fc->label("Select or Create a JefeCheck Playlist File");
			fc->type(Fl_File_Chooser::CREATE);
			fc->show();
			while (fc->shown())
				Fl::wait();

			if (fc->count()) {
				playlistManager.savePlaylist(fc->value(0));
			}
									}
									break;

		case PL_MENU_CLEAR_ALL:
				playlistManager.clearPlaylist();

			break;

		case PL_MENU_LOAD_WINDOW:

			break;
		}
	}

}


void PlaylistWindow::createWindow() {


	int theWidht=750;
	int theHeight=300;
	
	
	std::string tmpLabel = "Playlist Window";

	{
		{
			int x, y;
			x=(mw.mainWindow->x()+theWidht)/2;
			y=(mw.mainWindow->y()+theHeight)/2;
			gfcPlaylistWindowWindow* o = theWindow= new gfcPlaylistWindowWindow(x, y, theWidht, theHeight, tmpLabel.c_str());
			o->copy_label(tmpLabel.c_str());

			//theWindow->set_modal();
			//theWindow->clear_border();
			theWindow->set_non_modal();
			
			theWindow->resizable(theWindow);
			theWindow->box(FL_FLAT_BOX);
			theWindow->box(FL_THIN_UP_FRAME);
			theWindow->color(FL_BLACK);
			o->user_data((void*)(this));
			
			
			{
				{
					Fl_Menu_Bar* o = menuBar = new Fl_Menu_Bar(0, 0, theWindow->w(), 25);
					o->box(FL_FLAT_BOX);
					o->color(fl_rgb_color(GFC_BG_COLOR,GFC_BG_COLOR,GFC_BG_COLOR));
					o->labelcolor(FL_BACKGROUND2_COLOR);
					o->textcolor(7);
					//o->box(FL_FLAT_BOX);
					
					o->add
						("Playlist/Save Playlist...",0,(Fl_Callback*)playlistMenuCB,(void*)PL_MENU_SAVE_PLAYLIST,0);
					o->add
						("_Playlist/Load Playlist...",0,(Fl_Callback*)playlistMenuCB,(void*)PL_MENU_LOAD_PLAYLIST,0);
					o->add
						("Playlist/Load Window...",0,(Fl_Callback*)playlistMenuCB,(void*)PL_MENU_LOAD_WINDOW,0);
					o->add
						("Playlist/Clear Playlist",0,(Fl_Callback*)playlistMenuCB,(void*)PL_MENU_CLEAR_ALL,0);
					o->add
						("Playlist/Close",0,(Fl_Callback*)playlistMenuCB,(void*)PL_MENU_CLOSE,0);


					//Options Menu
					o->add("View/Show Full Paths",0,(Fl_Callback*)playlistMenuCB,(void*)PL_MENU_SHOW_FULL_PATHS,FL_MENU_TOGGLE | (sett.playlistShowFullPaths?FL_MENU_VALUE:FL_MENU_TOGGLE));
					o->add("View/Compact View",0,(Fl_Callback*)playlistMenuCB,(void*)PL_MENU_SHOW_COMPACT_VIEW,FL_MENU_TOGGLE | (sett.playlistShowCompactView?FL_MENU_VALUE:FL_MENU_TOGGLE));

				}
				
				//add the playlist scale override control here.

				scaleOverrideCheckbox=new Fl_Check_Button(theWindow->w()-22,5,15,15);
				scaleOverrideCheckbox->callback((Fl_Callback*)playlistMenuCB,(void*)PL_MENU_SCALE_OVERRIDE);
				scaleOverrideCheckbox->when(FL_WHEN_CHANGED);
				
				scaleOverrideMenu=new Fl_Input_Choice(theWindow->w()-57-25,1,55,20,"Playlist Scale Override");
				scaleOverrideMenu->value("100");
				scaleOverrideMenu->add("100");
				scaleOverrideMenu->add("50");
				scaleOverrideMenu->add("25");
				scaleOverrideMenu->align(FL_ALIGN_LEFT);
				scaleOverrideMenu->labelcolor(fl_rgb_color(254,254,254));
				scaleOverrideMenu->labelsize(10);
				scaleOverrideMenu->textsize(10);
				scaleOverrideMenu->tooltip(strdup("Overrides the scale at which items loaded from the playlist are loaded. This is useful during remote sessions with computers that have more RAM than us. Only we will load in the overriden scale."));
				scaleOverrideMenu->callback((Fl_Callback*)playlistMenuCB,(void*)PL_MENU_SCALE_OVERRIDE);
				scaleOverrideMenu->when(FL_WHEN_CHANGED);
		
				plScroll = new PlaylistScroll(0,menuBar->h(),theWindow->w(),theWindow->h()-menuBar->h());
				plScroll->box(FL_FLAT_BOX);
				plScroll->color(fl_rgb_color(48,48,48));
				plScroll->end();
				plScroll->init_sizes();
				Fl::check();
				theWindow->resizable(menuBar);
				theWindow->resizable(plScroll);
				plScroll->parentWindow=this;
			}
			theWindow->end(); //theWindow end
			
		}
	}
	theWindow->setParentWindow(this);
	scheduleWindowUpdate();
}

void PlaylistWindow::updateWindow() {
	
	if(!this->theWindow->visible())
	return;
	
	if(windowUpdateScheduled){
	windowUpdateScheduled=0;
	plScroll->reset();
	fillPlaylistScroll();
	plScroll->redraw();
	}
}

void PlaylistWindow::fillPlaylistScroll()
{

	thePlaylist=playlistManager.getPlaylist();

	if (!thePlaylist) {
		printf("Could not obtain Playlist\n");
	}

	itemCount=thePlaylist->size();
	for(pliCounter=0;pliCounter<itemCount;pliCounter++){
		gfcPlaylistItem tmpPli = thePlaylist->at(pliCounter);
		tmpPli.index=pliCounter;
		plScroll->AddItem(tmpPli);
		
	}

}

void  PlaylistWindow::fillPlaylistPane(int scrollPos)
{
	bottomPaneScroll->clear();	
	bottomPaneScroll->position(0,scrollPos);
	thePlaylist=playlistManager.getPlaylist();

	if (!thePlaylist) {
	printf("Could not obtain Playlist\n");
	}
	
	Fl_Pack *pliPacker = new Fl_Pack(bottomPaneScroll->x(), menuBar->h(), bottomPaneScroll->w()-30, 1);
	pliPacker->resizable(pliPacker);
	//fxPacker->resizable(fxPacker);
	
	pliPacker ->type(Fl_Pack::VERTICAL);
	pliPacker ->spacing(7); //a little space between each entry
	pliPacker->align(FL_ALIGN_INSIDE);
	pliPacker ->end();
	
	itemCount=thePlaylist->size();
	for(pliCounter=0;pliCounter<itemCount;pliCounter++){
	gfcPlaylistItem tmpPli = thePlaylist->at(pliCounter);
	pliPacker->add(createPLIEntry(tmpPli));

	/*Fl_Group *spacer=new Fl_Group(0,0,10,1);
	spacer->box(FL_FLAT_BOX);
	spacer->color(FL_GRAY);
	spacer->end();
	pliPacker->add(spacer);*/

	

	pliPacker->init_sizes();
	}
	
	bottomPaneScroll->add(pliPacker);
	bottomPaneScroll->resizable(pliPacker);
}

Fl_Group* PlaylistWindow::createPLIEntry(gfcPlaylistItem &pli)
{
	//1. This pack contains the general controls pack and controls pack.
	Fl_Pack* generalAndControlPacker = new Fl_Pack(0, 0, 10, 1); 
	generalAndControlPacker->type(Fl_Pack::VERTICAL);
	generalAndControlPacker->spacing(0);
	generalAndControlPacker->box(FL_NO_BOX);
	if (pli.selected)
	{
		generalAndControlPacker->box(FL_BORDER_FRAME);
		generalAndControlPacker->color(FL_WHITE);
	}
	else
	{
		generalAndControlPacker->box(FL_NO_BOX);
	}
	
	generalAndControlPacker->end();
	
		Fl_Pack *generalControlPack=new Fl_Pack(5, 20,1,1,"");
		generalControlPack->type(Fl_Pack::VERTICAL);
		generalControlPack->spacing(2);
		generalControlPack->end();
		/*{
			Fl_Button* o = new Fl_Button(0,0, 20, 20);

			o->copy_label("@>");
			o->labelcolor(FL_BLACK);
			o->labelfont(FL_HELVETICA_BOLD);
			o->labelsize(10);
			o->align(FL_ALIGN_INSIDE);
			o->callback((Fl_Callback*)generalPLIGUICB, (void*)(this));
			
			PlaylistParamInfo info(pliCounter,PL_GUI_LOAD);
			playlistManager.addPLIGUIInfo(info,o);
			generalControlPack->add(o);
			int w=0,h=0;

			//add this dummy to protect the on/off label size.
			fl_font(fl_font(), o->labelsize());
		}
		{
			Fl_Button* o = new Fl_Button(0,0, 20, 20);

			o->copy_label("@9+");
			o->labelcolor(FL_RED);
			o->labelsize(10);
			o->labelfont(FL_HELVETICA_BOLD);
			o->align(FL_ALIGN_INSIDE);
			o->callback((Fl_Callback*)generalPLIGUICB, (void*)(this));

			PlaylistParamInfo info(pliCounter,PL_GUI_DELETE);
			playlistManager.addPLIGUIInfo(info,o);
			generalControlPack->add(o);
			int w=0,h=0;
		}
		{
			Fl_Button* o = new Fl_Button(0,0, 20, 20);
			o->copy_label("@8UpArrow");
			o->labelcolor(FL_BLACK);
			o->labelfont(FL_HELVETICA_BOLD);
			o->align(FL_ALIGN_INSIDE);
			o->labelsize(10);
			o->callback((Fl_Callback*)generalPLIGUICB, (void*)(this));

			PlaylistParamInfo info(pliCounter,PL_GUI_MOVEUP);
			playlistManager.addPLIGUIInfo(info,o);
			generalControlPack->add(o);
			int w=0,h=0;
		}
		{
			Fl_Button* o = new Fl_Button(0,0, 20, 20);
			o->copy_label("@2UpArrow");
			o->labelcolor(FL_BLACK);
			o->labelsize(10);
			o->labelfont(FL_HELVETICA_BOLD);
			o->align(FL_ALIGN_INSIDE);
			o->callback((Fl_Callback*)generalPLIGUICB, (void*)(this));

			PlaylistParamInfo info(pliCounter,PL_GUI_MOVEDOWN);
			playlistManager.addPLIGUIInfo(info,o);
			generalControlPack->add(o);
			int w=0,h=0;
		}*/

	
	

	//NOW CREATE THE ENTRIES FOR EACH TRACK
	//WE SHOULD USE A BROWSER I THINK, GIVES US SOME GOOD FORMATING
	/*
		generalAndControlPacker
				|+FL_BROWSER
	*/

	{
		Fl_Browser *b= new Fl_Browser(0,0,this->theWindow->w()-35,80);
		/*if (pli.selected)
		{
			b->box(FL_UP_BOX);
		}
		else*/
		{
			b->box(FL_FLAT_BOX);	
		}
		b->color(fl_rgb_color(75,75,75));
		
		b->textcolor(FL_WHITE);
		b->textsize(12);
		
		//TODO: Do this with iterators...
		int end=pli.loadParams.size();
		int rowCount=0;
		for (int i=0; i<end; i++)
		{
			gfcLoadParams lp=pli.loadParams[i];
			if(lp.fileName!="")
			{
				rowCount++;
				char tmp[6];
				sprintf(tmp,"@b%c: ",'A'+i);
				b->add(((tmp+lp.fileName).c_str()));
				std::stringstream ss;
				ss << "@i@s\tScale:" << lp.scale << "% (" << (lp.filterType==0?"linear filter)":"bilinear filter)");
				ss << " | Range:"<<lp.fromFrame<<"-"<<lp.toFrame<< " | channel: "<<lp.channelName;
				if (lp.crop)
				{
					ss<<" | crop: true (" <<lp.aoi.x<<","<<lp.aoi.y<<","<<lp.aoi.w<<","<<lp.aoi.h;
				}
				else
				{
					ss<<" | crop: false"; 
				}
				switch(lp.compressed)
				{
				case GFC_8BPC:
					ss << " | Format: 8bpc";
					break;
				case GFC_4BPC:
					ss << " | Format: 4bpc";
					break;
				case GFC_16BPC:
					ss << " | Format: 16bpc";
				    break;
				case GFC_16HALF:
					ss << " | Format: HALF";
				    break;
				case GFC_S3TCDX1:
					ss << " | Format: S3TC";
					break;
				default:
				    break;
				}

				b->add(ss.str().c_str());
			}
		
		}
		
		b->size(b->w(),rowCount*15*2+15);
		b->callback((Fl_Callback*)playlistItemCB, (void*)(this->pliCounter));
		
		/*Fl_Pack* horizontalPacker = new Fl_Pack(0, 0, 10, max(b->h(),0)); 
		horizontalPacker->type(Fl_Pack::HORIZONTAL);
		horizontalPacker->spacing(0);

		horizontalPacker->end();
		horizontalPacker->add(generalControlPack); //put the controls under the item. 
		horizontalPacker->add(b);
		generalAndControlPacker->add(horizontalPacker);*/
		generalAndControlPacker->add(b);
	}
	



	generalAndControlPacker->init_sizes();
	return generalAndControlPacker;
}

void PlaylistWindow::scheduleWindowUpdate()
{
	windowUpdateScheduled=1;
}

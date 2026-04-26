#include "UICallbacks.h"
#include <FL/Fl_Browser.H>
#include "stdio.h"
#include "ui/IEventSystem.h"
#include "ui/IApplication.h"
namespace { jefe::ui::IEventSystem& evt() { return jefe::ui::IEventSystem::instance(); } }
namespace { jefe::ui::IApplication& app() { return jefe::ui::IApplication::instance(); } }
#include "mainWindow.h"
#include "loadWindow.h"
#include "lutWindow.h"
#include "fxWindow.h"
#include "fxcontrolwindow.h"
#include "playlistwindow.h"
#include "gfcSequence.h"
#include "preferencesWindow.h"
#include "gfcTextRenderer.h"
#include "renderWindow.h"
#include "remoteWindow.h"
#include "drawingToolsWindow.h"
#include "gfcreview.h"

//#include "network.h"
//#include <Fl/Fl_Native_File_Chooser.h>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Color_Chooser.H>
#include <FL/Fl_Input.H>
#include "aboutWindow.h"
#include "exrWindow.h"
#include "xmlParser.h"

#include <FL/Fl_Pixmap.H>
#include "trilerp.h"
#include "gfcfx.h"
#include "gammaWindow.h"
#include <math.h>
#include <iostream>
#include <fstream>
#include <algorithm>

#include "moreOptionWindow.h"
#ifdef __APPLE__
//#include </Developer/Headers/FlatCarbon/CGDirectDisplay.h>
#endif
ExrWindow ew ( 100, 100, 200, 200,"ExrWindow" );
extern MainWindow mw;
extern LoadWindow lw;
extern LutWindow lutw;
extern FXWindow fxw;
extern FXControlWindow fxControlWindow1,fxControlWindow2,fxControlWindow3,fxControlWindow4;
extern PreferencesWindow pw;
extern RenderWindow rw;
extern RemoteWindow rmw;
extern PlaylistWindow plw;
extern DrawingToolsWindow dtw;
GammaWindow gw ( 0,0,100,100,"Gamma Window" );
extern bool rotateActive;
#include "gfcfilechooser.h"
extern NativeFileChooser *fc;
extern bool gLoadCanceled;
extern bool quitNow;
extern void startLoadingThreadA();
extern bool npotTextures;


extern int TEST_GLOBAL_loaderToUse;

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

#include "gfcpickmanager.h"
extern gfcPickManager pickManager;

#include "gfcplaybackmanager.h"
extern gfcPlaybackManager playbackManager;

#include "gfcplaylistmanager.h"
extern gfcPlaylistManager playlistManager;

#include "gfcfxmanager.h"
extern gfcFXManager fxManager;

#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;

#include "gfcmemorymanager.h"
extern gfcMemoryManager memoryManager;

#include "gfcsessionmanager.h"
extern gfcSessionManager sessionManager;

#include "minSpecsWindow.h"
extern MinSpecsWindow reqW;

#include "gfcsessionmanager.h"
extern gfcSessionManager sessionManager;

#include "gfctrackmanager.h"
extern gfcTrackManager trackManager;

extern bool gConnected;
extern bool gIsServer;
extern std::string gChatTextString;



extern bool dragging;
extern bool zooming;

//std::vector<CubeLUT> lutArray; //contains loaded LUTs
//std::map<Fl_Widget*, lutParamInfo> widgetToLUTs; //tells which widgets affect what LUT
//std::map<Fl_Widget*, fxLoaderParamInfo> widgetToFXs; //tells which widgets affect what FX

std::vector<gfcFX> fxArray;
std::vector<int> fxArrayActiveCount;
std::vector<gfcFX> fxApplied[4]; // this are the applied fx for each quadrant, they can change order and are added when the correponding button on the FX window is pressed.
std::map<std::string, int> fxHashMap; //stores the position of each fx in the fxArray based on the md5Hash of each fx.
std::map<std::string, int> lutHashMap; //stores the position of each LUT in the lutArray based on the md5Hash of each LUT.
int numberOfActiveEffects[4]={0,0,0,0}
; //if this amount is zero, then we don't have to draw to the FBO.

gfcReview gReview; //the global current review object.

moreOptionsPopup moPopup ( 0,0,0,0,"More" );
int fullscreenActive=0;
int fsX,fsY,fsW,fsH;

Fl_File_Browser fb ( 100,100,100,100,"Choose a Directory" );
AboutWindow aw ( 0,0,100,100,"About JefeCheck" );
GLint gFilteringModeMin=GL_NEAREST;
GLint gFilteringModeMag=GL_NEAREST;
#ifdef WIN32
WORD m_RampSaved[256*3];
#endif
#ifdef __APPLE__
/*
static CGGammaValue redMin;
static CGGammaValue redMax;
static CGGammaValue redGamma;
static     CGGammaValue greenMin;
static     CGGammaValue greenMax;
static     CGGammaValue greenGamma;
static     CGGammaValue blueMin;
static     CGGammaValue blueMax;
static     CGGammaValue blueGamma;

CGDisplayErr macError;*/
#endif
char gFilename[2048]=" ";
float gSavedGamma=1;
bool originalGammaExists=false;

/*std::thread* threadA;
std::thread* threadB;
std::thread* threadC;
std::thread* threadD;

std::thread* sequenceThreads[GFC_NUM_OF_SEQUENCES];

std::thread* startThreadA();
std::thread* startThreadB();
std::thread* startThreadC();
std::thread* startThreadD();*/
gfcPlate* getPlateFromWidget ( Fl_Widget* o, void *v );

void lutCBFillLoadedScroll();
void fxCBFillLoadedScroll();

void toggleFullscreen();

/**********
Auxiliary Functions to handle some aspects of the GUI
that don't really have too much to do with any other
part of the logic
**********/



//ENDOF AUXILIARY FUNCTIONS***/



void save_input_file ( Fl_File_Chooser *w, void *userdata ) {
	//printf ( "File selected: %s\n",w->value() );
	strcpy ( gFilename,w->value() );
}
/*
void toggleHideControls(int force=-1) {
static bool hidden=false;
static int vpPrevX,vpPrevY,vpPrevW, vpPrevH;
static int cbGroupHeight=-1;
static int menuBarHeight=-1;

printf("toggle hide!\n");

//only run the first time
if (cbGroupHeight==-1 && menuBarHeight==-1)
{
cbGroupHeight=mw.controlBarGroup->h();
menuBarHeight=mw.menuBar->h();
}

if (hidden || force==1) { //show the Controls


mw.menuBar->set_visible();
mw.controlBarGroup->set_visible();

//mw.controlBarGroup->resize(0,mw.mainWindow->h(),mw.mainWindow->w(),cbGroupHeight);

//mw.vp->resize(0,mw.menuBar->h(),mw.mainWindow->w(),mw.mainWindow->h()-mw.menuBar->h()-mw.controlBarGroup->h());
mw.vp->resize(0,menuBarHeight,mw.mainWindow->w(),mw.mainWindow->h()-menuBarHeight-cbGroupHeight);


//mw.vp->resize ( vpPrevX,vpPrevY,vpPrevW,vpPrevH );


mw.controlBarGroup->redraw();
Fl::redraw();
} else { //hide the controls

vpPrevX=mw.vp->x();
vpPrevY=mw.vp->y();
vpPrevW=mw.vp->w();
vpPrevH=mw.vp->h();

mw.menuBar->hide();
mw.controlBarGroup->hide();

//not cool for multi monitor //mw.vp->resize ( 0,0,glutGet ( GLUT_SCREEN_WIDTH ),glutGet ( GLUT_SCREEN_HEIGHT ) );

mw.vp->resize ( 0,0,mw.mainWindow->w(),mw.mainWindow->h());
}

hidden=!hidden;
}*/


void gammaCB ( Fl_Widget* o, void* v ) {
	// #ifdef WIN32
	//     WORD ramp[256*3];
	// #endif
	//
	//     float Gamma=1.0;
	//     char gammaComand[300];
	//
	//
	//     switch ( ( long ) v ) {
	//     case GAMMABYCOLOR_ID:
	//         if ( gw.byColorCheckBox->value() ) {
	//             gw.gammaBar->deactivate();
	//             gw.gammaRBar->activate();
	//             gw.gammaGBar->activate();
	//             gw.gammaBBar->activate();
	//         } else {
	//             gw.gammaBar->activate();
	//             gw.gammaRBar->deactivate();
	//             gw.gammaGBar->deactivate();
	//             gw.gammaBBar->deactivate();
	//         }
	//         break;
	//     case GAMMAGLOBAL_ID:
	//
	//
	// #ifdef WIN32
	//
	//         Gamma=1.0/gw.gammaBar->value();
	//         for ( int i=0; i<256; i++ ) {
	//             ramp[i+0] = ramp[i+256] = ramp[i+512] =
	//                                           ( WORD ) min ( 65535, max ( 0, pow ( ( float ) ( ( i+1 ) / 256.0 ), ( float ) Gamma ) * 65535 + 0.5 ) );
	//         }
	//         SetDeviceGammaRamp ( ::GetDC ( NULL ), ramp );
	// #endif
	//
	// #ifdef linux
	//
	//         Gamma=gw.gammaBar->value();
	//         if ( gw.byColorCheckBox->value() ) {
	//             sprintf ( gammaComand,"xgamma -rgamma %f.2 -ggamma %f.2 -bgamma %f.2",gw.gammaRBar->value(),gw.gammaGBar->value(),gw.gammaBBar->value() );
	//         } else {
	//             gw.gammaRBar->value ( gw.gammaBar->value() );
	//             gw.gammaGBar->value ( gw.gammaBar->value() );
	//             gw.gammaBBar->value ( gw.gammaBar->value() );
	//             sprintf ( gammaComand,"xgamma -gamma %f.2",Gamma );
	//         }
	//         system ( gammaComand );
	// #endif
	//
	//
	// #ifdef __APPLE__
	//
	// /*
	//         macError=CGGetDisplayTransferByFormula ( kCGDirectMainDisplay,
	//
	//                  &redMin, &redMax, &redGamma, //min red, max red, red value
	//
	//                  &greenMin, &greenMax, &greenGamma, //min green, max green, green value
	//
	//                  &blueMin, &blueMax, &blueGamma ); //min blue, max blue, blue value
	//         printf ( "redGamma: %f\n", redGamma );
	//         printf ( "Error code: %i\n",macError );
	//
	//         if ( !gw.byColorCheckBox->value() ) {
	//             Gamma=1.0/gw.gammaBar->value();
	//             gw.gammaRBar->value ( gw.gammaBar->value() );
	//             gw.gammaGBar->value ( gw.gammaBar->value() );
	//             gw.gammaBBar->value ( gw.gammaBar->value() );
	//             macError=CGSetDisplayTransferByFormula ( kCGDirectMainDisplay,
	//
	//                      redMin, redMax, Gamma*redGamma, //min red, max red, red value
	//
	//                      greenMin, greenMax, Gamma*greenGamma, //min green, max green, green value
	//
	//                      blueMin, blueMax, Gamma*blueGamma ); //min blue, max blue, blue value
	//         } else {
	//             macError=CGSetDisplayTransferByFormula ( kCGDirectMainDisplay,
	//
	//                      redMin, redMax, 1.0/gw.gammaRBar->value() *redGamma, //min red, max red, red value
	//
	//                      greenMin, greenMax, 1.0/gw.gammaGBar->value() *greenGamma, //min green, max green, green value
	//
	//                      blueMin, blueMax, 1.0/gw.gammaBBar->value() *blueGamma ); //min blue, max blue, blue value
	//         }
	//         printf ( "Gamma: %f\n", Gamma );
	//         printf ( "Error code: %i\n",macError );
	// #endif
	//
	//         break;
	//     case GAMMADONE_ID:
	//         gw.gammaWindow->hide();
	//         break;*/
	//     }
}

void moreOptionsCB ( Fl_Widget* o , void* v ) {

	switch ( ( long ) v ) {

	case MOCLOSE_ID:
		moPopup.moreOptions->hide();
		break;

	case MOUNLOADTRACK_ID:

		trackManager.getSequence(moPopup.ID-'A')->clearSequence();

		break;
	case MOHOLDFRAME_ID: {
		trackManager.getSequence(moPopup.ID-'A')->setHoldMode(( long ) ( ( Fl_Choice* ) o )->value(), playbackManager.getCurrentFrame());


						 }
						 break;

	case MOFRAMEOFFSET_ID:
		trackManager.getSequence(moPopup.ID-'A')->setOffset(( long ) ( ( Fl_Value_Input* ) o )->value());
		//gNetworkOtherEvent=true;

		break;

	default:
		printf ( "Unhandled MORE OPTIONS Callback: %i\n", ( long ) v );
		break;
	}
}


//extern Fl_RGB_Image image_tinyLogo;
//extern Fl_RGB_Image image_ollin_foot_rojo_gray_tiny;

void tracksBarCB(Fl_Widget* o , void* v) {
	//    printf("TrackBar event for %c\n",( long )v+'A');
	static int prevX=0;
	static int dragging=false;
	static int dragAmount=0;
	static int whatTrackDragging=-1;
	switch (evt().currentEventType()) {
	case jefe::ui::EventType::Push: {
		prevX=evt().mouseX();
		if (evt().isMouseButtonDown(jefe::ui::MouseButton::Left)) {

			if (evt().isAlt()) {
				printf("clicked on %i\n",((TrackWidget*)o)->getClickedFrame());
				trackManager.startLoadingSequenceAt(long(v),((TrackWidget*)o)->getClickedFrame());
			}

			dragging=true;
			whatTrackDragging=( long )v;
		}

		if (evt().isMouseButtonDown(jefe::ui::MouseButton::Right)) {
			moPopup.ID=(long)v+'A';
			moPopup.frameOffset->value (trackManager.getSequence(long(v))->getOffset());
			moPopup.holdFrame->value((trackManager.getSequence(long(v))->getHoldMode()));
			moPopup.bgBox->label ( &moPopup.ID );
			moPopup.popup();
			moPopup.moreOptions->redraw();
			dragging=false;
		}
				  }
				  break;


	case jefe::ui::EventType::Release:
		dragging=false;
		dragAmount=0;
		break;

	case jefe::ui::EventType::Drag:
		if (dragging) {
			dragAmount+=evt().mouseX()-prevX;
			float dragLimit=playbackManager.getGUIFrameSize();
			//printf("DragLimit=%i\n",dragLimit);
			if (dragAmount>dragLimit || dragAmount<-dragLimit) {
				trackManager.getSequence(whatTrackDragging)->setOffset(trackManager.getSequence(whatTrackDragging)->getOffset()+dragAmount/dragLimit);
				dragAmount=0;
				if (moPopup.ID=='A'+whatTrackDragging) { //if the popup corresponds to the one we drag, update it's offset value
					moPopup.frameOffset->value (trackManager.getSequence(whatTrackDragging)->getOffset());
					moPopup.moreOptions->redraw();
					plateManager.setChanged();
				}

			}

			prevX=evt().mouseX();

		}
		break;

	case jefe::ui::EventType::Paste: {
		std::string pastedText=evt().currentText().c_str();
		std::cout<<"pasted text: "<<GetFilenameNoFilePrefix(RemoveNewLine(pastedText))<<std::endl<<"nextLine"<<std::endl;

		//printf("\nDropped %s into track\n",RemoveNewLine(GetFilenameNoFilePrefix(evt().currentText().c_str())).c_str());
		int trackID=trackManager.getTrackIDfromWidget(o);
		if (trackID!=-1) {
			/*if(strcmp(evt().currentText().c_str(),"") && strcmp(evt().currentText().c_str()," "))*/{
				//#ifdef linux
				trackManager.getSequence(trackID)->myGUI->setFilename(getFirstSequenceInDirectory(GetFilenameNoFilePrefix(RemoveNewLine(pastedText))));
				if (evt().isShift() || evt().isShift()) {
					trackManager.getSequence(trackID)->myGUI->setScale("50");
				} else {
					trackManager.getSequence(trackID)->myGUI->setScale("100");
				}
				//#else
				//trackManager.getSequence(trackID)->myGUI->setFilename(evt().currentText().c_str());
				//#endif

			}

			trackManager.loadPreviewFrame(trackID);

			if (!lw.loadWindow->visible()) {
				printf("we want to start!\n");
				trackManager.startLoadingSequence(trackID);
			}

		}
				   }

	default:

		break;
	}

}

void controlBarCB ( Fl_Widget* o , void* v ) {
	static bool testBool=0;

	switch ( ( long ) v ) {

	case MENUHELPABOUT_ID:
		//call the menu callback for this event...
		menuCB(0,(void*)MENUHELPABOUT_ID);
		break;

	case ABOUTWINDOWCLOSE_ID:
		aw.aboutWindow->hide();
		break;

	case PLATEFX_ID: {


		fxControlWindow1.theWindow->show();

					 }
					 break;

	case ADDTOPLAYLIST_ID:{
		playlistManager.addItemlist(trackManager.getPlaylistItem());
		if (plw.theWindow->visible()) {
			plw.scheduleWindowUpdate();
		}
						  }

	case CBMOREOPTIONS_ID: {

		int trackID=trackManager.getTrackIDfromWidget(o);
		if (trackID!=-1) {
			moPopup.ID=trackID+'A';
			moPopup.frameOffset->value (trackManager.getSequence(trackID)->getOffset());
			moPopup.bgBox->label ( &moPopup.ID );
			moPopup.popup();
			moPopup.moreOptions->redraw();
		}
						   }
						   break;


	case MENUFILEPLAYLIST_ID:
		/*
		* PlaylistWindow
		*/
		plw.theWindow->show();
		plw.updateWindow();


		break;

	case MENUREMOTEMANAGER_ID:
		rmw.remoteWindow->show();
		break;

	case MENUFILERENDER_ID:
		rw.renderWindow->show();
		break;


	case PLATECONTROLS_ID:
		plateManager.updateAllFromGUI();
		break;

	case PLATECONTROLSCOLOR_ID:
		plateManager.updateColorCorrectionsFromGUI();
		break;

	case PLATECONTROLSTRANSFORMATIONS_ID:
		plateManager.updateTransformationsFromGUI();
		break;


	case TARGETFPS_ID:
		//mw.vp->targetFPS=atoi ( mw.targetFPSInput->value() );
		playbackManager.setTargetFPS();
		//tmpCount=0;
		//printf ( "Target FPS: %i\n",mw.vp->targetFPS );

		break;

	case TIMELINE_ID:


		playbackManager.updateTimelineValueFromGUI();
		playbackManager.updateInOutFromGUI();
		if (evt().isAlt() && evt().currentEventType()==jefe::ui::EventType::Push) {
			printf("Load all tracks from this point on %i!\n",playbackManager.getCurrentFrame());
			trackManager.startLoadingAllAt(playbackManager.getCurrentFrame()-1); //this functions expects frames that start at 0
		}
		break;

	case TIMELINEINPUT_ID:
		playbackManager.updateCurrentFrameValueFromGUI();

		//mw.timeLineInput->maximum(mw.timeLine->maximum());

		//gNetworkPlayEvent=true;
		break;

	case LOOPPRIORITY_ID:
		playbackManager.updateLoopPriorityFromGUI();
		////Fl::focus ( mw.vp );
		break;

	case PLAYFWD_ID:
		playbackManager.startPlayFwd();
		//Fl::focus ( mw.vp );
		break;

	case PLAYREV_ID:
		playbackManager.startPlayRev();
		//Fl::focus ( mw.vp );
		break;

	case PLAYPAUSE_ID:
		playbackManager.pause();
		//Fl::focus ( mw.vp );
		break;

	case REWIND_ID:
		playbackManager.rew();
		//Fl::focus ( mw.vp );
		break;

	case FF_ID:
		playbackManager.ffwd();
		//Fl::focus ( mw.vp );
		break;

	case BACKONE_ID:
		playbackManager.oneFrameRev();
		//Fl::focus ( mw.vp );
		break;

	case FORWARDONE_ID:
		playbackManager.oneFrameFwd();
		//Fl::focus ( mw.vp );
		break;

	case ABORTA_ID:
		trackManager.stopLoadingSequence(0);
		//Fl::focus ( mw.vp );
		break;

	case ABORTB_ID:
		trackManager.stopLoadingSequence(1);
		//Fl::focus ( mw.vp );
		break;

	case ABORTC_ID:
		trackManager.stopLoadingSequence(2);
		//Fl::focus ( mw.vp );
		break;

	case ABORTD_ID:
		trackManager.stopLoadingSequence(3);
		//Fl::focus ( mw.vp );
		break;

	case FRAMINGSINGLE_ID:
	case FRAMINGDOUBLE_ID:
	case FRAMINGDOUBLEVERT_ID:
	case FRAMINGQUAD_ID:
		plateManager.setFramingMode(( long )v);
		mw.vp->invalidate();
		//Fl::focus ( mw.vp );
		break;

	case LOOPMODEBOUNCE_ID:
	case LOOPMODEONCE_ID:
	case LOOPMODELOOP_ID:
		playbackManager.setPlaybackMode(( long )v);
		//Fl::focus ( mw.vp );
		break;

	case PLAYFROM_ID:
	case PLAYTO_ID:
		playbackManager.updateToFromFromGUI();

		////Fl::focus ( mw.vp );
		break;

	case INPOINT_ID:
	case OUTPOINT_ID:

		playbackManager.updateInOutFromGUI();

		////Fl::focus ( mw.vp );
		break;


	case CHANNELMASKR_ID:
	case CHANNELMASKG_ID: //fall through
	case CHANNELMASKB_ID: //fall through
	case CHANNELMASKA_ID: //fall through

		//gNetworkOtherEvent=true;
		break;


	default:

		printf ( "Unmanaged controlBar callback! ID: %i\n", ( long ) v );

		break;

	}

	////Fl::focus ( mw.vp );
#ifndef __APPLE__
	app().processEvents();
#endif

	Fl::redraw();

}


void updateReviewToolsWindowReview();
void menuCB ( Fl_Menu_* o , void* v ) {

	float newSize;
	Fl_Widget* temp;
	Fl_File_Chooser chooser ( "./","*",0,"Pick a file" );
	static char gammaCommand[300];

	switch ( ( long ) v ) {

	case MENUREVIEWTOOLBAR_ID:
		dtw.drawingToolsWindow->show();
		updateReviewToolsWindowReview();
		break;

	case MENUREMOTESAVECHAT_ID:

		fc->callback ( save_input_file );
		fc->preview ( 0 );
		fc->filter ( "Text File (*.txt)" );
		fc->label ( "Select or Create a File to Save Chat Log to" );
		fc->type ( Fl_File_Chooser::CREATE );
		//printf ( "FileChooser Type is %i\n",fc->type() );
		sprintf ( gFilename,"" );
		fc->show();
		while ( fc->shown() )
			app().waitForEvents();

		if (fc->count())
			networkManager.saveChatLog(fc->value(0));

		break;

	case MENUREMOTEMANAGER_ID:
		rmw.remoteWindow->show();
		break;
	case MENUREMOTEPASTE_ID:
		Fl::paste ( *mw.vp );
		break;
	case MENUFILERENDER_ID:
		rw.renderWindow->show();
		break;

	case MENUPLUGINLOADER_ID:
		fxw.fxWindow->show();
		break;

	case MENULUTLOADER_ID:
		lutw.lutWindow->show();
		lutCBFillLoadedScroll();
		break;


	case MENUFILESAVESESSION_ID: {
		fc->callback ( save_input_file );
		fc->preview ( 0 );
		fc->filter ( "JefeCheck Session (*.jcs)" );
		fc->label ( "Select or Create a File" );
		fc->type ( Fl_File_Chooser::CREATE );
		printf ( "FileChooser Type is %i\n",fc->type() );
		fc->show();
		while ( fc->shown() )
			app().waitForEvents();

		if (fc->count()) {
			sessionManager.saveSession ( fc->value(0));
		}
								 }
								 break;

	case MENUFILEOPENSESSION_ID: {
		fc->callback ( save_input_file );
		fc->preview ( 0 );
		fc->filter ( "JefeCheck Session (*.jcs)" );
		fc->label ( "Select a Session File" );
		fc->type ( Fl_File_Chooser::SINGLE );
		printf ( "FileChooser Type is %i\n",fc->type() );
		fc->show();
		while ( fc->shown() )
			app().waitForEvents();

		if (fc->count()) {
			std::string tmpStringName=fc->value(0);
			sessionManager.loadSession ( tmpStringName );
		}


								 }
								 break;

	case MENUFILEOPENRECENTSESSION_ID: {
		std::string tmpStringName=( ( Fl_Menu_* ) o )->text();
		RemoveMenuSlash(tmpStringName);
		sessionManager.loadSession ( tmpStringName );
									   }
									   break;



	case MENUFILEPREFERENCES_ID:

		pw.preferencesWindow->show();

		break;
	case MENUGAMMA_ID:

		if ( !originalGammaExists ) {
#ifdef WIN32
			if ( !GetDeviceGammaRamp ( ::GetDC ( NULL ), m_RampSaved ) ) {
				printf ( "WARNING: Cannot initialize DeviceGammaRamp.\n" );
			}
#endif
#ifdef linux
			gSavedGamma=1; //TO DO, SAVE GAMMA FROM XGAMMA
#endif

			originalGammaExists=true;
		}
		{
			int sx, sy, sw, sh;
			Fl::screen_xywh(sx, sy, sw, sh, 0);
			gw.gammaWindow->position(sx + sw/2 - gw.gammaWindow->w()/2,
			                         sy + sh/2 - gw.gammaWindow->h()/2);
		}
#ifdef linux

		gw.gammaBar->when ( FL_WHEN_RELEASE );
#endif

		gw.gammaWindow->show();
		break;


	case FILTERINGMAGLINEAR_ID:
		sett.filterMin=GL_NEAREST;
		sett.filterMax=GL_NEAREST;
		printf("filterMin set to GL_NEAREST (%i)\n",GL_NEAREST);
		break;

	case FILTERINGMAGBILINEAR_ID:
		sett.filterMin=GL_LINEAR;
		sett.filterMax=GL_LINEAR;
		printf("filterMin set to GL_LINEAR (%i)\n",GL_LINEAR);
		break;

	case MENUASPECTOPACITY100_ID:
		sett.aspectBarsOpacity=1.0;
		break;

	case MENUASPECTOPACITY75_ID:
		sett.aspectBarsOpacity=0.75;
		break;

	case MENUASPECTOPACITY50_ID:
		sett.aspectBarsOpacity=0.5;
		break;

	case MENUASPECTOPACITY25_ID:
		sett.aspectBarsOpacity=0.25;
		break;


	case MENUFILELOAD_ID:
		/*
		* OPEN LOADING WINDOW
		*Load into track acording to what the loading window says.
		*/

		lw.loadWindow->show();
		plateManager.updateAllFromGUI();


		break;

	case MENUFILEPLAYLIST_ID:
		/*
		* PlaylistWindow
		*/
		plw.theWindow->show();
		plw.updateWindow();


		break;

	case MENUFILEEXIT_ID:
		//rotateActive=!rotateActive;
		//printf("rotate: %i\n",rotateActive);
		//return gamma to what it was before
		/*if (!GetDeviceGammaRamp(::GetDC(NULL), m_RampSaved))
		{
		printf("WARNING: Cannot initialize DeviceGammaRamp.\n");
		}*/

		quitNow=confirmQuit();

		break;

	case MENURESETXFORMALL_ID:

		plateManager.resetAllPlates();

		break;

	case FRAMINGSINGLE_ID:
	case FRAMINGDOUBLE_ID:
	case FRAMINGQUAD_ID:
	case FRAMINGDOUBLEVERT_ID:
	case FRAMINGOVERLAY_ID:
	case FRAMINGSTEREO_ID:
		controlBarCB ( temp,v );
		break;
	

	case MENUSAVECCFAVORITE1_ID:
	case MENUSAVECCFAVORITE2_ID:
	case MENUSAVECCFAVORITE3_ID:
	case MENUSAVECCFAVORITE4_ID:
	case MENUSAVECCFAVORITE5_ID:
		plateManager.saveFavoriteColorCorrectionFromPlate((long)v-MENUSAVECCFAVORITE1_ID);
		break;

	case MENULOADCCFAVORITE1_ID:
	case MENULOADCCFAVORITE2_ID:
	case MENULOADCCFAVORITE3_ID:
	case MENULOADCCFAVORITE4_ID:
	case MENULOADCCFAVORITE5_ID:
		plateManager.setFavoriteColorCorrectionOnPlate((long)v-MENULOADCCFAVORITE1_ID);
		break;

	case MENUHIDECONTROLS_ID: {
		mw.toggleHideControls();
							  }
							  break;

	case MENUFULLSCREEN_ID:

		mw.toggleFullscreen();

		/*
		switch ( fullscreenActive ) {
		case 0:
		fsX=mw.mainWindow->x();
		fsY=mw.mainWindow->y();
		fsW=mw.mainWindow->w();
		fsH=mw.mainWindow->h();

		mw.mainWindow->fullscreen();
		fullscreenActive=1;


		break;
		case 1:

		mw.mainWindow->fullscreen();
		fullscreenActive=2;


		break;
		case 2:

		mw.mainWindow->fullscreen_off ( fsX,fsY,fsW,fsH );
		fullscreenActive=0;

		break;
		}
		*/


		break;

	case MENUHELPONLINESUPPORT_ID:
		fl_open_uri("http://jefecheck.jefecorp.com/support.html");
		break;


	case MENUHELPVIDEOTUTORIALS_ID:

		fl_open_uri("http://jefecheck.jefecorp.com/videotutorials.html");
		break;

	case MENUHELPUSERGUIDE_ID:{
		char message[1024];
		std::filesystem::path userGuidePath(getApplicationDataPath());
		std::cout<<"Application Data Path: "<< userGuidePath.string()<<std::endl;
		userGuidePath=userGuidePath/"JefeCheckManual.pdf";
		if (std::filesystem::exists(userGuidePath))
		{
			std::string completePath=std::string("file://")+userGuidePath.string();
			fl_open_uri(completePath.c_str(),message,1024);
		}
		else
		{
			fl_alert("Could not find the User Guide, please make sure your installation is correct or visit www.jefecheck.com for the latest userguide");
		}

							  }
							  break;


	case MENUHELPQUICKREFERENCE_ID:{

		char message[1024];
		std::filesystem::path userGuidePath(getApplicationDataPath());
		userGuidePath=userGuidePath/"JefeCheckQuickStart.pdf";
		if (std::filesystem::exists(userGuidePath))
		{
			std::string completePath=std::string("file://")+userGuidePath.string();
			fl_open_uri(completePath.c_str(),message,1024);
		}
		else
		{
			fl_alert("Could not find the User Guide, please make sure your installation is correct or visit www.jefecheck.com for the latest userguide");
		}
								   }
								   break;

	case MENUHELPQUICKHELP_ID:
		plateManager.toggleHelp();
		break;

	case MENUHELPREQUIREMENTSCHECK_ID:
		reqW.minSpecsWindow->show();
		break;
	case MENUHELPABOUT_ID: {


		aw.make_window();

		{
			int sx, sy, sw, sh;
			Fl::screen_xywh(sx, sy, sw, sh, 0);
			aw.aboutWindow->position(sx + sw/2 - aw.aboutWindow->w()/2,
			                         sy + sh/2 - aw.aboutWindow->h()/2);
		}


		aw.aboutWindow->position(mw.mainWindow->x()+mw.mainWindow->w()/2-aw.aboutWindow->w()/2,mw.mainWindow->y()+200);

		aw.textBrowser->add(" ");
		aw.textBrowser->add("@c@b@mJefeCheck");
		aw.textBrowser->add("@c@bby JefeCorp:");
		std::string version="@c@bversion: ";
		version+=JEFE_VERSION;

		aw.textBrowser->add(version.c_str());
		aw.textBrowser->add("@cwww.jefecheck.com");
		aw.textBrowser->add("@cwww.jefecorp.com");
		aw.textBrowser->add("");
		aw.textBrowser->add("@csupport: support@jefecheck.com");
		aw.textBrowser->add("@chttp://jefecheck.jefecorp.com/forum");
		aw.textBrowser->add("@chttp://jefecheck.jefecorp.com/helpdesk");

		aw.textBrowser->add(" ");
		aw.textBrowser->add("@c@b@mJefeCheck Credits and Acknowledgments");
		aw.textBrowser->add(" ");
		aw.textBrowser->add("@c@bSystem Design and Programming:");
		aw.textBrowser->add("@cDaniel Goll\341s Gilman");

		aw.textBrowser->add(" ");
		aw.textBrowser->add("@c@bCreative Development:");
		aw.textBrowser->add("@cDaniel Goll\xE1s Gilman");
		aw.textBrowser->add("@cCharlie Iturriaga");

		aw.textBrowser->add(" ");
		aw.textBrowser->add("@c@bGraphic Design Advisory:");
		aw.textBrowser->add("@cOctavio L\xF3pez Sierra");
		aw.textBrowser->add("@cAna Gabriela Garc\xED\x61 Reyes");


		aw.textBrowser->add(" ");
		aw.textBrowser->add("@c@bBeta Testers:");
		aw.textBrowser->add("@cRa\xFAl Prado");
		aw.textBrowser->add("@cAdriana Arriaga");
		aw.textBrowser->add("@cMevlana Rumi Alva");
		aw.textBrowser->add("@cJorge Mendoza");
		aw.textBrowser->add("@cNatalia de la Garza");
		aw.textBrowser->add("@cCharlie Iturriaga");
		aw.textBrowser->add("@cFernando N\341jera");
		aw.textBrowser->add("@cDavid Chiu");
		aw.textBrowser->add("@cJose Fernandez");
		aw.textBrowser->add("@cThorsten Wolf");
		aw.textBrowser->add("@cMike Villasana");
		aw.textBrowser->add("@cJulio Galv\341n");
		aw.textBrowser->add("@cTodd Daugherty");

		aw.textBrowser->add(" ");
		aw.textBrowser->add("@c@bSpecial Thanks to:");
		aw.textBrowser->add("@cOllin Studio");
		//aw.textBrowser->add("@cAlejandro Diego");
		aw.textBrowser->add("@cThe VES Festival Committee");
		aw.textBrowser->add("@cOctavio L\xF3pez Sierra");
		aw.textBrowser->add("@cRa\xFAl Prado");
		aw.textBrowser->add("@cMike Villasana");
		aw.textBrowser->add("@cTodd Daugherty and EntityFX");
		aw.textBrowser->add("@cNeil Smith and Hollywood D.I.");
		aw.textBrowser->add("@cGreg Ercolano");
		aw.textBrowser->add("@cFLTK and the FLTK community");
		aw.textBrowser->add("@cPierre Gougelet and GFLSdk");
		aw.textBrowser->add("@cRakkarsoft and Raknet");
		aw.textBrowser->add("@cBob Friesenhahn");
		aw.textBrowser->add("@cGabriel Foster");

		aw.closeButton->set_visible();
		char versionString[40];
		sprintf(versionString,"v.%s",JEFE_VERSION);
		printf("versionString: %s\n",versionString);
		aw.versionLabel->copy_label(versionString);
		aw.aboutWindow->show();
						   }
						   break;


	case MENURESETXFORM_ID:

		plateManager.resetPlate(plateManager.getActiveQuad());

		break;

	case MENURESETCC_ID:
		plateManager.resetColorCorrection(plateManager.getActiveQuad());
		break;

	case MENURESETCCALL_ID:
		plateManager.resetAllColorCorrections();
		break;

	case MENUFITTOVIEWPORT_ID:
		plateManager.fitToViewport(plateManager.getActiveQuad());
		break;

	case MENUFITALLTOVIEWPORT_ID:
		plateManager.fitToViewportAll();
		break;

	case MENUFLIP_ID:
		plateManager.toggleFlip(plateManager.getActiveQuad());
		break;

	case MENUFLOP_ID:
		plateManager.toggleFlop(plateManager.getActiveQuad());
		break;

	case MENUFLIPALL_ID:
		plateManager.toggleFlipAll();
		break;

	case MENUFLOPALL_ID:
		plateManager.toggleFlopAll();
		break;

	case MENUHISTOGRAM_ID:
		plateManager.toggleHistogramMode(plateManager.getActiveQuad());

	default:

		printf ( "Unmanaged menu callback! ID: %i\n", ( long ) v );

		break;

	}

}

void remoteCB ( Fl_Widget* o , void* v ) {

	switch ( ( long ) v ) {

	case REMOTE_PREFERENCES_ID:{
		pw.preferencesWindow->show();
		pw.sectionList->value(5); pw.showPanel(4);
							   }
							   break;

	case REMOTE_RECENT_ID: {
		//printf("selected: %s\n",( ( Fl_Menu_* ) o )->text());

		std::string theIP, thePort, theMash=( ( Fl_Menu_* ) o )->text();

		if (theMash.find_last_of(":")!=std::string::npos) {
			theIP=theMash.substr(0,theMash.find_last_of(":"));
			thePort=theMash.substr(theMash.find_last_of(":")+1);
		}


		printf("theIP: %s\nthePort: %s\n",theIP.c_str(),thePort.c_str());

		networkManager.setClientAddress(theIP,thePort);

						   }
						   break;

	case REMOTE_CONNECT_ID: {
		if (networkManager.getIsServer()) {
			fl_alert ( "Can't connect as client when acting as server!" );
			return;
		}
		networkManager.startConnection();
							}
							break;

	case REMOTE_START_ID:
		//printf("Start server...\n");
		{
			if (!networkManager.getIsServer()) {
				networkManager.startServer();
			} else {
				networkManager.stopServer();
			}

		}
		break;

	case REMOTE_SELECTCOLOR_ID:
		{
			//TODO: eventually show picker dialog and save the color to settings.
			sett.remotePointerColor=rmw.remotePointerColor->value();
			rmw.remotePointerColorSample->color(Fl_Color(int(sett.remotePointerColor)));
			rmw.remotePointerColorSample->redraw();

			//also set this in the preferences window
			pw.remotePointerColor->value(sett.remotePointerColor);
			pw.remotePointerColorSample->color(Fl_Color(int(sett.remotePointerColor)));
			pw.remotePointerColorSample->redraw();

			networkManager.sendRemotePointerColor(sett.remotePointerColor);

		}
		break;

	}
}

void updateReviewToolsWindowReview() {
	//iterate through all the revisions in the review and fill the review browser.
	std::vector<gfcRevision>::iterator iter,end;
	iter = gReview.revisions.begin();
	end=gReview.revisions.end();

	//clear the revisions browser.

	dtw.revisionsTree->clear();
	dtw.revisionsTree->root_label("Revisions");
	dtw.revisionsTree->add("Default Revision");
	dtw.revisionsTree->add("Second Revision");
	for ( iter; iter!=end;iter++ ) {}
}

void updateReviewToolsWindowRevision ( gfcRevision* revision ) {}

void updateReviewToolsWindowNote ( gfcNote* note ) {}

void reviewToolsCB ( Fl_Widget* o , void* v ) {

	switch ( ( long ) v ) {
	case REVIEWTOOLSREVIEWTREE_ID: {}
								   break;
	default: {
		printf ( "Unhandled drawingToolsCB event: %i\n", ( long ) v );
			 }
			 break;

	}
}


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

int globalCB ( int e ) {
	static bool showGui=true;

	// printf("event handler!\n");

	//if ( gChatMode==0 )
	{

		switch ( e ) {


		case FL_SHORTCUT:



			if (evt().isCtrl()) {
				switch ( static_cast<int>(evt().currentKey()) ) {

					case '1':{
						if (evt().isShift())
							{
							//	printf("Save Color Settings 1\n");
								plateManager.saveFavoriteColorCorrectionFromPlate(static_cast<int>(evt().currentKey())-'1');
							} 
							else
							{
								plateManager.setFramingMode(FRAMINGSINGLE_ID);
								mw.vp->invalidate();
							}
												
						
							 }
							 return 1;
							 break;

					case '2':{
						if (evt().isShift())
						{
							//printf("Save Color Settings 2\n");
							plateManager.saveFavoriteColorCorrectionFromPlate(static_cast<int>(evt().currentKey())-'1');
						} 
						else
						{
						plateManager.setFramingMode(FRAMINGDOUBLE_ID);
						mw.vp->invalidate();
						}
							 }
							 return 1;
							 break;
					case '3':{
						if (evt().isShift())
						{
							//printf("Save Color Settings 3\n");
							plateManager.saveFavoriteColorCorrectionFromPlate(static_cast<int>(evt().currentKey())-'1');
						} 
						else
						{
						plateManager.setFramingMode(FRAMINGDOUBLEVERT_ID);
						mw.vp->invalidate();
						}
							 }
							 return 1;
							 break;
					case '4':{
						if (evt().isShift())
						{
							//printf("Save Color Settings 4\n");
							plateManager.saveFavoriteColorCorrectionFromPlate(static_cast<int>(evt().currentKey())-'1');
						} 
						else
						{
						plateManager.setFramingMode(FRAMINGQUAD_ID);
						mw.vp->invalidate();
						}
							 }
							 return 1;
							 break;
					
					case '5':
						{
							if (evt().isShift())
							{
								//printf("Save Color Settings 5\n");
								plateManager.saveFavoriteColorCorrectionFromPlate(static_cast<int>(evt().currentKey())-'1');
							} 
							else
							{
							}
							return 1;
							break;

						}
						break;

					case '8':{
						if (evt().isAlt())
						{
							plateManager.toggleFlipAll();
						}
						else
						{
							plateManager.toggleFlip(plateManager.getActiveQuad());
						}
							 }
							 return 1;
							 break;
					case '9':{
						if (evt().isAlt())
						{
							plateManager.toggleFlopAll();
						}
						else
						{
							plateManager.toggleFlop(plateManager.getActiveQuad());
						}
							 }
							 return 1;
							 break;

					}

			}
			//HERE IS WHERE WE HANDLE THE GLOBAL KEYBOARD SHORTCUTS
			//    printf("key handler %c!\n",static_cast<int>(evt().currentKey()));
			switch ( static_cast<int>(evt().currentKey()) ) {

		case FL_Escape:
			return 1;
			break;
			/*playback button shortcuts*/
		case 32: //space bar
			playbackManager.pause();
			return 1;

		case '.': //in english, this has > on top
			playbackManager.setDirection(1);
			return 1;
			break;
		case ',': //in english, this has < on top
			playbackManager.setDirection(-1);
			return 1;
			break;
		case 'c':
			playbackManager.oneFrameFwd();
			return 1;
			break;
		case 'x':
			playbackManager.oneFrameRev();
			return 1;
			break;
		case 'v':
			playbackManager.ffwd();
			return 1;
			break;
		case 'z':
			playbackManager.rew();
			return 1;
			break;

			/***********/


			/*playback modes*/
		case '8':

			playbackManager.setPlaybackMode(LOOPMODEONCE_ID);
			return 1;
			break;
		case '9':
			playbackManager.setPlaybackMode(LOOPMODELOOP_ID);
			return 1;
			break;
		case '0':
			playbackManager.setPlaybackMode(LOOPMODEBOUNCE_ID);
			return 1;
			break;
			/***********/

			/*framing modes*/
			/*case '1':
			plateManager.setFramingMode(FRAMINGSINGLE_ID);
			mw.vp->invalidate();
			break;
			case '2':
			plateManager.setFramingMode(FRAMINGDOUBLE_ID);
			mw.vp->invalidate();
			break;
			case '3':
			plateManager.setFramingMode(FRAMINGDOUBLEVERT_ID);
			mw.vp->invalidate();
			break;
			case '4':
			plateManager.setFramingMode(FRAMINGQUAD_ID);
			mw.vp->invalidate();
			break;*/
			/***************/

			/*Viewport-track selection*/
		case '1':
			if (evt().isShift())
			{
				//load color
				//printf("Load Color Settings %i\n",static_cast<int>(evt().currentKey())-'1');
				plateManager.setFavoriteColorCorrectionOnPlate(static_cast<int>(evt().currentKey())-'1');
			} 
			else
			{
				plateManager.setTrackOnPlate(plateManager.getActiveQuad(),static_cast<int>(evt().currentKey())-'1');
				//plateManager.updateAllFromGUI(); //this send the network message.
				mw.vp->invalidate();
				
			}
			break;
			
		case '2':
			if (evt().isShift())
			{
				//load color
				//printf("Load Color Settings %i\n",static_cast<int>(evt().currentKey())-'1');
				plateManager.setFavoriteColorCorrectionOnPlate(static_cast<int>(evt().currentKey())-'1');
			} 
			else
			{
			plateManager.setTrackOnPlate(plateManager.getActiveQuad(),static_cast<int>(evt().currentKey())-'1');
			//plateManager.updateAllFromGUI(); //this send the network message.
			mw.vp->invalidate();
			}
			break;
		case '3':
			if (evt().isShift())
			{
				//load color
				//printf("Load Color Settings %i\n",static_cast<int>(evt().currentKey())-'1');
				plateManager.setFavoriteColorCorrectionOnPlate(static_cast<int>(evt().currentKey())-'1');
			} 
			else
			{
			plateManager.setTrackOnPlate(plateManager.getActiveQuad(),static_cast<int>(evt().currentKey())-'1');
			//plateManager.updateAllFromGUI(); //this send the network message.
			mw.vp->invalidate();
			}
			break;
		case '4':
			if (evt().isShift())
			{
				//load color
				//printf("Load Color Settings %i\n",static_cast<int>(evt().currentKey())-'1');
				plateManager.setFavoriteColorCorrectionOnPlate(static_cast<int>(evt().currentKey())-'1');
			} 
			else
			{
			plateManager.setTrackOnPlate(plateManager.getActiveQuad(),static_cast<int>(evt().currentKey())-'1');
			//plateManager.updateAllFromGUI(); //this send the network message.
			mw.vp->invalidate();
			}
			break;
			/****************/
		 
		case '5':
			if (evt().isShift())
			{
				//load color
				//printf("Load Color Settings %i\n",static_cast<int>(evt().currentKey())-'1');
				plateManager.setFavoriteColorCorrectionOnPlate(static_cast<int>(evt().currentKey())-'1');
			} 
			else
			{
				
			}
			break;


		case 'l':
			{
				if (evt().isCtrl())
				{
					lw.loadWindow->show();
					plateManager.updateAllFromGUI();
				}


			}
			break;

			/*set in and out points with i/o*/
		case 'i':{
			if (evt().isAlt())
			{
				int currentFrame=playbackManager.getCurrentFrame();
				trackManager.startLoadingAllAt(currentFrame-1);
				playbackManager.setInPoint(currentFrame);
			}else{
				if (evt().isShift()){
					playbackManager.setInPoint(1);
				}
				else{
					playbackManager.setInPoint(playbackManager.getCurrentFrame());
				}
			}

			return 1;
				 }
		case 'o':{

			if (evt().isShift())
			{
				//playbackManager.setToFrame(trackManager.getMaxTrackLength());
				playbackManager.setOutPoint(trackManager.getMaxTrackLength());

			}else
			{
				playbackManager.setOutPoint(playbackManager.getCurrentFrame());
			}


			return 1;
				 }


				 //FX Stack Presets
		case FL_F+1 :
		case FL_F+2 :
		case FL_F+3 :
		case FL_F+4 :
		case FL_F+5 :
			{

				int whichOne=static_cast<int>(evt().currentKey())-FL_F-1;

				if(evt().isShift() && evt().isCtrl()){
					printf("SAVING FX STACK TO FAVORITE %i\n",whichOne);
					plateManager.saveFavoriteFromPlate(whichOne);
				}
				else
				{
					if(evt().isCtrl())
					{
						printf("Appending FX STACK FROM FAVORITE %i\n",whichOne);
						plateManager.appendFavoriteOnPlate(whichOne);
					}
					else{
						if(evt().isShift()){
							printf("LOADING FX STACK FROM FAVORITE %i\n",whichOne);
							plateManager.setFavoriteOnPlate(whichOne);
						}
					}
				}

				return 0;
			}
			break;

			/*plate resets*/
		case 'r': {

			//printf("reset from keyboard?\n");
			if (evt().isAlt() && evt().isCtrl()) {
				plateManager.resetAllPlates();
				return 1;
			} else {
					if(evt().isCtrl()){
						plateManager.resetPlate(plateManager.getActiveQuad());
						return 1;
					}
					else
					{
						if (evt().isShift())
						{
							if (evt().isAlt())
							{
								plateManager.resetAllColorCorrections();
								return 1;
							}
							else
							{
								plateManager.resetColorCorrection(plateManager.getActiveQuad());
								return 1;
							}

						}
						else{
							if (!evt().isAlt())
							{
								//here we should do color channel filtering
								int whatPlate=plateManager.getActiveQuad();//mw.vp->startQuad-1;
								plateManager.toggleChannelR(whatPlate);
								return 1;
							}
						}



					}
				}
			}
				break;

		case 'g':
			{
				if (!evt().isAlt() && !evt().isCtrl())
				{
					int whatPlate=plateManager.getActiveQuad();
					plateManager.toggleChannelG(whatPlate);
					return 1;
				}
			}
			break;

		case 'b':
			if (!evt().isAlt() && !evt().isCtrl()){
				int whatPlate=plateManager.getActiveQuad();
				plateManager.toggleChannelB(whatPlate);
				return 1;
			}
			break;

		case 'a':
			if (!evt().isAlt() && !evt().isCtrl()){
				int whatPlate=plateManager.getActiveQuad();
				plateManager.toggleChannelA(whatPlate);
				return 1;
			}
			break;
			/**************/
			/*Plate texts*/
			/************/
		case 't':

			if ( evt().isAlt() ) {
				plateManager.toggleTextModeAll();
			} else {
				plateManager.toggleTextMode(plateManager.getActiveQuad());
			}
			return 1;
			break;

		case 'h':
			if (evt().isCtrl()) {
				if ( evt().isAlt() ) {
					plateManager.toggleHistogramModeAll();
				} else {
					plateManager.toggleHistogramMode(plateManager.getActiveQuad());
				}
			}
			else
			{
				plateManager.toggleHelp();
			}
			return 1;
			break;

			/*************/

			/*quit*/
		case 'q': {
			if (evt().isCtrl()) {
				quitNow=confirmQuit();
			}
			return 1;
				  }
				  break;


		case 'f':
			//hide/show the gui if ctrl+alt are pressed
			if (evt().isAlt() && evt().isCtrl()) {
				//printf("hide GUI from keyboard!\n");
				mw.toggleHideControls();
			}
			else{
				if (!evt().isCtrl()) 
				{

					//fit plates to viewport
					if(evt().isAlt())
					{
						plateManager.fitToViewportAll();
					}
					else
					{
						plateManager.fitToViewport(plateManager.getActiveQuad());
					}



					mw.vp->invalidate();
				}
				else
				{
					//toggleHideControls();
					mw.toggleFullscreen();
				}

			}

			return 1;
			break;


		case 'y': {

			if ( evt().isCtrl() ) {
				if ( networkManager.chatFadeCounter>networkManager.chatOpacity )
					networkManager.chatFadeCounter=networkManager.chatOpacity;
				else
					networkManager.chatFadeCounter= networkManager.chatFadeDelay/GFCNET_CHAT_FADE_SPEED;
				return 1;
			} else {
				//printf ( "Chat mode!\n" );
				networkManager.chatFadeCounter= networkManager.chatFadeDelay/GFCNET_CHAT_FADE_SPEED;

				networkManager.gChatMode=1;
				return 1;
			}
				  }



		default:
			//printf ( "Unhandled event\n" );
			//printf ( "Unhandled key event: %c (%i)\n", static_cast<int>(evt().currentKey()),static_cast<int>(evt().currentKey()) );
			return 0;
			break;
			}
			break;


				  }
			}

			return 0;
		}


void PreferencesCB ( Fl_Widget* o , void* v ) {
			//UPDATE ALL THE OTHER PREFERENCES IN ANY CASE
			
			if (( long ) v == EXRPREFSDEFAULTSBUTTON_ID)
			{
				//reset exr conversion preferences
				pw.exrExposure->value(0.0);
				pw.exrDefog->value(0);
				pw.exrGamma->value(2.2);
				pw.exrKneeLow->value(0.0);
				pw.exrKneeHigh->value(5.0);
			}
			


			if ( pw.startFullscreenCheckBox->value() )
				sett.startFullscreen=1;
			else
				sett.startFullscreen=0;

			if ( pw.loadWindowOnStartupCheckBox->value() )
				sett.openLoadWindowAtStartup=1;
			else
				sett.openLoadWindowAtStartup=0;


			int bgColor=pw.bgColor->value();
			sett.bgColor=(bgColor/255.0);
			int bgColorOffset=-5;
			if(abs(bgColor)<=abs(bgColorOffset))
			{
				bgColorOffset*=-1;
			}

			mw.bgBox->color(fl_rgb_color(bgColor+bgColorOffset,bgColor+bgColorOffset,bgColor+bgColorOffset));
			mw.mainWindow->redraw();

			plateManager.setHistogramQuality(pw.histogramQuality->value());

			sett.enableCrashRecoverySession=pw.attemptToRecoverFromCrashCheckBox->value();

			sett.processorPriority=pw.processorPriority->value();

			memoryManager.setLimit((float)(pw.percentageOfRam->value()));
			memoryManager.countInactive=pw.dontUseInactiveMemory->value();
			trackManager.setForceGFLLoading(pw.forceGFLLoading->value());
			trackManager.setContinueLoadingOnError(pw.continueLoadingOnError->value());
			plateManager.setForceSingleBufferedFXs(pw.forceSingleBufferedFXs->value());

			sett.balanceReads=pw.balanceReads->value();

			//vsync options
			sett.vsync=pw.vsync->value();
			mw.vp->setVsync(sett.vsync);

			//max frames in queueueueueue.
			sett.maximumFramesInQueue=pw.maximumFramesInQueue->value();

			//
			sett.feedbackMessageFadeDelay=pw.ActionFeedbackFadeDelay->value();
			sett.feedbackMessageSize=pw.ActionFeedbackSize->value();
			
			//FORMAT OPTIONS
			sett.exrIgnoreDisplayWindow=pw.exrIgnoreDisplayWindow->value();
			sett.exrIgnoreHeadersAspectRatio=pw.exrIgnoreHeadersAspectRatio->value();
			sett.exrEnableExposureTransformOnLoad=pw.exrEnableExposureTransformOnLoad->value();
			sett.exrExposure=pw.exrExposure->value();
			sett.exrDefog=pw.exrDefog->value();
			sett.exrGamma=pw.exrGamma->value();
			sett.exrKneeLow=pw.exrKneeLow->value();
			sett.exrKneeHigh=pw.exrKneeHigh->value();

			//Chat options
			networkManager.chatFontSize=pw.fontSize->value();
			networkManager.chatTextBG=pw.textBG->value();
			networkManager.chatAutoFade=pw.autoFade->value();
			networkManager.chatOpacity=pw.opacity->value();
			networkManager.chatDisplayLines=pw.chatLines->value();
			networkManager.chatFadeDelay=pw.fadeDelay->value();

			//remote pointer options


			plateManager.setRemotePointerOptions(pw.remotePointerFontSize->value(),pw.remotePointerSize->value(),pw.remotePointerFade->value(),pw.remotePointerFadeDelay->value(),pw.remotePointerTrail->value(),pw.remotePointerTrailLenght->value(),pw.remotePointerColor->value());
			pw.remotePointerColorSample->color(Fl_Color(int(pw.remotePointerColor->value())));
			pw.remotePointerColorSample->redraw();

			if (sett.remotePointerColor!=pw.remotePointerColor->value())
			{
				networkManager.sendRemotePointerColor(pw.remotePointerColor->value());
			}
			sett.remotePointerColor=pw.remotePointerColor->value();

			//also change this in the remote session window
			rmw.remotePointerColor->value(sett.remotePointerColor);
			rmw.remotePointerColorSample->color(Fl_Color(int(sett.remotePointerColor)));
			rmw.remotePointerColorSample->redraw();

			//text display options
			plateManager.setTextDisplayOptions(pw.textDisplayFontSize->value(),pw.textDisplayColor->value(),pw.textDisplayOpacity->value());
			textRenderer().setShadowEnabled(pw.textDisplayShadow->value());

			// Rendering options
			textRenderer().setHintMode((GfcTextRenderer::HintMode)pw.textDisplayHinting->value());
			textRenderer().setFilterNearest(pw.textDisplayFilter->value() == 0);
			textRenderer().setGamma(pw.textDisplayGamma->value());

			// Font selection from dropdown
			if (pw.textDisplayFont->value() >= 0) {
				const char *fontPath = (const char *)pw.textDisplayFont->menu()[pw.textDisplayFont->value()].user_data();
				if (fontPath) {
					textRenderer().loadFont(fontPath);
					textRenderer().loadBoldFont(fontPath);
				}
			}

			//Remote Update options
			networkManager.setEventSendDelay(GFCNETEVENT_TRANSFORMS,1.0/pw.remoteTransformationsFrequency->value());
			networkManager.setEventSendDelay(GFCNETEVENT_FX,1.0/pw.remoteFXFrequency->value());
			networkManager.setEventSendDelay(GFCNETEVENT_OTHER,1.0/pw.remoteOthersFrequency->value());
			networkManager.setSendRemoteLoadRequests(pw.remoteSendLoadRequests->value());
			trackManager.setAutoAcceptRemoteLoadRequests(pw.remoteAutoAcceptLoadRequests->value());

			sett.searchPathsRecursive=pw.searchPathsRecursive->value();
			sett.useSearchPaths=pw.useSearchPaths->value();



			/*if ( pw.blackBGRadioButton->value() ) {
			sett.bgColor=0;
			mw.bgBox->color ( fl_rgb_color ( 0,0,0 ) );
			//		mw.bgBoxForTimeline->color ( fl_rgb_color ( 0,0,0 ) );
			mw.LogoOllin->show();
			mw.LogoOllinGray->hide();
			mw.mainWindow->damage();
			mw.mainWindow->redraw();
			} else {
			mw.LogoOllin->hide();
			mw.LogoOllinGray->show();
			sett.bgColor=1;
			mw.bgBox->color ( fl_rgb_color ( 42,42,42 ) );
			//	mw.bgBoxForTimeline->color ( fl_rgb_color ( 42,42,42 ) );
			mw.mainWindow->damage();
			mw.mainWindow->redraw();
			}*/

			switch ( ( long ) v ) {

	case PREFERENCESDONE_ID:
		saveSettings ( &sett );
		pw.preferencesWindow->hide();

		break;


	case PATHBROWSEBUTTON_ID:

		fc->callback ( save_input_file );
		fc->preview ( 0 );
		fc->filter ( NULL );
		fc->label ( "Select a directory" );
		fc->type ( Fl_File_Chooser::DIRECTORY );
		//printf ( "FileChooser Type is %i\n",fc->type() );
		fc->show();
		fc->show();
		while ( fc->shown() )
			app().waitForEvents();

		if (fc->count()) {
			pw.defaultBrowsePath->value ( fc->value(0));
			sett.defaultBrowsePath=fc->value(0);
			sett.defaultBrowsePathBackup=sett.defaultBrowsePath;
		}
		break;

	case PATHSEARCHBROWSEBUTTON_ID:

		fc->callback ( save_input_file );
		fc->preview ( 0 );
		fc->filter ( NULL );
		fc->label ( "Select a directory for Search" );
		fc->type ( Fl_File_Chooser::DIRECTORY );
		//printf ( "FileChooser Type is %i\n",fc->type() );
		fc->show();
		fc->show();
		while ( fc->shown() )
			app().waitForEvents();

		if (fc->count()) {
			/*pw.defaultBrowsePath->value ( fc->value(0));
			sett.defaultBrowsePath=fc->value(0);*/
			//here we add the path to the settings and add
			std::string tmp = fc->value(0);
			if (std::find(sett.searchPaths.begin(),sett.searchPaths.end(),tmp)==sett.searchPaths.end()) {
				sett.searchPaths.push_back(tmp);
				refreshSearchPathsBrowser();
			} else {
				fl_alert("The selected folder is already in the search preferences");
			}

		}
		break;

	case PATHSEARCHDELETEBUTTON_ID:{
		std::string thePath="";
		for(int i=1; i<=pw.searchPaths->size();i++)
		{
			if(pw.searchPaths->selected(i)){
				thePath=pw.searchPaths->text(i);
				sett.searchPaths.erase(std::find(sett.searchPaths.begin(),sett.searchPaths.end(),thePath));

			}
		}
		refreshSearchPathsBrowser();

								   }
								   break;

	// License callbacks removed for open-source release

			}


		}

		void exitRoutine() {
			/*static char gammaCommand[300];
			printf ( "Restoring original system gamma..." );
			#ifdef WIN32

			SetDeviceGammaRamp ( ::GetDC ( NULL ), m_RampSaved );
			#endif
			#ifdef linux

			sprintf ( gammaCommand,"xgamma -gamma %f.2",gSavedGamma );
			system ( gammaCommand );
			sprintf ( gammaCommand,"xgamma -rgamma %f.2 -ggamma %f.2 -bgamma %f.2",1,1,1 );
			system ( gammaCommand );
			#endif
			#ifdef __APPLE__

			CGSetDisplayTransferByFormula ( kCGDirectMainDisplay,

			redMin, redMax, 1, //min red, max red, red value

			greenMin, greenMax, 1, //min green, max green, green value

			blueMin, blueMax, 1 ); //min blue, max blue, blue value
			#endif

			printf ( "ok\n" );*/
			//clean everything up and exit with correct status
			printf ( "Cleaning up dynamic memory allocations...\n" );

			trackManager.stopLoadingAll();
			trackManager.clearAllSequences();

			printf ( "Storing Favorites...\n" );
			delete fc;
			printf ( "done\n" );

			printf ( "Storing Other Settings...\n" );
			saveSettings ( &sett );
			printf ( "done\n" );

			printf("Clearing crash recovery...\n");
			sessionManager.removeCrashSession();
			printf("done\n");

			//mw.mainWindow->hide();
		}

		void lutCBFillLoadedScroll() {

		}

		void lutCB ( Fl_Widget* o , void* v ) {

			switch ( ( long ) v ) {
	case LUTDONE_ID:

		break;

	case LUTREFRESH_ID:

		break;

	case LUTBROWSE_ID: {



					   }
					   break;


	case LUTAUTOLOAD_ID: {

						 }
						 break;

	case LUTDELETE_ID:

		{

		}



		break;

	default:
		printf ( "Unmanaged lutCB callback, ID: %i\n",long ( v ) );
		break;
			}
		}


		void updateRenderParamsAndSampleFrames(gfcRenderParams &params, Fl_Text_Buffer &textBuffer) {
			params.format = rw.formatChoice->value();
			params.formatString=rw.formatChoice->text();
			params.from=rw.startFrame->value();
			params.to=rw.endFrame->value();
			params.padding=rw.padding->value();
			params.path=rw.path->value();
			params.prefix=rw.prefix->value();
			params.postfix=rw.postfix->value();
			params.frame=rw.startFrame->value();
			std::string tmpName=CreateRenderFilename ( params );
			textBuffer.text ( tmpName.c_str() );
			params.frame=rw.endFrame->value();
			textBuffer.append ( "\n" );
			textBuffer.append ( "...\n" );
			textBuffer.append ( CreateRenderFilename ( params ).c_str() );
			rw.example->buffer ( textBuffer );
		}

		void RenderCB ( Fl_Widget* o,void* v ) {

			static Fl_Text_Buffer textBuffer;
			static gfcRenderParams params;
			switch ( ( long ) v ) {



	case RENDERDONE_ID:
		rw.renderWindow->hide();
		break;

	case RENDERCANCEL_ID: {

		plateManager.abortRender();
		rw.render->copy_label("Render");
		rw.quadrantChoice->activate();
		rw.formatChoice->activate();
		rw.startFrame->activate();
		rw.endFrame->activate();
		rw.padding->activate();
		rw.path->activate();
		rw.prefix->activate();
		rw.startFrame->activate();
		rw.scale->activate();
		rw.jpegQuality->activate();
		rw.pngCompression->activate();
		rw.tiffCompression->activate();
		rw.jpegProgressive->activate();
		rw.jpegOptimize->activate();
		rw.createMovie->activate();
		rw.render->activate();
		rw.cancel->deactivate();

						  }
						  break;

	case RENDERRENDER_ID: {
		if ( !plateManager.isRendering()) {

			params.quadrant=rw.quadrantChoice->value();
			params.format = rw.formatChoice->value();
			params.formatString=rw.formatChoice->text();
			params.from=rw.startFrame->value();
			params.to=rw.endFrame->value();
			params.padding=rw.padding->value();
			params.path=rw.path->value();
			params.prefix=rw.prefix->value();
			params.frame=rw.startFrame->value();
			params.scale=rw.scale->value();
			params.jpegQuality=rw.jpegQuality->value();
			params.pngQuality=rw.pngCompression->value();
			params.tiffCompression=rw.tiffCompression->value();
			params.jpegProgressive=rw.jpegProgressive->value();
			params.jpegOptimized=rw.jpegOptimize->value();
			params.videoCodec=rw.codecChoice->value();
			params.videoVBR=rw.aviQuality->value();
			params.createMovie=rw.createMovie->value();

			if(params.createMovie && rw.deleteFramesAfterMovie->value()){
				params.deleteFramesAfter=1;
			}
			else
			{
				params.deleteFramesAfter=0;
			}


			params.frameRate=atoi ( mw.targetFPSInput->value() );
			params.exrFormat=rw.openEXRDepth->value();
			params.exrCompression=rw.openEXRCompression->value();

			//freeze everything
			rw.quadrantChoice->deactivate();
			rw.formatChoice->deactivate();
			rw.startFrame->deactivate();
			rw.endFrame->deactivate();
			rw.padding->deactivate();
			rw.path->deactivate();
			rw.prefix->deactivate();
			rw.startFrame->deactivate();
			rw.scale->deactivate();
			rw.jpegQuality->deactivate();
			rw.pngCompression->deactivate();
			rw.tiffCompression->deactivate();
			rw.jpegProgressive->deactivate();
			rw.jpegOptimize->deactivate();
			rw.createMovie->deactivate();
			rw.render->deactivate();
			rw.cancel->activate();
			//rw.render->copy_label("Cancel");
			Render ( params );

			//unfreeze everything after rendering.
			rw.render->activate();
			rw.cancel->deactivate();
			rw.render->copy_label("Render");
			rw.quadrantChoice->activate();
			rw.formatChoice->activate();
			rw.startFrame->activate();
			rw.endFrame->activate();
			rw.padding->activate();
			rw.path->activate();
			rw.prefix->activate();
			rw.startFrame->activate();
			rw.scale->activate();
			rw.jpegQuality->activate();
			rw.pngCompression->activate();
			rw.tiffCompression->activate();
			rw.jpegProgressive->activate();
			rw.jpegOptimize->activate();
			rw.createMovie->activate();

		}
						  }
						  break;

	case RENDERBROWSE_ID: {
		fc->callback ( save_input_file );
		fc->preview ( 0 );
		fc->filter ( NULL );
		fc->label ( "Select a directory" );
		fc->type ( Fl_File_Chooser::DIRECTORY );
		fc->show();
		while ( fc->shown() )
			app().waitForEvents();
		rw.path->value ( gFilename );
		fc->type ( Fl_File_Chooser::SINGLE );

		updateRenderParamsAndSampleFrames(params,textBuffer);
						  }

						  break;

	case RENDERAUTORANGE_ID:
		rw.startFrame->value(playbackManager.getFromFrame());
		rw.endFrame->value(playbackManager.getToFrame());

		updateRenderParamsAndSampleFrames(params,textBuffer);
		break;


	case RENDERCREATEMOVIE_ID:
		if(rw.createMovie->active())
			rw.deleteFramesAfterMovie->activate();
		else
			rw.deleteFramesAfterMovie->deactivate();
		break;

	case RENDERFORMAT_ID:
	case RENDERSTART_ID:
	case RENDEREND_ID:
	case RENDERPADDING_ID:
	case RENDERPREFIX_ID: {

		updateRenderParamsAndSampleFrames(params,textBuffer);
						  }
						  break;

	default:
		printf ( "Unhandled RenderCB event: %i\n", ( long ) v );

		break;
			}

		}



		void Render ( gfcRenderParams params ) {
			printf ( "****RENDERING*****\n" );

			std::vector< std::string > renderedFiles;
			plateManager.renderPlate(params,&renderedFiles);

			if ( params.createMovie ) {
#ifdef linux
				std::string source, output,command;
				char vbitrate[30];
				char frameRate[30];
				source=params.path+params.prefix;
				for ( int wildcardCount=0;wildcardCount<params.padding; wildcardCount++) {
					source+="?";
				}
				source+=params.postfix;
				source+=".";
				source+=params.formatString;

				//source = CreateRenderFilename(params);

				output=params.path+params.prefix+params.postfix;
				output+=".avi";

				command="mencoder mf://\"";
				command+=source;
				command+="\" -o ";
				command+=output;
				command+=" -ovc lavc -lavcopts vcodec=msmpeg4v2:mbd=2:trell:autoaspect";
				command+=":vbitrate=";
				sprintf ( vbitrate,"%i",params.videoVBR*1024 );
				command+=vbitrate;
				sprintf ( vbitrate," -mf fps=%i",params.frameRate );
				command+=vbitrate;


				printf ("Command: %s\n",command.c_str() );
				//system("mencoder mf://\"/tmp/testRenders/colibri_*.jpeg\" -o /tmp/testRenders/testFromC.avi -ovc lavc -lavcopts vcodec=msmpeg4v2:mbd=2:trell:aspect=480/270:autoaspect");
				//system("sh ~/projects/gfcheck/kjefecheck7/mencoderScriptMSMPEG.sh");

				//system ( command.c_str() );

				FILE *fp;
				char line[130];			/* line of easa!from unix command*/

				fp = popen(command.c_str(), "r");		/* Issue the command.		*/

				/* Read a line			*/
				while ( fgets( line, sizeof line, fp)) {
					printf("POPEN: %s\n", line);
				}
				pclose(fp);
				if (params.deleteFramesAfter)
				{
					std::vector<std::string>::iterator it=renderedFiles.begin(), end=renderedFiles.end();
					for (it;it!=end;it++)
					{
						removeFile(*it);
					}
				}

#endif
#ifdef WIN32
				printf("mencoder movie option only works in Linux\n");
#endif
#ifdef __APPLE__
				printf("mencoder movie option only works in Linux\n");
#endif
			}

			if ( rw.openWhenDone->value() ) {
				//CLEAR FXs
				plateManager.clearFXStack(params.quadrant);

				//clear sequence
				int trackNumber=plateManager.getTrackOnPlate(params.quadrant);
				gfcSequence* theTrack=trackManager.getSequence(trackNumber);

				theTrack->stopLoading();
				theTrack->unloadAndClear();

				gfcLoadParams loadParams;
				params.frame=params.from;
				loadParams.fileName=CreateRenderFilename(params); //we need the name of the first rendered frame.
				trackManager.loadFromFilename(trackNumber, loadParams);
			}

		}


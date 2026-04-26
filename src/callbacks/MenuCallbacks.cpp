// Menu / shortcut / review-tools / lifecycle callbacks. Extracted from UICallbacks.cpp.
#include "CallbacksInternal.h"
#include <FL/fl_ask.H>
#include <FL/Fl_Menu_Button.H>


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

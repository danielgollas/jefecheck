// Playback / track-bar / control-bar callbacks. Extracted from UICallbacks.cpp.
#include "CallbacksInternal.h"


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



// Preferences-window callback. Extracted from UICallbacks.cpp.
#include "CallbacksInternal.h"
#include <FL/fl_ask.H>

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

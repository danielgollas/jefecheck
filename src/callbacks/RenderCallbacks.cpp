// Render-window callbacks. Extracted from UICallbacks.cpp.
#include "CallbacksInternal.h"

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

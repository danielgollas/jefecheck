// Network/remote-session callbacks. Extracted from UICallbacks.cpp.
#include "CallbacksInternal.h"
#include <FL/fl_ask.H>

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

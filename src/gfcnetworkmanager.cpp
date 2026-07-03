#include "gfcnetworkmanager.h"
#include "gfcTextRenderer.h"

#include "gfcNetworkStructures.h"

#include "gfcnetworklog.h"
extern gfcNetworkLog networkLog;

#include "gfcplaybackmanager.h"
extern gfcPlaybackManager playbackManager;

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

#include "gfctrackmanager.h"
extern gfcTrackManager trackManager;


gfcNetworkManager networkManager;




gfcNetworkManager::gfcNetworkManager()
{
    chatFontSize=12;
    chatTextBG=true;
    chatAutoFade=true;
    chatOpacity=0.75;
    chatDisplayLines=8;
    chatFadeDelay=10;
    takeNotifications=true;
	blinkerOn=false;

	sendRemoteLoadRequests=true;

	allReady=0;
	sincStatus_FX=0;
	sincStatus_LUT=0;
	sincStatus_Stacks=0;
	sincStatus_Playlist=0;
	
}


gfcNetworkManager::~gfcNetworkManager()
{
}

void gfcNetworkManager::resetSincStatus()
{
	allReady=0;
	sincStatus_FX=0;
	sincStatus_LUT=0;
	sincStatus_Stacks=0;
	sincStatus_Playlist=0;
}

void gfcNetworkManager::setSendRemoteLoadRequests(int value){
	sendRemoteLoadRequests=value;
}
int gfcNetworkManager::getSendRemoteLoadRequests()
{
	return sendRemoteLoadRequests;
}



void gfcNetworkManager::startServer(gfcServerParams * params)
{

	if ( connected && !isServer) {
            return;
        }
        
        //1. Startup Server and disable client gui so we can't connect as client from the gui when acting as server
        client.disableGUI();
		
		resetSincStatus();
        
		server.start(params);
        
        
        //2. Connect to out own server from the client, since the machine that acts as server is also a client.
        //We use the connect method using params so we can override the client's GUI params.
        networkLog.addToLog("Starting Loopback Client...");
        gfcConnectionParams clientParams;
        clientParams.nickname=server.getName();
        clientParams.serverIP="127.0.0.1";
        clientParams.port=server.getPort();
        clientParams.password=server.getPassowrd();
        client.setIsServerClient(true);
        client.Connect(&clientParams);
        networkLog.addToLog("Loopback Client Started");
	isServer=true;
	connected=true;

}

void gfcNetworkManager::initializeWidgets()
{
	server.initializeWidgets();
	client.initializeWidgets();
}

void gfcNetworkManager::stopServer()
{
	client.Disconnect();
	server.stop();
	isServer=false;
	connected=false;
	client.enableGUI();


}

void gfcNetworkManager::stopConnection()
{
    client.Disconnect();
    isServer = false;
    connected = false;
    server.enableGUI();
}

std::vector<std::string> gfcNetworkManager::participantNames() {
    if (isServer) return server.getParticipantNames();
    return client.getPeersInSession();
}

std::string gfcNetworkManager::connectionStatusText() {
    if (isServer) return connected ? "Hosting (server)" : "Not hosting";
    return client.getStatus();   // e.g. "Online!", "Attempting Connection...", "Offline"
}

std::vector<std::string> gfcNetworkManager::chatLogLines() {
    std::vector<std::string> out;
    for (auto& e : client.getChatLog())
        out.push_back(e.sender + ": " + e.message);
    return out;
}

std::vector<std::string> gfcNetworkManager::drainErrors() {
    // Errors already surface through client status strings (RED). Reserved
    // for a dedicated error queue; empty for now so callers compile.
    return {};
}

bool gfcNetworkManager::getConnected()
{
	return connected;
}

bool gfcNetworkManager::getIsServer()
{
	return isServer;
}

void gfcNetworkManager::update()
{
	//update client and server 
	{
		server.Update();
		client.Update();
	}
	
	//check for client status changes
	if(client.statusChange)
	{
		client.statusChange=false;
		if(!client.getIsConnected() && !client.getAttemptingConnection())
		{
			server.enableGUI();
			if(!isServer) connected = false;   // client peer dropped
		}
		
		if(client.getIsConnected())
		{
			connected=true;
		}
		
		if(client.getGotNewChatMessage())
		{
			chatFadeCounter=chatFadeDelay/GFCNET_CHAT_FADE_SPEED;
		}
	}
	
	
	//Check the event notification flags from the rest of the program and send appropiate messages.
	if(connected)
	{
	float timeStep=playbackManager.getTimestep();
	//check transformations
	if(events[GFCNETEVENT_TRANSFORMS].readyForSend(timeStep))
	{
		events[GFCNETEVENT_TRANSFORMS].processed();
		//printf("Sending transforms\n");
		client.SendTransformations(plateManager.getTransformations());
	}

	if (events[GFCNETEVENT_COLOR].readyForSend(timeStep))
	{
		events[GFCNETEVENT_COLOR].processed();
		client.SendColorCorrections(plateManager.getColorCorrections());
	}
	
	
	//check fx
	if(events[GFCNETEVENT_FX].readyForSend(timeStep))
	{
		events[GFCNETEVENT_FX].processed();
		printf("Sending FX\n");
	}
	
	
	
	
	//check other
	if(events[GFCNETEVENT_OTHER].readyForSend(timeStep))
	{
		events[GFCNETEVENT_OTHER].processed();
		//printf("Sending other\n");
		gfcNetOtherStatesInfo tmp;
		tmp.layout=plateManager.getFramingMode();
		tmp.playbackInfo=playbackManager.getPlaybackInfo();
		tmp.plateStateInfo=plateManager.getPlateStateInfo();
		tmp.trackStateInfo=trackManager.getTrackStateInfo();
		client.SendOtherStatesMessage(tmp);
	}

	
	
	


	}
	
	//update the chat fade even if not connected so we can fade out when disconnected
	//also update the chat blinker
	{
		if ( gChatMode==0 ){
			cursorBlinkCounter=0;
			chatFadeCounter-=playbackManager.getTimestep()*GFCNET_CHAT_FADE_SPEED*chatAutoFade;
			if (chatFadeCounter>0 && chatFadeCounter<chatOpacity)
			{
				plateManager.setChanged();
			}
		}
		else
		{
			cursorBlinkCounter+=playbackManager.getTimestep();

			if ( cursorBlinkCounter>0.25 ){
				blinkerOn=!blinkerOn;
				cursorBlinkCounter=0;
				plateManager.setChanged();
			}
		}
	}


}

void gfcNetworkManager::startConnection(gfcConnectionParams * params)
{
	resetSincStatus();
	if(client.getAttemptingConnection() || client.getIsConnected()) //if already trying to connect or connected, disconnect
	{
		client.Disconnect();
		connected=false;
	}
	else
	{
	
	client.Connect(params);
	server.disableGUI();
	}
}

void gfcNetworkManager::startFXSinc()
{
	//if we are a server, we start an FX sinc...
	if(this->connected){
		if(this->isServer)
		{
			server.startFXSinc(client.getServerSystemAddress(),true); //broadcaste the event	
		}
		else{//...if we are a client, we send a Loaded FX hashes.
			client.SendLoadedFXsHashes();
		}
	}
}

void gfcNetworkManager::startLUTSinc()
{
	//if we are a server, we start a LUT sinc...
	if(this->connected){
		if(this->isServer)
		{
			server.startLUTSinc(client.getServerSystemAddress(),true);	//broadcaste the event
		}
		else{//...if we are a client, we send a Loaded LUT hashes.
			client.SendLoadedLUTsHashes();
		}
	}
}

void gfcNetworkManager::startStackSinc()
{
	//if we are a server, we send the stack
	if(this->connected){
		if(this->isServer)
		{
			server.startFXSinc(client.getServerSystemAddress(),true); //broadcaste the event	
		}
		else{//...if we are a client, we send a Loaded FX hashes.
			client.SendLoadedFXsHashes();
		}
	}
}


void gfcNetworkManager::sendChatMessage()
{	
	//printf("sending: %s\n",gChatTextString.c_str());
	client.SendChatMessage(gChatTextString); //this simply sends the message in the chatString buffer to the server
}

std::string gfcNetworkManager::getChatDisplayString()
{
		return "";
}

void setupAnfPrintSincStatus(int value, std::string sincWaitString,int w, int h)
{
	gfc_gl_font(FL_HELVETICA, 24);
	if (value==1)
	{
		sincWaitString+="...Done\n";
		textRenderer().setColor(0.0, 0.7, 0.0, 1.0);
	}
	else
	{
		textRenderer().setColor(0.9, 0.9, 0.9, 1.0);
	}
	gfc_gl_draw(sincWaitString.c_str(), -w/4+10, -h/4+10, w/2-10, h/2-10, FL_ALIGN_CENTER | FL_ALIGN_TOP | FL_ALIGN_WRAP | FL_ALIGN_INSIDE);
}

void gfcNetworkManager::draw(int w, int h, bool resized)
{
	 //START DRAWING THE ONLINE STUFF
    {        
        //DRAW CONNECTION STATUS STUFF
		if (connected && !allReady)
		{
			//draw the status box with the current sync state

			glPushAttrib(GL_ALL_ATTRIB_BITS);
			
			glMatrixMode ( GL_PROJECTION );
			glPushMatrix();
			glLoadIdentity();
			glOrtho ( -w /2.0, w /2.0, -h /2.0, h /2.0, -5000.0, 5000.0 );

			glMatrixMode ( GL_MODELVIEW );
			glPushMatrix();
			glLoadIdentity();
			glViewport ( 0,0,w,h );

			
			glEnable ( GL_BLEND );
			glDisable ( GL_TEXTURE_RECTANGLE_ARB );
			glBlendFunc ( GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA );

		

			 
			//glColor4f ( 0.1,0.1,0.1,chatFadeCounter>chatOpacity?chatOpacity:chatFadeCounter );

			//draw a dark gray box with a light gray frame
			float darkGray=0.2;
			float boxOpacity=0.75;
			glColor4f( darkGray,darkGray,darkGray,boxOpacity);
			gl_rectf( -w/4, -h/4, w/2, h/2 );
			GLint oldLineWidth;
			glGetIntegerv(GL_LINE_WIDTH,&oldLineWidth);
			glLineWidth(3);
			glColor4f( darkGray*2,darkGray*2,darkGray*2,boxOpacity);
			gl_rect( -w/4, -h/4, w/2, h/2 );
			glLineWidth(oldLineWidth);
			
			//build the string
			std::string sincWaitString="\nPlease wait until all users are ready\n\n";
			
			gfc_gl_font(FL_HELVETICA, 18);
			textRenderer().setColor(1, 1, 1, 1.0);
			gfc_gl_draw(sincWaitString.c_str(), -w/4+10, -h/4+10, w/2-10, h/2-10, FL_ALIGN_CENTER | FL_ALIGN_TOP | FL_ALIGN_WRAP | FL_ALIGN_INSIDE);

			sincWaitString="\n\n\nSyncing FXs";			
			setupAnfPrintSincStatus(sincStatus_FX,sincWaitString,w,h);

			sincWaitString="\n\n\n\nSyncing LUTs";
			setupAnfPrintSincStatus(sincStatus_LUT,sincWaitString,w,h);
			
			sincWaitString="\n\n\n\n\nSyncing FX Stacks";
			setupAnfPrintSincStatus(sincStatus_Stacks,sincWaitString,w,h);

			sincWaitString="\n\n\n\n\n\nMerging Playlists";
			setupAnfPrintSincStatus(sincStatus_Playlist,sincWaitString,w,h);
			
			if (sincStatus_LUT && sincStatus_FX && sincStatus_Stacks && sincStatus_Playlist && !allReady)
			{
				gfc_gl_font(FL_HELVETICA, 18);
				textRenderer().setColor(1, 1, 1, 1.0);
				sincWaitString="\n\n\n\n\n\n\n\n\nWaiting for other users\n";
				gfc_gl_draw(sincWaitString.c_str(), -w/4+10, -h/4+10, w/2-10, h/2-10, FL_ALIGN_CENTER | FL_ALIGN_WRAP | FL_ALIGN_INSIDE);
			}
			
			//printf("\n\n****Sinc Status****\n\nFX:%i\nLUT:%i\nStack:%i\nPlaylist:%i\nAllReady:%i\nString:\n%s\n",sincStatus_FX,sincStatus_LUT,sincStatus_Stacks,sincStatus_Playlist,allReady,sincWaitString.c_str());

			

			

			

			

			

			//glColor4f ( 1,1,1,chatFadeCounter>chatOpacity?chatOpacity:chatFadeCounter );

			//gl_draw ( chatDisplayString.c_str(),-w /2+10,-h /2 + 10,w,( gl_height() ) * ( logSize-beginLine+1+gChatMode ),Fl_Align ( FL_ALIGN_LEFT | FL_ALIGN_BOTTOM | FL_ALIGN_WRAP | FL_ALIGN_INSIDE) );
			glDisable ( GL_BLEND );
			glPopMatrix(); //pop modelview
			glMatrixMode ( GL_PROJECTION );
			glPopMatrix();//pop matrix
			glMatrixMode ( GL_MODELVIEW );

			glPopAttrib();
		}
		
        
        //DRAW CHAT STUFF
        
	    
            if ( chatFadeCounter>0 || gChatMode!=0 ) {

		glPushAttrib(GL_ALL_ATTRIB_BITS);
		
                glMatrixMode ( GL_PROJECTION );
                glPushMatrix();
                glLoadIdentity();
                glOrtho ( -w /2.0, w /2.0, -h /2.0, h /2.0, -5000.0, 5000.0 );

                glMatrixMode ( GL_MODELVIEW );
                glPushMatrix();
                glLoadIdentity();
                glViewport ( 0,0,w,h );

                std::string chatDisplayString="";
                std::vector<gfcChatLogEntry> chatLog=client.getChatLog();
                
                int logSize=chatLog.size();

                if ( chatLineOffset>abs ( logSize-chatDisplayLines ) )
                    chatLineOffset=abs ( logSize-chatDisplayLines );

                if ( chatLineOffset>=logSize )
                    chatLineOffset=logSize;

                int beginLine=logSize-chatDisplayLines-chatLineOffset;
                if ( beginLine<0 )
                    beginLine=0;



                int endLine;
                logSize-=chatLineOffset;
                //get the last n lines from the chatLog
                for ( int i=beginLine;i<logSize;i++ ) {
                    chatDisplayString+=chatLog[i].getFormattedString();
                    chatDisplayString+="\n";
                }

                std::string tmpChatTextString=gChatTextString;

               	if ( blinkerOn) {
					tmpChatTextString.insert(chatPosOffset,"|");//chatDisplayString+="][";
				} else {
					if (tmpChatTextString.empty())
						tmpChatTextString+=" ";
				}
				
                /*if ( cursorBlinkCounter>0.25 ) {
                    tmpChatTextString.insert(chatPosOffset,"|");//chatDisplayString+="][";
                } else {
                    if (tmpChatTextString.empty())
                        tmpChatTextString+=" ";
                }*/

                if ( gChatMode==1 ) {
                    chatDisplayString+="________________________________________________________\n";
                    chatDisplayString+=tmpChatTextString;
                }
                //chatDisplayString+="";
                								
		//printf ( "chatFadeCounter: %f\n",chatFadeCounter );


                //gl_draw("This is the chat text\nAnd another Line\nOne More\n---------------------------\nThis is what the user is typing ",-w/2+10,-h/2+10,200,w,Fl_Align(FL_ALIGN_LEFT | FL_ALIGN_BOTTOM | FL_ALIGN_WRAP));
                glEnable ( GL_BLEND );
                glDisable ( GL_TEXTURE_RECTANGLE_ARB );
                glBlendFunc ( GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA );

                gfc_gl_font(FL_HELVETICA, chatFontSize);

                //how many lines to draw?
                int linesToDraw=0;
                std::string::iterator linesToDrawIter=chatDisplayString.begin(), linesToDrawEnd=chatDisplayString.end();

                for (linesToDrawIter;linesToDrawIter!=linesToDrawEnd;linesToDrawIter++) {
                    if ((char)(*linesToDrawIter)=='\n')
                        linesToDraw++;
                }

                if ( chatTextBG && ( gChatMode || logSize>0 ) ) {
                    glColor4f ( 0.1,0.1,0.1,chatFadeCounter>chatOpacity?chatOpacity:chatFadeCounter );
                    gl_rectf ( -w /2,  -h/2+10, w, (int)( gfc_gl_height() * ( linesToDraw+gChatMode ) ) );
                }

                textRenderer().setColor(1, 1, 1, chatFadeCounter>chatOpacity?chatOpacity:chatFadeCounter);

                gfc_gl_draw(chatDisplayString.c_str(), -w/2+10, -h/2+10, w, (int)(gfc_gl_height() * (logSize-beginLine+1+gChatMode)), FL_ALIGN_LEFT | FL_ALIGN_BOTTOM | FL_ALIGN_WRAP | FL_ALIGN_INSIDE);
                glDisable ( GL_BLEND );
                glPopMatrix(); //pop modelview
                glMatrixMode ( GL_PROJECTION );
                glPopMatrix();//pop matrix
                glMatrixMode ( GL_MODELVIEW );

		glPopAttrib();
            }
             
        
        
       
        //DRAW REMOTE POINTERS: Remote pointers are drawn in the gfcPlates since they need to have all the transformation.

    }
    //END OF DRAWING THE ONLINE STUFF
}

void gfcNetworkManager::saveChatLog(std::string sfilename)
{
	  if ( sfilename.empty() ) {
        printf ( "Not a filename\n" );
        return;
    }

    

    //if extension is wrong append it.
    /*if ( strrchr ( pfileName,'.' ) ==NULL  || ( ( strcmp ( strrchr ( pfileName,'.' ),".txt" ) !=0 ) ) ) {
        sfileName+=".txt";
        printf ( "Corrected filename %s\n",sfileName.c_str() );
    }*/
    
    sfilename=AppendExtensionToFilename(sfilename,".txt");

    std::ofstream outFile;

    //write jfxFile
    outFile.open ( sfilename.c_str(),std::ostream::out );

    outFile<<"---------------------------------------"<<std::endl;
    outFile<<"JefeCheck Chat Log"<<std::endl;
    outFile<<"Creation Date: "<<asciiTime(false) <<std::endl;
    outFile<<"---------------------------------------"<<std::endl;
    
    std::vector<gfcChatLogEntry>log=client.getChatLog();
    std::vector<gfcChatLogEntry>::iterator iter=log.begin(), end=log.end();
    for ( iter;iter!=end;iter++ ) {
          outFile<<iter->getFormattedString()<<std::endl;
    }
    
    /*for ( int i=0;i<client.getchatLog.size();i++ ) {
        outFile<<chatLog[i]<<std::endl;
    }*/
    
    outFile<<"---------------------------------------"<<std::endl;
    outFile.close();
}

void gfcNetworkManager::sendSystemChatMessage(std::string message, int type)
{
	client.SendChatMessage(message,type);
}

void gfcNetworkManager::sendPointerInfoMessage(gfcNetPointerInfo pointerInfo)
{
	client.SendPointerInfoMessage(pointerInfo);

}



/**
 * Sets the flag for a specific type of event, when the network manager updates and the time
 threshold has passed we will send whatever events are needed.
 * @param networkEvent 
 */
void gfcNetworkManager::notifyEvent(int networkEvent)
{
	if(takeNotifications){
	if(networkEvent<GFCNETEVENT_NUMOFEVENTTYPES)
	{
		events[networkEvent].notified=true;
	}
	}
}

void gfcNetworkManager::setEventSendDelay(int networkEvent, float delay)
{
	if(networkEvent<GFCNETEVENT_NUMOFEVENTTYPES)
	{
		events[networkEvent].delay=delay;
	}
}

void gfcNetworkManager::setTakeNotifications(bool value)
{
	takeNotifications=value;
}



void gfcNetworkManager::sendPlayPauseMessage(gfcNetPlayPauseInfo info)
{
	if(connected && takeNotifications)
		client.SendPlayPauseMessage(info);
}

void gfcNetworkManager::sendFXAddMessage(gfcNetFXAddInfo info)
{
	if(connected && takeNotifications)
	client.SendFXAddMessage(info);
}

void gfcNetworkManager::sendFXStackMessage(gfcNetFXStackMessage message)
{
	if(connected && takeNotifications)
		client.SendFXStackMessage(message);
}

void gfcNetworkManager::sendFXCommonMessage(gfcNetFXCommonInfo info)
{
	if(connected && takeNotifications)
	client.SendFXCommonMessage(info);
}

void gfcNetworkManager::sendFXAttribMessage(gfcNetFXAttribInfo info)
{
	if(connected && takeNotifications)
	    client.SendFXAttribMessage(info);
}

void gfcNetworkManager::setRecent(std::vector<std::string> recents)
{
	client.setRecent(recents);
}

void gfcNetworkManager::setClientAddress(std::string ip, std::string port)
{
	client.setAddress(ip,port);
}

void gfcNetworkManager::sendPlaylistItem(gfcPlaylistItem item)
{
	if(connected && takeNotifications && sendRemoteLoadRequests)
	    client.SendPlaylistItem(item);
}

void gfcNetworkManager::sendPlaylist(std::string playlist)
{
	if(connected && takeNotifications){
		gfcNetPlaylistMessage tmpMessage;
		tmpMessage.thePlaylist=playlist;
		client.SendPlaylistMessage(tmpMessage);
	}
	
}

void gfcNetworkManager::sendRemotePointerColor(int color){
	if(connected && takeNotifications){
		client.SendRemotePointerColor(color);
	}
}

void gfcNetworkManager::sendPlaylistEvent(gfcNetPlaylistEvent theEvent)
{
	if(connected && takeNotifications){
		client.sendPlaylistEvent(theEvent);
	}
}

void gfcNetworkManager::handleAllReady()
{
	//force the plate manager to redraw.
	plateManager.setChanged();
}

void gfcNetworkManager::handleNewPlayer()
{
	allReady=0;
	plateManager.setChanged();
}

void gfcNetworkManager::handleSincStart()
{
		//force the plate manager to redraw.
		resetSincStatus();
		plateManager.setChanged();
}
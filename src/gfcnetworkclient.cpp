#include "gfcnetworkclient.h"


#include <string>
#include <vector>
#include <map>
#include <set>
#include <stdlib.h> // For atoi
#include <cstring> // For strlen
#include <sstream> //for stingstream
#include <stdio.h>
#include "gfcTransportFactory.h"
#include "gfcNetworkStructures.h"
#include "gfcWire.h"
#include "gfcWireMessages.h"


#include "gfcnetworklog.h"
extern gfcNetworkLog networkLog;

#include "gfcfxmanager.h"
extern gfcFXManager fxManager;

#include "gfcplaylistmanager.h"
extern gfcPlaylistManager playlistManager;

#include "gfclutmanager.h"
extern gfcLUTManager lutManager;

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

#include "gfctrackmanager.h"
extern gfcTrackManager trackManager;

#include "gfcplaybackmanager.h"
extern gfcPlaybackManager playbackManager;

#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;

gfcNetworkClient::gfcNetworkClient() {
    transport_ = jefe::net::makeTransport(); // kind from JEFECHECK_TRANSPORT (JEF-24)
	haveSentMyPlaylist=false;
	// Explicit init: pumpNetwork() reads these from the very first tick.
	// Safe today only because networkManager is a global (static zero-init),
	// but don't rely on that.
	gotMessages=false;
	isConnected=false;
	attemptingConnection=false;
	statusChange=false;
	statusColor=0;
	gotNewChatMessage=false;
	isServerClient=false;
}

void gfcNetworkClient::setStatusInternal(std::string s, int color) {
    status = s;
    statusColor = color;
    statusChange = true;
}

std::string gfcNetworkClient::getStatus() { return status; }
int gfcNetworkClient::getStatusColor() { return statusColor; }
std::vector<std::string> gfcNetworkClient::getPeersInSession() { return peersInSession; }


gfcNetworkClient::~gfcNetworkClient() {
}

void gfcNetworkClient::Startup() {
    isConnected=false;


}

bool gfcNetworkClient::Connect(gfcConnectionParams * params) {
    bool b;
	

	
    std::string theServerIP = "";
    int thePort = 0;
	
    std::string thePassword;

    if (params) {
        theServerIP  = params->serverIP;
        thePort      = params->port;
        thePassword  = params->password;
        this->nickName = params->nickname;
    }
    // Store for saveCurrentToRecentIPs()
    this->serverIP = theServerIP;
    this->port     = thePort;
	std::string conectingMessage="Client: Starting Connection to:";
	conectingMessage+=theServerIP;
	conectingMessage+=":";
	char thePortString[30];
	sprintf(thePortString,"%i",thePort);
	conectingMessage+=thePortString;
	networkLog.addToLog(conectingMessage);
    Startup();
    b=transport_->connect ( theServerIP, ( unsigned short ) thePort, thePassword );
    if ( b==false ) {
        printf ( "Client connect call failed!\n" );
        networkLog.addToLog("Client: Client connect call failed!",GFCNETLOGTYPE_ALERT);
        Disconnect();
    }

    setStatusInternal("Attempting Connection...", GFCCOLOR_GRAY);
    
	//since the connection attempt was succesful, save the ip and port to the recent
	this->saveCurrentToRecentIPs();

	//statusChange=true;
    /*strcpy ( status, "Attempting Connection" );

    insertIntoNetLog ( "Starting Connection" );*/
    attemptingConnection=b;
    return b;
}

void gfcNetworkClient::initializeWidgets() {
}

void gfcNetworkClient::saveCurrentToRecentIPs()
{
	std::stringstream mashed;
	mashed << this->serverIP;
	mashed << ":";
	mashed << this->port;

	int found=-1;
	for (int i=sett.recentIPs.size()-1;i>=0;i--)
	{
		if(mashed.str()==sett.recentIPs[i])
		{
			found=i;
			break;
		}
	}

	if(found>=0)
	{
		sett.recentIPs.erase(sett.recentIPs.begin()+found);
	}

	sett.recentIPs.insert(sett.recentIPs.begin(),mashed.str());

	//check if the last one is still in the max to store range.

	if (sett.recentIPs.size()> sett.maxRecentIPs)
	{
		sett.recentIPs.erase(sett.recentIPs.begin()+sett.maxRecentIPs,sett.recentIPs.end());
	}
	// recent-IP UI update is out of scope for Qt port
}

void gfcNetworkClient::setRecent(std::vector<std::string> recents){
	// recent-IP UI is out of scope for Qt port
	(void)recents;
}

void gfcNetworkClient::setAddress(std::string pip, std::string pport)
{
	this->serverIP = pip;
	this->port = atoi(pport.c_str());
	this->saveCurrentToRecentIPs();
}

void gfcNetworkClient::disableGUI() {
    // GUI enable/disable is managed by Qt — no-op
}

void gfcNetworkClient::enableGUI() {
    // GUI enable/disable is managed by Qt — no-op
}

void gfcNetworkClient::Disconnect() {
    transport_->disconnect();
    std::vector<std::string> emptyVector;
    peersInSession = emptyVector; statusChange = true;

    if (attemptingConnection) {
        setStatusInternal("Offline: Connection Attempt Canceled", GFCCOLOR_GRAY);
        networkLog.addToLog("Client: Connection Attempt Canceled");
    }
	else
	{
		setStatusInternal("Offline", GFCCOLOR_GRAY);
		networkLog.addToLog("Client: Ended Session");
	}


    isConnected=false;
    attemptingConnection=false;

    statusChange=true;

}

bool gfcNetworkClient::GetGotMessages()
{
	if (gotMessages)
	{
		gotMessages=false;
		return true;
	}
	else
	{
		return false;
	}
	
}

void gfcNetworkClient::Update() {
    jefe::net::TransportEvent ev;
    while ( transport_->poll ( ev ) ) {
		gotMessages=true;
        std::stringstream ss (std::stringstream::in | std::stringstream::out);
        switch ( ev.type ) {
        case jefe::net::TransportEventType::ConnectAccepted: {
            printf("Client Connected!\n");
            setStatusInternal("Connected!... getting acquainted with everyone", GFCCOLOR_YELLOW);
			networkManager.handleSincStart();
            serverPeerId_=ev.peer;
            attemptingConnection=false;
            jefe::wire::Writer w;
            jefe::wire::beginFrame ( w, ( uint16_t ) GFCNETID_NICKNAMESEND );
            w.writeString ( nickName );
			//also send our pointer color
			w.writeI32 ( sett.remotePointerColor );
            if ( w.ok() ) transport_->send ( w.data(), ( int ) w.size(), serverPeerId_, false );
            networkLog.addToLog("Client: Connection Request Accepted!");
        }
        break;

        case jefe::net::TransportEventType::ConnectFailed: {

            if (attemptingConnection) {
                printf ( "Client Error: ID_CONNECTION_ATTEMPT_FAILED\n" );

                setStatusInternal("Offline, Connection Attempt Failed", GFCCOLOR_RED);
                attemptingConnection=false;
                Disconnect();
            }
            networkLog.addToLog("Client: Connection Attempt Failed",GFCNETLOGTYPE_ALERT);


        }
        break;
        case jefe::net::TransportEventType::AlreadyConnected:
            printf ( "Client Error: ID_ALREADY_CONNECTED\n" );
            networkLog.addToLog("Client: Already Connected!",GFCNETLOGTYPE_ALERT);
            break;

        case jefe::net::TransportEventType::ServerFull: {
            printf ( "Client Error: ID_NO_FREE_INCOMING_CONNECTIONS\n" );
            setStatusInternal("Could not Connect, no free incoming connections on server", GFCCOLOR_RED);
            networkLog.addToLog("Client: Could not Connect, no free incoming connections on server",GFCNETLOGTYPE_ALERT);
            Disconnect();
        }
        break;

        case jefe::net::TransportEventType::Disconnected: {
            printf("ID_DISCONNECTION_NOTIFICATION\n");
            //gIsServer=false; //WE DON'T STOP BEING THE SERVER UNTIL THE INTERNAL CLIENT DISCONNECTS
            setStatusInternal("Offline, Disconnected from Server", GFCCOLOR_GRAY);
            Disconnect();
            networkLog.addToLog("Client: Disconnected from Server");
        }
        break;

        case jefe::net::TransportEventType::ConnectionLost: {
            printf ( "Client Error: ID_CONNECTION_LOST\n" );


            setStatusInternal("Offline, Connection Lost", GFCCOLOR_RED);
            Disconnect();
            networkLog.addToLog("Client: Connection Lost",GFCNETLOGTYPE_ALERT);
            //insertIntoNetLog ( "Connection Lost" );
        }
        break;
        // ID_MODIFIED_PACKET is swallowed by the transport (JEF-22 decision, revisit JEF-23)

        case jefe::net::TransportEventType::Data: {
        // JEF-23: ev.bytes is a jefe::wire frame (the transport already
        // stripped the RakNet envelope byte). Parse the frame header; a
        // truncated buffer or version mismatch is a silent drop.
        jefe::wire::Reader r ( ev.bytes.data(), ev.bytes.size() );
        uint16_t msgType = 0;
        if ( !jefe::wire::readFrameHeader ( r, msgType ) ) break;
        switch ( msgType ) {

        case GFCNETID_NICKALREADYINUSE: {
            networkLog.addToLog("Client: Could not connect to server, nickname already taken!",GFCNETLOGTYPE_ALERT);
            Disconnect();
        }
        break;

        case GFCNETID_PEERSINSESSION: {
            //printf ( "Got a GFCNETID_PEERSINSESSION!\n" );
            uint32_t numOfPeers=0;
            if ( !r.readU32 ( numOfPeers ) ) break; // malformed: skip silently

            std::vector<std::string> tmpPeersInSession;
            bool readOk=true;
            for ( uint32_t i=0;i<numOfPeers;i++ ) {
                std::string tmpNickname;
                if ( !r.readString ( tmpNickname ) ) { readOk=false; break; }
                tmpPeersInSession.push_back(tmpNickname);
            }
            if ( !readOk ) break; // malformed: skip silently

            peersInSession = tmpPeersInSession; statusChange = true;
            networkLog.addToLog("Client: Updated peers in session");
            statusChange=true;
            isConnected=true;
        }
        break;

		case GFCNETID_NEWPEERINSESSION:
			{
				networkManager.handleNewPlayer();
			}
			break;
				

        case GFCNETID_REQUESTLUTSHASHES: {
            networkLog.addToLog( "Client: Got a request for LUT Hashes");
			networkManager.sincStatus_LUT=0;
			plateManager.setChanged();
            SendLoadedLUTsHashes();
        }
        break;

        case GFCNETID_REQUESTLUTS: {
            printf ( "Client: Got GFCNETID_REQUESTLUTS\n" );
            uint32_t howMany=0;
            if ( !r.readU32 ( howMany ) ) break; // malformed: skip silently

            jefe::wire::Writer outW;
            jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_REQUESTEDLUTS ); //send back the requested LUTs
            outW.writeU32 ( howMany ); //how many we are sending out (legacy semantics: requested count, loaded ones follow)

            printf ( " %i LUTs Requested\n", (int)howMany );

            bool readOk=true;
            for ( uint32_t i=0; i<howMany;i++ ) {
                std::string theText;
                if ( !r.readString ( theText ) ) { readOk=false; break; }
                CubeLUT tmpLUT=lutManager.getLUTbyHash(theText.c_str());
                if ( strcmp(tmpLUT.name,"")!=0 ) {//the LUT is indeed loaded, serialize it
                    serializeLUT ( & tmpLUT,outW );
                }
            }
            if ( !readOk ) break; // malformed request: send nothing

            //send the serialized LUTs to the server.
            if ( outW.ok() ) transport_->send ( outW.data(), ( int ) outW.size(), serverPeerId_, false );

        }
        break;

        case GFCNETID_MISSINGLUTS: {
            //printf ( "Client: Got a GFCNETID_MISSINGLUTS\n" );

            uint32_t howMany=0;
            if ( !r.readU32 ( howMany ) ) break; // malformed: skip silently

            printf ( " parsing and loading %i LUTs\n",(int)howMany );

            for ( uint32_t i=0;i<howMany;i++ ) {
                //TODO: Check for errors loading LUTS
                if ( !unserializeLUT ( r ) ) break; // truncated mid-list: stop (already-loaded LUTs stay)
            }
        }
        break;
		
		case GFCNETID_LUTSSINCCOMPLETE:{

			ss.str("");
			ss<<"Client: LUT Sinc Complete";
			networkLog.addToLog(ss.str());
			
			networkManager.sincStatus_LUT=1;
			plateManager.setChanged();

		 }
		 break;
		
		case GFCNETID_SENDFXTACKS:
			{
				ss.str("");
				ss<<"Client: Received FX Stacks";
				networkLog.addToLog(ss.str());
				
				//unserialize the stack and load into plate manager
				std::string stackString;
				if ( !r.readString ( stackString ) ) break; // malformed: skip silently (no apply, no reply)

				//std::cout << "Client Received FX stack: \n" << stackString;

				//setting takeNotifications to false prevents loops
				networkManager.setTakeNotifications(false);
				plateManager.setPlateFXStacksFromString(stackString);
				networkManager.setTakeNotifications(true);

				//send stack received.
				jefe::wire::Writer outW;
				jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_RECEIVEDFXSTACKS ); //send back the requested FXs
				if ( outW.ok() ) transport_->send ( outW.data(), ( int ) outW.size(), serverPeerId_, false );

			}
			break;

		case GFCNETID_SENDSTACKSINCFINISHED:
			{
				ss.str("");
				ss<<"Client: Finished FX Stack Sinc";
				networkLog.addToLog(ss.str());
				
				networkManager.sincStatus_Stacks=1;
				plateManager.setChanged();
			}
			break;
			

		case GFCNETID_REQUESTPLAYLIST:
		{
			//send our playlist

			//printf("Client got REQUESTPLAYLIST\n");
			
			
			networkManager.sincStatus_Playlist=0;
			plateManager.setChanged();

			jefe::wire::Writer outW;
			jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_SENDPLAYLISTFORMERGE );

			std::string pl=playlistManager.getPlaylistAsString();
			//printf("Client: sending my pl: %s\n",pl.c_str());
			outW.writeString ( pl ); //legacy explicit length(+5) field dropped: writeString carries the length
			if ( outW.ok() ) transport_->send ( outW.data(), ( int ) outW.size(), serverPeerId_, false );
			haveSentMyPlaylist=true;
		}
		break;

		case GFCNETID_MERGEDPLAYLISTS:
			{
				//printf("Client got GFCNETID_MERGEDPLAYLISTS\n");
				
				
				
				//read the playlist and load it (making sure it is replaced)
				std::string thePl;
				if ( !r.readString ( thePl ) ) break; // malformed: skip silently

				if (haveSentMyPlaylist==true)
				{
				//we only overwrite ours if we already sent it before, otherwise, we might get a merged playlist and ours will be left in oblivion.
				//printf("Client received this pl:\n%s***",thePl.c_str());
				networkManager.setTakeNotifications(false);
				playlistManager.setPlaylistFromString(thePl,1);
				networkManager.setTakeNotifications(true);
				
				//we should send a reply to notify the server so that it can tell that we are ready.
				//(legacy had a commented-out GFCNETID_MERGEDPLAYLISTSRESPONSE send here; still not sent)
				}

			}
			break;
		
		case GFCNETID_PLAYLISTMERGEFINISHED:
			{
				//we are done with the merge!
				ss.str("");
				ss<<"Client: Finished Playlist Merge";
				networkLog.addToLog(ss.str());
				haveSentMyPlaylist=false;

				networkManager.sincStatus_Playlist=1;
				plateManager.setChanged();
			}
			break;

		case GFCNETID_SENDALLREADY:
		{
				ss.str("");
				ss<<"Client: All players are ready to start";
				networkLog.addToLog(ss.str());
				
				

				setStatusInternal("Online!", GFCCOLOR_GREEN);

				networkManager.sincStatus_LUT=1;
				networkManager.sincStatus_FX=1;
				networkManager.sincStatus_Stacks=1;
				networkManager.sincStatus_Playlist=1;

				networkManager.allReady=1;
				networkManager.handleAllReady();

		}
		break;

        case GFCNETID_REQUESTFXHASHES: {
			networkManager.handleSincStart(); //this is really where sync starts
            networkLog.addToLog( "Client: Got a request for FX Hashes");
            SendLoadedFXsHashes();


        }
        break;

        case GFCNETID_REQUESTFXS: {

            uint32_t howMany=0;
            if ( !r.readU32 ( howMany ) ) break; // malformed: skip silently

            jefe::wire::Writer outW;
            jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_REQUESTEDFXS ); //send back the requested FXs

            ss.str("");
            ss<<"Client: Server requested we send "<<(int)howMany << "FXs";
            networkLog.addToLog(ss.str());

            outW.writeU32 ( howMany ); //how many we are sending out (legacy semantics: requested count, loaded ones follow)

            printf ( "Client: Server requested we send %i FXs...", (int)howMany );

            bool readOk=true;
            for ( uint32_t i=0; i<howMany;i++ ) {
                std::string theText;
                if ( !r.readString ( theText ) ) { readOk=false; break; }
                gfcFX tmpFX=fxManager.getFXbyHash(theText.c_str());
                if ( tmpFX.loadedAndCompiled) {//the FX is indeed loaded, serialize it
                    serializeFX ( & tmpFX,outW );
                }
            }
            if ( !readOk ) break; // malformed request: send nothing

            //send the serialized FXs to the server.
            if ( outW.ok() ) transport_->send ( outW.data(), ( int ) outW.size(), serverPeerId_, false );
            printf("FXs sent\n");
        }
        break;
		case GFCNETID_FXSINCCOMPLETE:{

			ss.str("");
			ss<<"Client: FX Sinc Complete";
			networkLog.addToLog(ss.str());

			networkManager.sincStatus_FX=1;
			plateManager.setChanged();
		}
		break;

        case GFCNETID_MISSINGFXS: {
            //printf ( "Client: Got a GFCNETID_MISSINGFXS\n" );

            uint32_t howMany=0;
            if ( !r.readU32 ( howMany ) ) break; // malformed: skip silently

            printf ( " parsing and loading %i FXs\n",(int)howMany );

            for ( uint32_t i=0;i<howMany;i++ ) {


                if ( !unserializeFX ( r ) ) break; // truncated mid-list: stop (already-loaded FXs stay)

//                 else { //TODO:the unserializeFX method should return a value to indicate if the unserlization 			 //went well, the error can be gathered from the fxManager afterwards.
//                     char tmpMsg[32000];
//                     sprintf(tmpMsg,"Something horrible happened while loading the received FX:\n%s\nThe Fx was not compiled correctly, tell the Fxs provider to check the FXs source code, you should not use this FX until it is fixed:\n%s",theFX.getNameNoPath(),theFX.compilationError.c_str());
//                     fl_alert(tmpMsg);
//                 }
            }
        }
        break;

        case GFCNETID_CHATBROADCASTMESSAGE: {
            gfcChatLogEntry tmpEntry;
            if ( !jefe::wire::decodeChatEntry ( r, tmpEntry ) ) break; // malformed: skip silently

            chatLog.push_back ( tmpEntry );
            statusChange=true;
            gotNewChatMessage=true;
			plateManager.setChanged();
            //printf("Message received: %s\n",tmpEntry.getFormattedString().c_str());
            //chatFadeCounter=sett.chatFadeDelay/GFCNET_CHAT_FADE_SPEED;
        }
        break;

        case GFCNETID_POINTERINFOBROADCASTMESSAGE: {
            gfcNetRemotePointerInfo ptrInfo;
            ptrInfo.fadeCounter=sett.remotePointerFadeDelay; //receiver-local, not on the wire
            //reads quadID, x, y, scale, name, color (legacy order)
            if ( !jefe::wire::decodeRemotePointerInfo ( r, ptrInfo ) ) break; // malformed: skip silently

            //printf("got a GFCNETID_POINTERINFOBROADCASTMESSAGE\n");
            //each plate has their own pointer map, they store it and draw however they want, trails is up to the plate, not the networkmanager
            plateManager.storePointerInfo(ptrInfo);


        }
        break;

        case GFCNETID_TRANSFORMATIONMESSAGE: {
            std::vector< gfcNetTransformationInfo > transformations;
            if ( !jefe::wire::decodeTransformations ( r, transformations ) ) break; // malformed: skip silently

            //use the transformations
            networkManager.setTakeNotifications(false);
            plateManager.setTransformations(transformations);
            networkManager.setTakeNotifications(true);
        }
        break;
		
		case GFCNETID_COLORCORRECTIONMESSAGE:{
			std::vector< gfcNetPlateColorCorrectionInfo > corrections;
			if ( !jefe::wire::decodeColorCorrections ( r, corrections ) ) break; // malformed: skip silently

			//use the corrections
			networkManager.setTakeNotifications(false);
			plateManager.setColorCorrections(corrections);
			networkManager.setTakeNotifications(true);
		}
		break;

        case GFCNETID_OTHERSTATESMESSAGE: {
            //printf("Client: got a OTHERSTATES message\n");

            gfcNetOtherStatesInfo info;
            // Decode the whole message first (legacy interleaved reads with
            // manager applies; a garbage packet would have applied partial
            // state — we skip silently instead). Apply order below is
            // byte-identical to legacy: playbackInfo, layout, plateStateInfo,
            // trackStateInfo, all inside the takeNotifications(false) window.
            if ( !jefe::wire::decodeOtherStates ( r, info ) ) break; // malformed: skip silently

            networkManager.setTakeNotifications(false);

            playbackManager.setPlaybackInfo(info.playbackInfo);

            plateManager.setFramingMode(info.layout);

            plateManager.setPlateStateInfo(info.plateStateInfo);

            trackManager.setTrackStateInfo(info.trackStateInfo);

            networkManager.setTakeNotifications(true);
        }
        break;

        case GFCNETID_PLAYPAUSEMESSAGE: {

            gfcNetPlayPauseInfo tmp;
            if ( !jefe::wire::decodePlayPause ( r, tmp ) ) break; // malformed: skip silently

            networkManager.setTakeNotifications(false);
            playbackManager.setPlayPauseInfo(tmp);
            networkManager.setTakeNotifications(true);
        }
        break;

        

        case GFCNETID_FXADDMESSAGE: {

            //printf ( "Client: Got add FX message\n" );
            gfcNetFXAddInfo info;
            if ( !jefe::wire::decodeFXAdd ( r, info ) ) break; // malformed: skip silently

            networkManager.setTakeNotifications(false);
            plateManager.addFXToPlate(info.id.quadID, fxManager.getFXbyHash(info.id.hash) );
            networkManager.setTakeNotifications(true);

        }
        break;

        case GFCNETID_FXCOMMONMESSAGE: {
            //printf("Client: Got an FX common message\n");
            gfcNetFXCommonInfo message;
            if ( !jefe::wire::decodeFXCommon ( r, message ) ) break; // malformed: skip silently

            networkManager.setTakeNotifications(false);
            plateManager.processNetFXCommonInfo(message);
            networkManager.setTakeNotifications(true);

        }
        break;

        case GFCNETID_FXATTRIBMESSAGE: {
            //printf("Client: Got an FX attrib\n");
            gfcNetFXAttribInfo message;
            if ( !jefe::wire::decodeFXAttrib ( r, message ) ) break; // malformed: skip silently

            networkManager.setTakeNotifications(false);
            plateManager.processNetFXAttribInfo(message);
            networkManager.setTakeNotifications(true);
        }
        break;

        case GFCNETID_LAYERCHANGEMESSAGE: {
            // A peer switched the EXR layer/channel on a plate's track. Apply
            // the same channel name, then re-decode via the ASYNC loader — NOT
            // gfcSequence::loadPreview(), which calls generateTexture() (a GL
            // upload) and we're not in a current GL context here. The async
            // path decodes on the loader thread into rawFrames; the receiver's
            // per-tick generateTextures() (which does makeCurrent) uploads them.
            int quadID = 0;
            std::string layerName;
            if ( !jefe::wire::decodeLayerChange ( r, quadID, layerName ) ) break; // malformed: skip silently

            networkManager.setTakeNotifications(false);
            int track = plateManager.getTrackOnPlate(quadID);
            gfcSequence* seq = (track >= 0) ? trackManager.getSequence(track) : nullptr;
            if (seq && seq->myGUI) {
                seq->myGUI->setChannel(layerName.c_str());
                trackManager.startLoadingSequence(track);
            }
            networkManager.setTakeNotifications(true);
        }
        break;

		case GFCNETID_FXSTACKMESSAGE:
		{
			gfcNetFXStackMessage message;
			if ( !jefe::wire::decodeFXStack ( r, message ) ) break; // malformed: skip silently

			networkManager.setTakeNotifications(false);
			plateManager.processNetFXStackMessage(message);
			networkManager.setTakeNotifications(true);
		}
		break;
		
		case GFCNETID_SENDPLAYLIST:
		{
			//we got a new playlist
			printf("Got GFCNETID_SENDPLAYLIST\n");
			gfcNetPlaylistMessage plMessage;
			if ( !jefe::wire::decodePlaylistMessage ( r, plMessage ) ) break; // malformed: skip silently

			networkManager.setTakeNotifications(false);
			playlistManager.setPlaylistFromString(plMessage.thePlaylist,1);
			networkManager.setTakeNotifications(true);
		}
		break;

		case GFCNETID_PLAYLISTITEMLOADMESSAGE:
			{
				//READ THE PLAYLIST ITEM XML TEXT
				std::string itemXml;
				if ( !jefe::wire::decodePlaylistItem ( r, itemXml ) ) break; // malformed: skip silently
				//printf("PLAYLIST ITEM:\n\n%s\n\n",itemXml.c_str());

				gfcPlaylistItem tmpItem;
				XMLNode tmpNode;
				tmpNode= XMLNode::parseString(itemXml.c_str());
				//printf("NODE STRING: \n%s\n",tmpNode.createXMLString());
				
				tmpItem.loadPlaylistItemParameters(tmpNode);
				networkManager.setTakeNotifications(false);
				trackManager.setPlaylistItem(tmpItem, true);
				networkManager.setTakeNotifications(true);
			}
			break;

		case GFCNETID_PLAYLISTEVENTOTHER:
			{
				gfcNetPlaylistEvent tmpEvent;
				if ( !jefe::wire::decodePlaylistEvent ( r, tmpEvent ) ) break; // malformed: skip silently

				networkManager.setTakeNotifications(false);
				playlistManager.handlePlaylistEventOther(tmpEvent);
				networkManager.setTakeNotifications(true);
			}
			break;

//NEXT CASE GOES HERE

        }
        }
        break;

        default:
            break;
        }
    }
}

bool gfcNetworkClient::getIsConnected() {
    return isConnected;
}

bool gfcNetworkClient::getAttemptingConnection() {
    return attemptingConnection;
}

void gfcNetworkClient::SendLoadedFXsHashes(void ) {

    jefe::wire::Writer w;

    //printf("GFCNETID_LOADEDFXSHASHES: %i\n",GFCNETID_LOADEDFXSHASHES);
    jefe::wire::beginFrame ( w, ( uint16_t ) GFCNETID_LOADEDFXSHASHES );


    //get hashes from the fxManager
    std::vector<std::string> hashes=fxManager.getHashes();
    int numOfHashes= ( int ) hashes.size();
    //Write how many FXs we are sending.
    w.writeU32 ( ( uint32_t ) numOfHashes );
    std::stringstream ss (std::stringstream::in | std::stringstream::out);

    ss<< "Client: Sending FX Hashes (" <<numOfHashes << ")";
    networkLog.addToLog(ss.str().c_str());
    //Write each FXs hash
    std::vector<std::string>::iterator iter=hashes.begin(),end=hashes.end();
    for ( iter; iter!=end ;iter++ ) {
        w.writeString ( *iter );
    }

    //send!

    if ( w.ok() ) transport_->send ( w.data(), ( int ) w.size(), serverPeerId_, false );
}

void gfcNetworkClient::SendLoadedLUTsHashes(void ) {
    jefe::wire::Writer w;

    //printf("GFCNETID_LOADEDFXSHASHES: %i\n",GFCNETID_LOADEDFXSHASHES);
    jefe::wire::beginFrame ( w, ( uint16_t ) GFCNETID_LOADEDLUTSHASHES );

    //get hashes from the fxManager
    std::vector<std::string> hashes=lutManager.getHashes();
    int numOfHashes= ( int ) hashes.size();
    //Write how many FXs we are sending.
    w.writeU32 ( ( uint32_t ) numOfHashes );
    std::stringstream ss (std::stringstream::in | std::stringstream::out);

    ss<< "Client: Sending LUT Hashes (" <<numOfHashes << ")";
    networkLog.addToLog(ss.str().c_str());
    //Write each LUT hash
    std::vector<std::string>::iterator iter=hashes.begin(),end=hashes.end();
    for ( iter; iter!=end ;iter++ ) {
        w.writeString ( *iter );
    }

    //send!

    if ( w.ok() ) transport_->send ( w.data(), ( int ) w.size(), serverPeerId_, false );

}

void gfcNetworkClient::setIsServerClient(bool value) {
    isServerClient=value;
}

bool gfcNetworkClient::getIsServerClient() {
    return isServerClient;
}

jefe::net::PeerId gfcNetworkClient::getServerPeerId() {
    return serverPeerId_;
}


void gfcNetworkClient::SendChatMessage(std::string message, unsigned char type) {
    jefe::wire::Writer w;
    jefe::wire::beginFrame ( w, ( uint16_t ) GFCNETID_CHATMESSAGE );
    //write stuff here
    w.writeU8 ( type );
    w.writeString ( message );

    if ( w.ok() ) transport_->send ( w.data(), ( int ) w.size(), serverPeerId_, false );
}


void gfcNetworkClient::SendRemotePointerColor(int color)
{
	jefe::wire::Writer w;
	jefe::wire::beginFrame ( w, ( uint16_t ) GFCNETID_SENDREMOTEPOINTERCOLOR );
	//write stuff here
	w.writeI32 ( color );
	if ( w.ok() ) transport_->send ( w.data(), ( int ) w.size(), serverPeerId_, false );
}

std::vector< gfcChatLogEntry > gfcNetworkClient::getChatLog() {
    return chatLog;
}

bool gfcNetworkClient::getGotNewChatMessage() {
    bool result=gotNewChatMessage;
    gotNewChatMessage=false;
    return result;
}

void gfcNetworkClient::SendPointerInfoMessage(gfcNetPointerInfo info) {
    jefe::wire::Writer w;
    jefe::wire::beginFrame ( w, ( uint16_t ) GFCNETID_POINTERINFOMESSAGE );
    jefe::wire::encodePointerInfo ( w, info );
    if ( w.ok() ) transport_->send ( w.data(), ( int ) w.size(), serverPeerId_, false );
}


void gfcNetworkClient::SendTransformations(std::vector< gfcNetTransformationInfo > transformations) {
    jefe::wire::Writer w;
    jefe::wire::beginFrame ( w, ( uint16_t ) GFCNETID_TRANSFORMATIONMESSAGE );
    jefe::wire::encodeTransformations ( w, transformations );
    if ( w.ok() ) transport_->send ( w.data(), ( int ) w.size(), serverPeerId_, false );
}

void gfcNetworkClient::SendColorCorrections(std::vector<gfcNetPlateColorCorrectionInfo> corrections)
{
	jefe::wire::Writer w;
	jefe::wire::beginFrame ( w, ( uint16_t ) GFCNETID_COLORCORRECTIONMESSAGE );
	jefe::wire::encodeColorCorrections ( w, corrections );
	if ( w.ok() ) transport_->send ( w.data(), ( int ) w.size(), serverPeerId_, false );
}

void gfcNetworkClient::SendOtherStatesMessage ( gfcNetOtherStatesInfo info) {
    //printf ( "SendOtherSTatesMessage message:\n" );
    //printOtherStatesMessage(&message);
    jefe::wire::Writer w;
    jefe::wire::beginFrame ( w, ( uint16_t ) GFCNETID_OTHERSTATESMESSAGE );
    jefe::wire::encodeOtherStates ( w, info );
    if ( w.ok() ) transport_->send ( w.data(), ( int ) w.size(), serverPeerId_, false );
}

void gfcNetworkClient::SendPlayPauseMessage(gfcNetPlayPauseInfo info) {

    jefe::wire::Writer w;
    jefe::wire::beginFrame ( w, ( uint16_t ) GFCNETID_PLAYPAUSEMESSAGE );
    jefe::wire::encodePlayPause ( w, info );
    if ( w.ok() ) transport_->send ( w.data(), ( int ) w.size(), serverPeerId_, false );

}

void gfcNetworkClient::SendFXAddMessage(gfcNetFXAddInfo info) {
    jefe::wire::Writer w;
    jefe::wire::beginFrame ( w, ( uint16_t ) GFCNETID_FXADDMESSAGE );
    jefe::wire::encodeFXAdd ( w, info );
    if ( w.ok() ) transport_->send ( w.data(), ( int ) w.size(), serverPeerId_, false );
}

void gfcNetworkClient::SendFXCommonMessage(gfcNetFXCommonInfo info) {
    jefe::wire::Writer w;
    jefe::wire::beginFrame ( w, ( uint16_t ) GFCNETID_FXCOMMONMESSAGE );
    jefe::wire::encodeFXCommon ( w, info );
    if ( w.ok() ) transport_->send ( w.data(), ( int ) w.size(), serverPeerId_, false );
}

void gfcNetworkClient::SendFXAttribMessage(gfcNetFXAttribInfo info) {
    jefe::wire::Writer w;
    jefe::wire::beginFrame ( w, ( uint16_t ) GFCNETID_FXATTRIBMESSAGE );
    jefe::wire::encodeFXAttrib ( w, info );
    if ( w.ok() ) transport_->send ( w.data(), ( int ) w.size(), serverPeerId_, false );
}

void gfcNetworkClient::SendFXStackMessage(gfcNetFXStackMessage message)
{
	jefe::wire::Writer w;
	jefe::wire::beginFrame ( w, ( uint16_t ) GFCNETID_FXSTACKMESSAGE );
	jefe::wire::encodeFXStack ( w, message ); //legacy explicit length(+5) field dropped: writeString carries the length
	if ( w.ok() ) transport_->send ( w.data(), ( int ) w.size(), serverPeerId_, false );
}

void gfcNetworkClient::SendLayerChangeMessage(int quadID, std::string layerName)
{
	jefe::wire::Writer w;
	jefe::wire::beginFrame ( w, ( uint16_t ) GFCNETID_LAYERCHANGEMESSAGE );
	jefe::wire::encodeLayerChange ( w, quadID, layerName );
	if ( w.ok() ) transport_->send ( w.data(), ( int ) w.size(), serverPeerId_, false );
}

void gfcNetworkClient::SendPlaylistMessage(gfcNetPlaylistMessage message){
	jefe::wire::Writer w;
	jefe::wire::beginFrame ( w, ( uint16_t ) GFCNETID_SENDPLAYLIST );
	jefe::wire::encodePlaylistMessage ( w, message ); //legacy explicit length(+5) field dropped: writeString carries the length
	if ( w.ok() ) transport_->send ( w.data(), ( int ) w.size(), serverPeerId_, false );
}

void gfcNetworkClient::sendPlaylistEvent(gfcNetPlaylistEvent theEvent)
{
	jefe::wire::Writer w;
	jefe::wire::beginFrame ( w, ( uint16_t ) GFCNETID_PLAYLISTEVENTOTHER );
	jefe::wire::encodePlaylistEvent ( w, theEvent );
	if ( w.ok() ) transport_->send ( w.data(), ( int ) w.size(), serverPeerId_, false );
}

void gfcNetworkClient::SendPlaylistItem(gfcPlaylistItem item)
{
	std::string theString = item.asString();
	jefe::wire::Writer w;
	jefe::wire::beginFrame ( w, ( uint16_t ) GFCNETID_PLAYLISTITEMLOADMESSAGE );
	jefe::wire::encodePlaylistItem ( w, theString );
	if ( w.ok() ) transport_->send ( w.data(), ( int ) w.size(), serverPeerId_, false );
}


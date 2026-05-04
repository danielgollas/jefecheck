#include "gfcnetworkclient.h"
#include "gfcnetworkclientgui_fltk.h"


#include "RakPeerInterface.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <stdlib.h> // For atoi
#include <cstring> // For strlen
#include <sstream> //for stingstream
#include "Rand.h"
#include "RakNetStatistics.h"
#include "RakNetworkFactory.h"
#include "MessageIdentifiers.h"
#include <stdio.h>
#include "GetTime.h"
#include "RakAssert.h"
#include "RakSleep.h"
#include "BitStream.h"
#include "StringCompressor.h"
#include "gfcNetworkStructures.h"
#include "FL/fl_ask.H"

#include "remoteWindow.h"


extern RemoteWindow rmw;

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
    myGUI=new gfcNetworkClientGUI_FLTK;
    peer = RakNetworkFactory::GetRakPeerInterface();
	haveSentMyPlaylist=false;
}


gfcNetworkClient::~gfcNetworkClient() {
}

void gfcNetworkClient::Startup() {
    static SocketDescriptor socketDescriptor;
    socketDescriptor.port=0;
    peer->Startup ( 1,15,&socketDescriptor,1 );
    isConnected=false;


}

bool gfcNetworkClient::Connect(gfcConnectionParams * params) {
    bool b;
	

	
    std::string theServerIP;
    int thePort;
	
    std::string thePassword;

    if (params) {
        theServerIP=params->serverIP;
        thePort=params->port;
        thePassword=params->password;
        this->nickName=params->nickname;
    } else {
        theServerIP=myGUI->getIPAddress();
        thePort=myGUI->getPort();
        thePassword=myGUI->getPassword();
        this->nickName=myGUI->getName();
    }
	std::string conectingMessage="Client: Starting Connection to:";
	conectingMessage+=theServerIP;
	conectingMessage+=":";
	char thePortString[30];
	sprintf(thePortString,"%i",thePort);
	conectingMessage+=thePortString;
	networkLog.addToLog(conectingMessage);
    peer->Shutdown ( 30 );
    Startup();
    b=peer->Connect ( theServerIP.c_str(), ( unsigned short ) thePort, thePassword.c_str(), thePassword.size(), 0 );
    if ( b==false ) {
        printf ( "Client connect call failed!\n" );
        networkLog.addToLog("Client: Client connect call failed!",GFCNETLOGTYPE_ALERT);
        Disconnect();
    }

    myGUI->setStartStopButton("Cancel");
    myGUI->setStatus("Attempting Connection...",GFCCOLOR_GRAY);
    
	//since the connection attempt was succesful, save the ip and port to the recent
	this->saveCurrentToRecentIPs();

	//statusChange=true;
    /*strcpy ( status, "Attempting Connection" );

    insertIntoNetLog ( "Starting Connection" );*/
    attemptingConnection=b;
    return b;
}

void gfcNetworkClient::initializeWidgets() {
    myGUI->assignStartStopButtonWidget(rmw.connectButton);
    myGUI->assignPasswordWidget(rmw.password);
    myGUI->assignNameInputWidget(rmw.nickname);
    myGUI->assignPortInputWidget(rmw.port);
    myGUI->assignIPInputWidget(rmw.ip);
    myGUI->assignRemoteRecent(rmw.remoteRecent);
    myGUI->assignStatusWidget(rmw.status);
    myGUI->assignPeersInSessionWidget(rmw.peersInSession);
}

void gfcNetworkClient::saveCurrentToRecentIPs()
{
	std::stringstream mashed;
	mashed<<myGUI->getIPAddress();
	mashed<<":";
	mashed<<myGUI->getPort();

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
	
	myGUI->setRecent(sett.recentIPs);

}

void gfcNetworkClient::setRecent(std::vector<std::string> recents){
	myGUI->setRecent(recents);
}

void gfcNetworkClient::setAddress(std::string pip, std::string pport)
{
	myGUI->setIPAddress(pip);
	myGUI->setPort(atoi(pport.c_str()));
	this->saveCurrentToRecentIPs();
}

void gfcNetworkClient::disableGUI() {
    myGUI->disable();
}

void gfcNetworkClient::enableGUI() {
    myGUI->enable();
}

void gfcNetworkClient::Disconnect() {
    peer->CloseConnection ( peer->GetSystemAddressFromIndex ( 0 ),true,0 );
    std::vector<std::string> emptyVector;
    myGUI->setPeersInSession(emptyVector);

    if (attemptingConnection) {
        myGUI->setStatus("Offline: Connection Attempt Canceled",GFCCOLOR_GRAY);
        networkLog.addToLog("Client: Connection Attempt Canceled");
    }
	else
	{
		myGUI->setStatus("Offline",GFCCOLOR_GRAY);
		networkLog.addToLog("Client: Ended Session");
	}


    isConnected=false;
    attemptingConnection=false;
	peer->Shutdown(30);
    myGUI->setStartStopButton("Connect");

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
    Packet *p = peer->Receive();
    while ( p ) {
		gotMessages=true;
        std::stringstream ss (std::stringstream::in | std::stringstream::out);
        switch ( p->data[0] ) {
        case ID_CONNECTION_REQUEST_ACCEPTED: {
            printf("Client Connected!\n");
            myGUI->setStatus("Connected!... getting acquainted with everyone",GFCCOLOR_YELLOW);
			networkManager.handleSincStart();
            serverSystemAddress=p->systemAddress;
            myGUI->setStartStopButton("Disconnect");
            RakNet::BitStream bs;
            attemptingConnection=false;
            bs.Write ( ( unsigned char ) GFCNETID_NICKNAMESEND );
            StringCompressor::Instance()->EncodeString ( nickName.c_str(),GFCNET_MAX_NICKNAME_LENGHT,&bs );
			//also send our pointer color
			bs.WriteCompressed(sett.remotePointerColor);
            peer->Send ( &bs,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,0 );
            networkLog.addToLog("Client: Connection Request Accepted!");
        }
        break;

        case ID_CONNECTION_ATTEMPT_FAILED: {

            if (attemptingConnection) {
                printf ( "Client Error: ID_CONNECTION_ATTEMPT_FAILED\n" );

                myGUI->setStatus("Offline, Connection Attempt Failed",GFCCOLOR_RED);
                attemptingConnection=false;
                Disconnect();
            }
            networkLog.addToLog("Client: Connection Attempt Failed",GFCNETLOGTYPE_ALERT);


        }
        break;
        case ID_ALREADY_CONNECTED:
            printf ( "Client Error: ID_ALREADY_CONNECTED\n" );
            networkLog.addToLog("Client: Already Connected!",GFCNETLOGTYPE_ALERT);
            break;

        case ID_NO_FREE_INCOMING_CONNECTIONS: {
            printf ( "Client Error: ID_NO_FREE_INCOMING_CONNECTIONS\n" );
            myGUI->setStatus("Could not Connect, no free incoming connections on server",GFCCOLOR_RED);
            networkLog.addToLog("Client: Could not Connect, no free incoming connections on server",GFCNETLOGTYPE_ALERT);
            Disconnect();
        }
        break;

        case ID_DISCONNECTION_NOTIFICATION: {
            printf("ID_DISCONNECTION_NOTIFICATION\n");
            //gIsServer=false; //WE DON'T STOP BEING THE SERVER UNTIL THE INTERNAL CLIENT DISCONNECTS
            myGUI->setStatus("Offline, Disconnected from Server",GFCCOLOR_GRAY);
            Disconnect();
            networkLog.addToLog("Client: Disconnected from Server");
        }
        break;

        case ID_CONNECTION_LOST: {
            printf ( "Client Error: ID_CONNECTION_LOST\n" );


            myGUI->setStatus("Offline, Connection Lost",GFCCOLOR_RED);
            Disconnect();
            networkLog.addToLog("Client: Connection Lost",GFCNETLOGTYPE_ALERT);
            //insertIntoNetLog ( "Connection Lost" );
        }
        break;
        case ID_MODIFIED_PACKET:
            printf ( "Client Error: ID_MODIFIED_PACKET\n" );
            networkLog.addToLog("Client: Modified Packet!",GFCNETLOGTYPE_ALERT);
            break;

        case GFCNETID_NICKALREADYINUSE: {
            fl_alert ( "Nick already taken!");
            networkLog.addToLog("Client: Could not connect to server, nickname already taken!",GFCNETLOGTYPE_ALERT);
            Disconnect();
        }
        break;

        case GFCNETID_PEERSINSESSION: {
            //printf ( "Got a GFCNETID_PEERSINSESSION!\n" );
            int numOfPeers;
            RakNet::BitStream bs ( p->data,p->length,true );
            bs.IgnoreBits ( 8 );
            bs.Read ( numOfPeers );
            char tmpNickname[GFCNET_MAX_NICKNAME_LENGHT];

            // printf ( "Size of int: %i\n",sizeof ( int ) );


            std::vector<std::string> tmpPeersInSession;
            for ( int i=0;i<numOfPeers;i++ ) {

                StringCompressor::Instance()->DecodeString ( tmpNickname,GFCNET_MAX_NICKNAME_LENGHT,&bs );
                tmpPeersInSession.push_back(tmpNickname);

                //rmw.peersInSession->add ( tmpNickname );
            }

            myGUI->setPeersInSession(tmpPeersInSession);
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
            int howMany=0;
            RakNet::BitStream bs ( p->data,p->length,0 );
            RakNet::BitStream outBS;
            outBS.Write ( ( unsigned char ) GFCNETID_REQUESTEDLUTS ); //send back the requested LUTs

            bs.IgnoreBits ( 8 );
            bs.Read ( howMany );
            outBS.Write ( ( int ) howMany ); //how many we are sending out

            char theText[40];
            printf ( " %i LUTs Requested\n", howMany );


            for ( int i=0; i<howMany;i++ ) {
                StringCompressor::Instance()->DecodeString ( theText,40,&bs );
                CubeLUT tmpLUT=lutManager.getLUTbyHash(theText);
                if ( strcmp(tmpLUT.name,"")!=0 ) {//the LUT is indeed loaded, serialize it

                    serializeLUT ( & tmpLUT,&outBS );

                }
            }

            //send the serialized LUTs to the server.
            peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,0 );

        }
        break;

        case GFCNETID_MISSINGLUTS: {
            //printf ( "Client: Got a GFCNETID_MISSINGLUTS\n" );

            RakNet::BitStream bs ( p->data,p->length,0 );

            int howMany;
            bs.IgnoreBits ( 8 );

            bs.Read ( howMany );

            printf ( " parsing and loading %i LUTs\n",howMany );

            for ( int i=0;i<howMany;i++ ) {
                //TODO: Check for errors loading LUTS
                unserializeLUT ( &bs );

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
				RakNet::BitStream bs ( p->data,p->length,0 );
				
				bs.IgnoreBits ( 8 );
				
				int howLong;
				bs.ReadCompressed( howLong );
				
				char *theText=new char[howLong];
				StringCompressor::Instance()->DecodeString ( theText,howLong,&bs );
				std::string stackString=theText;
				delete [] theText;

				//std::cout << "Client Received FX stack: \n" << stackString;
				
				//setting takeNotifications to false prevents loops
				networkManager.setTakeNotifications(false);
				plateManager.setPlateFXStacksFromString(stackString);
				networkManager.setTakeNotifications(true);

				//send stack received.
				RakNet::BitStream outBS;
				outBS.Write ( ( unsigned char ) GFCNETID_RECEIVEDFXSTACKS ); //send back the requested FXs
				peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,0 );

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
			
			RakNet::BitStream outBS;
			outBS.Write ( ( unsigned char ) GFCNETID_SENDPLAYLISTFORMERGE );
			
			std::string pl=playlistManager.getPlaylistAsString();
			//printf("Client: sending my pl: %s\n",pl.c_str());
			int plLength=pl.length();
			plLength+=5;
			outBS.WriteCompressed(plLength);
			
			StringCompressor::Instance()->EncodeString ( pl.c_str(),plLength,&outBS );
			peer->Send( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,false);
			haveSentMyPlaylist=true;
		}
		break;

		case GFCNETID_MERGEDPLAYLISTS:
			{
				//printf("Client got GFCNETID_MERGEDPLAYLISTS\n");
				
				
				
				//read the playlist and load it (making sure it is replaced) 
				RakNet::BitStream bs ( p->data,p->length,0 );

				bs.IgnoreBits ( 8 );

				int howLong;
				bs.ReadCompressed(howLong);

				char *tmpPl=new char[howLong];
				StringCompressor::Instance()->DecodeString ( tmpPl,howLong,&bs );
				std::string thePl=tmpPl;
				delete [] tmpPl;
				
				
				
				if (haveSentMyPlaylist==true)
				{
				//we only overwrite ours if we already sent it before, otherwise, we might get a merged playlist and ours will be left in oblivion.
				//printf("Client received this pl:\n%s***",thePl.c_str());
				networkManager.setTakeNotifications(false);
				playlistManager.setPlaylistFromString(thePl,1);
				networkManager.setTakeNotifications(true);
				
				//we should send a reply to notify the server so that it can tell that we are ready.
				/*RakNet::BitStream outBS;
				outBS.Write ( ( unsigned char ) GFCNETID_MERGEDPLAYLISTSRESPONSE );
				peer->Send( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,false);*/
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
				
				

				myGUI->setStatus("Online!",GFCCOLOR_GREEN);
				
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

            int howMany=0;
            RakNet::BitStream bs ( p->data,p->length,0 );
            RakNet::BitStream outBS;
            outBS.Write ( ( unsigned char ) GFCNETID_REQUESTEDFXS ); //send back the requested FXs

            bs.IgnoreBits ( 8 );
            bs.Read ( howMany );

            ss.str("");
            ss<<"Client: Server requested we send "<<howMany << "FXs";
            networkLog.addToLog(ss.str());

            outBS.Write ( ( int ) howMany ); //how many we are sending out

            char theText[40];
            printf ( "Client: Server requested we send %i FXs...", howMany );


            for ( int i=0; i<howMany;i++ ) {
                StringCompressor::Instance()->DecodeString ( theText,40,&bs );
                gfcFX tmpFX=fxManager.getFXbyHash(theText);
                if ( tmpFX.loadedAndCompiled) {//the FX is indeed loaded, serialize it
                    serializeFX ( & tmpFX,&outBS );
                }
            }

            //send the serialized FXs to the server.
            peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,0 );
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

            RakNet::BitStream bs ( p->data,p->length,0 );

            int howMany;
            bs.IgnoreBits ( 8 );

            bs.Read ( howMany );

            printf ( " parsing and loading %i FXs\n",howMany );

            for ( int i=0;i<howMany;i++ ) {


                unserializeFX ( &bs );

//                 else { //TODO:the unserializeFX method should return a value to indicate if the unserlization 			 //went well, the error can be gathered from the fxManager afterwards.
//                     char tmpMsg[32000];
//                     sprintf(tmpMsg,"Something horrible happened while loading the received FX:\n%s\nThe Fx was not compiled correctly, tell the Fxs provider to check the FXs source code, you should not use this FX until it is fixed:\n%s",theFX.getNameNoPath(),theFX.compilationError.c_str());
//                     fl_alert(tmpMsg);
//                 }
            }
        }
        break;

        case GFCNETID_CHATBROADCASTMESSAGE: {
            //printf ( "Recieved chat message: %s\n", ( ( gfcNetChatBroadcastMessage* ) p->data )->text );
            char message[GFCNET_MAX_TEXT_LENGHT];
            unsigned char messageType;
            RakNet::BitStream bs ( p->data,p->length,false );
            bs.IgnoreBits ( 8 );
            gfcChatLogEntry tmpEntry;

            bs.Read(messageType);
            tmpEntry.type=messageType;

            StringCompressor::Instance()->DecodeString ( message,GFCNET_MAX_TEXT_LENGHT,&bs ); //time
            tmpEntry.time=message;

            StringCompressor::Instance()->DecodeString ( message,GFCNET_MAX_TEXT_LENGHT,&bs ); //sender
            tmpEntry.sender=message;

            StringCompressor::Instance()->DecodeString ( message,GFCNET_MAX_TEXT_LENGHT,&bs ); //message
            tmpEntry.message=message;

            chatLog.push_back ( tmpEntry );
            statusChange=true;
            gotNewChatMessage=true;
			plateManager.setChanged();
            //printf("Message received: %s\n",tmpEntry.getFormattedString().c_str());
            //chatFadeCounter=sett.chatFadeDelay/GFCNET_CHAT_FADE_SPEED;
        }
        break;

        case GFCNETID_POINTERINFOBROADCASTMESSAGE: {
            //printf("Client: Got a pointerInfoMessage from %s: %f %f\n",((gfcNetPointerInfoBroadcastMessage*)p->data)->nickname,((gfcNetPointerInfoBroadcastMessage*)p->data)->info.x,((gfcNetPointerInfoBroadcastMessage*)p->data)->info.y);
            RakNet::BitStream bs ( p->data,p->length,true );
            bs.IgnoreBits ( 8 );



            gfcNetRemotePointerInfo ptrInfo;
            ptrInfo.fadeCounter=sett.remotePointerFadeDelay;
            bs.ReadCompressed ( ptrInfo.quadID );
            bs.ReadCompressed ( ptrInfo.x );
            //printf("ptrInfo.x for quad %i: %i\n",ptrInfo.quadID,ptrInfo.x);
            bs.ReadCompressed ( ptrInfo.y );
            bs.Read(ptrInfo.scale);
            char ptrnickname[GFCNET_MAX_TEXT_LENGHT];
            StringCompressor::Instance()->DecodeString ( ptrnickname,GFCNET_MAX_TEXT_LENGHT,&bs );
            ptrInfo.name = ptrnickname;

			//also get the color
			int theColor;
			bs.ReadCompressed(ptrInfo.color);
			


            //printf("got a GFCNETID_POINTERINFOBROADCASTMESSAGE\n");
            //each plate has their own pointer map, they store it and draw however they want, trails is up to the plate, not the networkmanager
            plateManager.storePointerInfo(ptrInfo);


        }
        break;

        case GFCNETID_TRANSFORMATIONMESSAGE: {
            RakNet::BitStream bs ( p->data,p->length,false );
            bs.IgnoreBits ( 8 );
            std::vector< gfcNetTransformationInfo > transformations;
            gfcNetTransformationInfo tmp;
            int howMany=0;
            bs.ReadCompressed(howMany);
            //read all the transformations
            //printf("Reading %i transformations\n",howMany);
            for ( int i=0;i<howMany ;i++ ) {
                bs.Read(tmp.tX);
                bs.Read(tmp.tY);
                bs.Read(tmp.scale);
				bs.Read(tmp.rZ);
                transformations.push_back(tmp);
            }

            //use the transformations
            networkManager.setTakeNotifications(false);
            plateManager.setTransformations(transformations);
            networkManager.setTakeNotifications(true);
        }
        break;
		
		case GFCNETID_COLORCORRECTIONMESSAGE:{
			RakNet::BitStream bs ( p->data,p->length,false );
			bs.IgnoreBits ( 8 );
			std::vector< gfcNetPlateColorCorrectionInfo > corrections;
			gfcNetPlateColorCorrectionInfo tmp;
			int howMany=0;
			bs.ReadCompressed(howMany);
			//read all the transformations
			//printf("Reading %i transformations\n",howMany);
			for ( int i=0;i<howMany ;i++ ) {
				//read the lut and other color correction info
				char tmpCharlut[GFCNET_MAX_TEXT_LENGHT];
				bs.ReadCompressed(tmp.quadID);
				StringCompressor::Instance()->DecodeString ( tmpCharlut,GFCNET_MAX_TEXT_LENGHT,&bs );
				tmp.lutName=tmpCharlut;
				bs.Read(tmp.gamma);
				bs.Read(tmp.exposure);
				bs.Read(tmp.brightness);
				bs.Read(tmp.contrast);
				bs.Read(tmp.saturation);
				corrections.push_back(tmp);
			}

			//use the corrections
			networkManager.setTakeNotifications(false);
			plateManager.setColorCorrections(corrections);
			networkManager.setTakeNotifications(true);
		}
		break;

        case GFCNETID_OTHERSTATESMESSAGE: {
            //printf("Client: got a OTHERSTATES message\n");

            RakNet::BitStream bs ( p->data,p->length,false );
            bs.IgnoreBits ( 8 );
            gfcNetOtherStatesInfo info;


            networkManager.setTakeNotifications(false);
            //playback info stuff
            bs.ReadCompressed (  info.playbackInfo.from );
            bs.ReadCompressed (  info.playbackInfo.to );
            bs.Read (  info.playbackInfo.targetFPS );
            bs.ReadCompressed (  info.playbackInfo.playbackMode );
            bs.ReadCompressed (  info.playbackInfo.loopPriority );
			
			bs.ReadCompressed(info.playbackInfo.inPoint);
			bs.ReadCompressed(info.playbackInfo.outPoint);

            playbackManager.setPlaybackInfo(info.playbackInfo);

            //plate stuff and layout
            bs.ReadCompressed ( info.layout );
            int plateStateInfoSize;
            bs.ReadCompressed(plateStateInfoSize);
            gfcNetPlateStateInfo plateTmp;
            for ( int i=0;i<plateStateInfoSize;i++) {

                bs.Read( plateTmp.track);
                bs.ReadCompressed(plateTmp.quadID);
                bs.Read( plateTmp.flip);
                bs.Read( plateTmp.flop);
                bs.Read( plateTmp.a);
                bs.Read( plateTmp.r);
                bs.Read( plateTmp.g);
                bs.Read( plateTmp.b);
                char tmpChar[20];
                StringCompressor::Instance()->DecodeString ( tmpChar,20,&bs );
                plateTmp.aspect=tmpChar;
                //std::cout << "aspect received: " << plateTmp.aspect <<std::endl;
                bs.Read(plateTmp.crop);
                info.plateStateInfo.push_back(plateTmp);
            }

            plateManager.setFramingMode(info.layout);

            plateManager.setPlateStateInfo(info.plateStateInfo);

            //track stuff
            int trackStateInfoSize;
            bs.ReadCompressed(trackStateInfoSize);

            gfcNetTrackStateInfo trackTmp;
            for (  int i=0;i<trackStateInfoSize;i++) {
                bs.ReadCompressed(  trackTmp.frameOffset);
                bs.ReadCompressed(  trackTmp.holdMode);
                bs.ReadCompressed(  trackTmp.holdFrame);
                info.trackStateInfo.push_back(trackTmp);
            }

            trackManager.setTrackStateInfo(info.trackStateInfo);

            networkManager.setTakeNotifications(true);
        }
        break;

        case GFCNETID_PLAYPAUSEMESSAGE: {

            RakNet::BitStream bs ( p->data,p->length,false );
            bs.IgnoreBits ( 8 );
            gfcNetPlayPauseInfo tmp;
            bs.Read ( tmp.play);
            bs.ReadCompressed ( tmp.frame );
            bs.ReadCompressed ( tmp.direction);

            networkManager.setTakeNotifications(false);
            playbackManager.setPlayPauseInfo(tmp);
            networkManager.setTakeNotifications(true);
        }
        break;

        

        case GFCNETID_FXADDMESSAGE: {

            //printf ( "Client: Got add FX message\n" );
            RakNet::BitStream bs ( p->data,p->length,false );
            bs.IgnoreBits ( 8 );
            gfcNetFXAddInfo info;
            bs.ReadCompressed ( info.id.quadID );
            char tmpHash[40];
            StringCompressor::Instance()->DecodeString ( tmpHash,GFCNET_MAX_TEXT_LENGHT,&bs );
            info.id.hash=tmpHash;

            networkManager.setTakeNotifications(false);
            plateManager.addFXToPlate(info.id.quadID, fxManager.getFXbyHash(info.id.hash) );
            networkManager.setTakeNotifications(true);

        }
        break;

        case GFCNETID_FXCOMMONMESSAGE: {
            //printf("Client: Got an FX common message\n");
            gfcNetFXCommonInfo message;
            RakNet::BitStream bs ( p->data,p->length,false );
            bs.IgnoreBits ( 8 );
	    
            bs.ReadCompressed ( message.id.index );
            bs.ReadCompressed ( message.id.quadID );

            bs.ReadCompressed ( message.onOff );
            bs.ReadCompressed ( message.upDown );
            bs.Read ( message.reset );
            bs.Read ( message.remove );

            networkManager.setTakeNotifications(false);
            plateManager.processNetFXCommonInfo(message);
            networkManager.setTakeNotifications(true);

        }
        break;

        case GFCNETID_FXATTRIBMESSAGE: {
            //printf("Client: Got an FX attrib\n");
            gfcNetFXAttribInfo message;
            RakNet::BitStream bs ( p->data,p->length,false );
            bs.IgnoreBits ( 8 );
            
            char tmpChar[120];
            
            bs.ReadCompressed ( message.id.index );
            bs.ReadCompressed ( message.id.quadID );

            bs.Read ( message.attribType );
            bs.ReadCompressed ( message.theInt );
            bs.Read ( message.theFloat );
            StringCompressor::Instance()->DecodeString ( tmpChar,GFCNET_MAX_TEXT_LENGHT,&bs );
            message.lutOrCube=tmpChar;
            StringCompressor::Instance()->DecodeString ( tmpChar,GFCNET_MAX_TEXT_LENGHT,&bs );
            message.groupName=tmpChar;
            StringCompressor::Instance()->DecodeString ( tmpChar,GFCNET_MAX_TEXT_LENGHT,&bs );
            message.variableName=tmpChar;
                        
            networkManager.setTakeNotifications(false);
            plateManager.processNetFXAttribInfo(message);
            networkManager.setTakeNotifications(true);
        }
        break;
		
		case GFCNETID_FXSTACKMESSAGE:
		{
			gfcNetFXStackMessage message;
			RakNet::BitStream bs ( p->data,p->length,false );
			bs.IgnoreBits ( 8 );

			
			bs.ReadCompressed ( message.quadID);

			int stackLenght=0;
			bs.ReadCompressed(stackLenght);
			char *theStack=new char[stackLenght];
			StringCompressor::Instance()->DecodeString ( theStack,stackLenght,&bs );
			message.theStack=theStack;
			delete [] theStack;
						
			networkManager.setTakeNotifications(false);
			plateManager.processNetFXStackMessage(message);
			networkManager.setTakeNotifications(true);
		}
		break;
		
		case GFCNETID_SENDPLAYLIST:
		{
			//we got a new playlist
			printf("Got GFCNETID_SENDPLAYLIST\n");
			RakNet::BitStream bs ( p->data,p->length,false );
			bs.IgnoreBits ( 8 );
			
			int plLenght=0;
			bs.ReadCompressed(plLenght);
			char *thePL=new char[plLenght];
			StringCompressor::Instance()->DecodeString ( thePL,plLenght,&bs );
			std::string plString=thePL;
			delete [] thePL;

			networkManager.setTakeNotifications(false);
			playlistManager.setPlaylistFromString(plString,1);
			networkManager.setTakeNotifications(true);
		}
		break;

		case GFCNETID_PLAYLISTITEMLOADMESSAGE:
			{
				//READ THE PLAYLIST ITEM XML TEXT
				char message[GFCNET_MAX_TEXT_LENGHT];
				
				RakNet::BitStream bs ( p->data,p->length,false );
				bs.IgnoreBits ( 8 );
				StringCompressor::Instance()->DecodeString ( message,GFCNET_MAX_TEXT_LENGHT,&bs );
				//printf("PLAYLIST ITEM:\n\n%s\n\n",message);

				gfcPlaylistItem tmpItem;
				XMLNode tmpNode;
				tmpNode= XMLNode::parseString(message);
				//printf("NODE STRING: \n%s\n",tmpNode.createXMLString());
				
				tmpItem.loadPlaylistItemParameters(tmpNode);
				networkManager.setTakeNotifications(false);
				trackManager.setPlaylistItem(tmpItem, true);
				networkManager.setTakeNotifications(true);
			}
			break;

		case GFCNETID_PLAYLISTEVENTOTHER:
			{
				RakNet::BitStream bs ( p->data,p->length,false );
				bs.IgnoreBits ( 8 );
				gfcNetPlaylistEvent tmpEvent;
				bs.ReadCompressed(tmpEvent.selectedItem);
				
				networkManager.setTakeNotifications(false);
				playlistManager.handlePlaylistEventOther(tmpEvent);
				networkManager.setTakeNotifications(true);
			}
			break;

//NEXT CASE GOES HERE

        }

        peer->DeallocatePacket ( p );
        p = peer->Receive();
    }
}

bool gfcNetworkClient::getIsConnected() {
    return isConnected;
}

bool gfcNetworkClient::getAttemptingConnection() {
    return attemptingConnection;
}

void gfcNetworkClient::SendLoadedFXsHashes(void ) {

    RakNet::BitStream bs;

    //printf("GFCNETID_LOADEDFXSHASHES: %i\n",GFCNETID_LOADEDFXSHASHES);
    bs.Write ( ( unsigned char ) GFCNETID_LOADEDFXSHASHES );


    //get hashes from the fxManager
    std::vector<std::string> hashes=fxManager.getHashes();
    int numOfHashes= ( int ) hashes.size();
    //Write how many FXs we are sending.
    bs.Write ( ( int ) numOfHashes );
    std::stringstream ss (std::stringstream::in | std::stringstream::out);

    ss<< "Client: Sending FX Hashes (" <<numOfHashes << ")";
    networkLog.addToLog(ss.str().c_str());
    //Write each FXs hash
    std::vector<std::string>::iterator iter=hashes.begin(),end=hashes.end();
    for ( iter; iter!=end ;iter++ ) {
        StringCompressor::Instance()->EncodeString ( ( *iter ).c_str(),40,&bs );
    }

    //send!

    peer->Send ( &bs,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,0 );
}

void gfcNetworkClient::SendLoadedLUTsHashes(void ) {
    RakNet::BitStream bs;

    //printf("GFCNETID_LOADEDFXSHASHES: %i\n",GFCNETID_LOADEDFXSHASHES);
    bs.Write ( ( unsigned char ) GFCNETID_LOADEDLUTSHASHES );
    //bs.Write(5);

    //get hashes from the fxManager
    std::vector<std::string> hashes=lutManager.getHashes();
    int numOfHashes= ( int ) hashes.size();
    //Write how many FXs we are sending.
    bs.Write ( ( int ) numOfHashes );
    std::stringstream ss (std::stringstream::in | std::stringstream::out);

    ss<< "Client: Sending LUT Hashes (" <<numOfHashes << ")";
    networkLog.addToLog(ss.str().c_str());
    //Write each LUT hash
    std::vector<std::string>::iterator iter=hashes.begin(),end=hashes.end();
    for ( iter; iter!=end ;iter++ ) {
        StringCompressor::Instance()->EncodeString ( ( *iter ).c_str(),40,&bs );
    }

    //send!

    peer->Send ( &bs,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,0 );

}

void gfcNetworkClient::setIsServerClient(bool value) {
    isServerClient=value;
}

bool gfcNetworkClient::getIsServerClient() {
    return isServerClient;
}

SystemAddress gfcNetworkClient::getServerSystemAddress() {
    return serverSystemAddress;
}


void gfcNetworkClient::SendChatMessage(std::string message, unsigned char type) {
    RakNet::BitStream outBS;
    outBS.Write ( ( unsigned char ) GFCNETID_CHATMESSAGE );
    //write stuff here
    outBS.Write(type);
    StringCompressor::Instance()->EncodeString ( message.c_str(),GFCNET_MAX_TEXT_LENGHT,&outBS );

    peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,false );
}


void gfcNetworkClient::SendRemotePointerColor(int color)
{
	RakNet::BitStream outBS;
	outBS.Write ( ( unsigned char ) GFCNETID_SENDREMOTEPOINTERCOLOR );
	//write stuff here
	outBS.WriteCompressed(color);
	peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,false );
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
    RakNet::BitStream outBS;
    outBS.Write ( ( unsigned char ) GFCNETID_POINTERINFOMESSAGE );
    outBS.WriteCompressed ( ( int ) info.quadID );
    outBS.WriteCompressed ( ( int ) info.x );
    outBS.WriteCompressed ( ( int ) info.y );
    outBS.Write((float) info.scale);
    peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,false );
}


void gfcNetworkClient::SendTransformations(std::vector< gfcNetTransformationInfo > transformations) {
    RakNet::BitStream outBS;
    std::vector< gfcNetTransformationInfo >::iterator iter=transformations.begin(), end=transformations.end();
    outBS.Write ( ( unsigned char ) GFCNETID_TRANSFORMATIONMESSAGE );
    outBS.WriteCompressed( ( int ) transformations.size()  );
    for ( iter;iter!=end;iter++) {
        outBS.Write( ( float ) iter->tX );
        outBS.Write ( ( float ) iter->tY );
        outBS.Write ( ( float ) iter->scale );
		outBS.Write((float)iter->rZ);
    }
    peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,false );
}

void gfcNetworkClient::SendColorCorrections(std::vector<gfcNetPlateColorCorrectionInfo> corrections)
{
	RakNet::BitStream outBS;
	std::vector< gfcNetPlateColorCorrectionInfo >::iterator iter=corrections.begin(), end=corrections.end();
	outBS.Write ( ( unsigned char ) GFCNETID_COLORCORRECTIONMESSAGE );
	outBS.WriteCompressed( ( int ) corrections.size()  );
	for ( iter;iter!=end;iter++) {
		outBS.WriteCompressed((int)iter->quadID);
		StringCompressor::Instance()->EncodeString ( iter->lutName.c_str(),GFCNET_MAX_TEXT_LENGHT,&outBS );
		outBS.Write((float)iter->gamma);
		outBS.Write((float)iter->exposure);
		outBS.Write((float)iter->brightness);
		outBS.Write((float)iter->contrast);
		outBS.Write((float)iter->saturation);
	}
	peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,false );
}

void gfcNetworkClient::SendOtherStatesMessage ( gfcNetOtherStatesInfo info) {
    //printf ( "SendOtherSTatesMessage message:\n" );
    //printOtherStatesMessage(&message);
    RakNet::BitStream outBS;
    outBS.Write ( ( unsigned char ) GFCNETID_OTHERSTATESMESSAGE );


    //playback info stuff
    outBS.WriteCompressed ( ( int ) info.playbackInfo.from );
    outBS.WriteCompressed ( ( int ) info.playbackInfo.to );
    outBS.Write ( ( float ) info.playbackInfo.targetFPS );
    outBS.WriteCompressed ( ( int ) info.playbackInfo.playbackMode );
    outBS.WriteCompressed ( ( int ) info.playbackInfo.loopPriority );

	outBS.WriteCompressed ( ( int ) info.playbackInfo.inPoint );
	outBS.WriteCompressed ( ( int ) info.playbackInfo.outPoint );

    //plate stuff and layout
    outBS.WriteCompressed ( ( int ) info.layout );
    int plateStateInfoSize=info.plateStateInfo.size();
    outBS.WriteCompressed((int) plateStateInfoSize);
    std::vector<gfcNetPlateStateInfo>::iterator plateIter=info.plateStateInfo.begin(), plateEnd=info.plateStateInfo.end();
    for ( plateIter;plateIter!=plateEnd;plateIter++) {
        outBS.Write((unsigned char) plateIter->track);
        outBS.WriteCompressed((int) plateIter->quadID);
        outBS.Write((bool) plateIter->flip);
        outBS.Write((bool) plateIter->flop);
        outBS.Write((bool) plateIter->a);
        outBS.Write((bool) plateIter->r);
        outBS.Write((bool) plateIter->g);
        outBS.Write((bool) plateIter->b);
        // std::cout << "aspect sent: " << plateIter->aspect <<std::endl;
        StringCompressor::Instance()->EncodeString ( plateIter->aspect.c_str(),20,&outBS );
        outBS.Write((bool) plateIter->crop);
		
    }

    //track stuff
    int trackStateInfoSize=info.trackStateInfo.size();
    //printf("trackStateInfoSize sent: %i\n", trackStateInfoSize);
    outBS.WriteCompressed( (int) trackStateInfoSize);
    std::vector<gfcNetTrackStateInfo>::iterator trackIter=info.trackStateInfo.begin(), trackEnd=info.trackStateInfo.end();
    for ( trackIter;trackIter!=trackEnd;trackIter++) {
        outBS.WriteCompressed((int)  trackIter->frameOffset);
        outBS.WriteCompressed((int)  trackIter->holdMode);
        outBS.WriteCompressed((int)  trackIter->holdFrame);
    }

    peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,false );
}

void gfcNetworkClient::SendPlayPauseMessage(gfcNetPlayPauseInfo info) {

    RakNet::BitStream outBS;
    outBS.Write ( ( unsigned char ) GFCNETID_PLAYPAUSEMESSAGE );
    outBS.Write ( ( bool ) info.play );
    outBS.WriteCompressed ( ( int ) info.frame );
    outBS.WriteCompressed(info.direction);

    peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,false );

}

void gfcNetworkClient::SendFXAddMessage(gfcNetFXAddInfo info) {
    RakNet::BitStream outBS;
    outBS.Write ( ( unsigned char ) GFCNETID_FXADDMESSAGE );
    outBS.WriteCompressed ( ( int ) info.id.quadID );
    StringCompressor::Instance()->EncodeString ( info.id.hash.c_str(),GFCNET_MAX_TEXT_LENGHT,&outBS );
    peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,false);
}

void gfcNetworkClient::SendFXCommonMessage(gfcNetFXCommonInfo info) {
    RakNet::BitStream outBS;
    outBS.Write ( ( unsigned char ) GFCNETID_FXCOMMONMESSAGE );
    outBS.WriteCompressed ( ( int ) info.id.index );
    outBS.WriteCompressed ( ( int ) info.id.quadID );

    outBS.WriteCompressed ( ( int ) info.onOff );
    outBS.WriteCompressed ( ( int ) info.upDown );
    outBS.Write ( ( bool ) info.reset );
    outBS.Write ( ( bool ) info.remove );
    peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,false );
}

void gfcNetworkClient::SendFXAttribMessage(gfcNetFXAttribInfo info) {
    RakNet::BitStream outBS;
    outBS.Write ( ( unsigned char ) GFCNETID_FXATTRIBMESSAGE );
    outBS.WriteCompressed ( ( int ) info.id.index );
    outBS.WriteCompressed ( ( int ) info.id.quadID );

    outBS.Write ( ( unsigned char ) info.attribType );
    outBS.WriteCompressed ( ( int ) info.theInt );
    outBS.Write ( ( float ) info.theFloat );
    StringCompressor::Instance()->EncodeString ( info.lutOrCube.c_str(),GFCNET_MAX_TEXT_LENGHT,&outBS );
    StringCompressor::Instance()->EncodeString ( info.groupName.c_str(),GFCNET_MAX_TEXT_LENGHT,&outBS );
    StringCompressor::Instance()->EncodeString ( info.variableName.c_str(),GFCNET_MAX_TEXT_LENGHT,&outBS );

    peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,false );
}

void gfcNetworkClient::SendFXStackMessage(gfcNetFXStackMessage message)
{
	RakNet::BitStream outBS;
	outBS.Write ( ( unsigned char ) GFCNETID_FXSTACKMESSAGE );
	outBS.WriteCompressed ( ( int ) message.quadID );
	
	int stackLenght = message.theStack.length()+5;
	outBS.WriteCompressed ( ( int ) stackLenght );

	StringCompressor::Instance()->EncodeString ( message.theStack.c_str(),stackLenght,&outBS );
	
	peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,false );
}

void gfcNetworkClient::SendPlaylistMessage(gfcNetPlaylistMessage message){
	RakNet::BitStream outBS;
	outBS.Write ( ( unsigned char ) GFCNETID_SENDPLAYLIST );
	int plLenght = message.thePlaylist.length()+5; //send up to 5 more chars just in case we miss the null or linebreak in the end.
	outBS.WriteCompressed ( ( int ) plLenght );

	StringCompressor::Instance()->EncodeString ( message.thePlaylist.c_str(),plLenght,&outBS );

	peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,false );
}

void gfcNetworkClient::sendPlaylistEvent(gfcNetPlaylistEvent theEvent)
{
	RakNet::BitStream outBS;
	outBS.Write ( ( unsigned char ) GFCNETID_PLAYLISTEVENTOTHER);
	outBS.WriteCompressed(theEvent.selectedItem);
	
	peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,false );
}

void gfcNetworkClient::SendPlaylistItem(gfcPlaylistItem item)
{
	//make this take work as other string messages, sending the length first and the string after.
	std::string theString = item.asString();
	RakNet::BitStream outBS;
	outBS.Write ( ( unsigned char ) GFCNETID_PLAYLISTITEMLOADMESSAGE );
	StringCompressor::Instance()->EncodeString ( theString.c_str(),GFCNET_MAX_TEXT_LENGHT,&outBS );
	peer->Send( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,serverSystemAddress,false);
}


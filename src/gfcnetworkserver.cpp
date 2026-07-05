#include "gfcnetworkserver.h"
#include "StringCompressor.h"

#include <iostream>
#include <sstream> //for stingstream
#include <string>


#include "gfcnetworklog.h"
extern gfcNetworkLog networkLog;

#include "gfcfxmanager.h"
extern gfcFXManager fxManager;

#include "gfclutmanager.h"
extern gfcLUTManager lutManager;

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

#include "gfcplaylistmanager.h"
extern gfcPlaylistManager playlistManager;

#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;

namespace {
// Distinct, VFX-friendly palette in the packed-RGB format
// ((r&0xff)<<24)|((g&0xff)<<16)|((b&0xff)<<8).
inline int packRGB(int r, int g, int b) {
    return ((r & 0xff) << 24) | ((g & 0xff) << 16) | ((b & 0xff) << 8);
}
const int kColorPalette[] = {
    packRGB(0xE0, 0x83, 0x6C), // coral
    packRGB(0x5B, 0xB0, 0x7A), // green
    packRGB(0x6C, 0x9C, 0xE0), // blue
    packRGB(0xD4, 0xA0, 0x1E), // amber
    packRGB(0xB0, 0x7A, 0xD4), // violet
    packRGB(0x4C, 0xC0, 0xC0), // teal
    packRGB(0xE0, 0x6C, 0xB0), // pink
    packRGB(0xA0, 0xC0, 0x4C), // lime
    packRGB(0xE0, 0xB0, 0x6C), // sand
    packRGB(0x8C, 0x8C, 0xE0), // periwinkle
};
const int kColorPaletteSize = sizeof(kColorPalette) / sizeof(kColorPalette[0]);
// Default "no preference" sentinel: gray (128,128,128), matching gfcStructures.h.
const int kDefaultColor = packRGB(128, 128, 128);
}  // namespace

int gfcNetworkServer::assignColor(int preferred) {
    auto inUse = [this](int c) {
        for (const auto& kv : colorAddressMap)
            if (kv.second == c) return true;
        return false;
    };
    if (preferred != kDefaultColor && !inUse(preferred))
        return preferred;
    for (int i = 0; i < kColorPaletteSize; ++i)
        if (!inUse(kColorPalette[i]))
            return kColorPalette[i];
    return preferred;   // palette exhausted -> allow a duplicate
}

gfcNetworkServer::gfcNetworkServer() {
    peer = RakNetworkFactory::GetRakPeerInterface();
	middleOfSync=false;
}

std::vector<std::string> gfcNetworkServer::getParticipantNames() {
    std::vector<std::string> names;
    for (const auto& kv : nickNameAddressMap) names.push_back(kv.second);
    return names;
}


gfcNetworkServer::~gfcNetworkServer() {
}

void gfcNetworkServer::start(gfcServerParams * params) {


    networkLog.addToLog("Starting Server...");


    int thePort = 0;
    std::string thePassword = "";

    if (params) {
        thePort=params->port;
        thePassword=params->password;
        this->name=params->serverName;
    }

    this->port=thePort;
    this->password=thePassword;

    stop();

    SocketDescriptor socketDescriptor;
    socketDescriptor.port=thePort;

    peer->SetIncomingPassword ( thePassword.c_str(),thePassword.size() );
	
    peer->Startup ( ( unsigned short ) GFCNET_MAX_CLIENTS,15,&socketDescriptor,1 );

    peer->SetMaximumIncomingConnections ( GFCNET_MAX_CLIENTS );

    std::stringstream ss;
    ss << "Server Started with Max Incoming: "<<GFCNET_MAX_CLIENTS;
    networkLog.addToLog(ss.str());
}

void gfcNetworkServer::initializeWidgets() {
}

void gfcNetworkServer::stop() {
    peer->Shutdown ( 30 );
    nickNameAddressMap.clear();
    // GUI updates (IP, start/stop button, status) are managed by Qt
}

void gfcNetworkServer::Update() {
    Packet *p = peer->Receive();

    while ( p ) {
        std::stringstream ss (std::stringstream::in | std::stringstream::out);
        switch ( p->data[0] ) {
        case ID_CONNECTION_LOST: {

            //printf ( "Server: Client Connection Lost!\n" );
            //sprintf ( theString,"Server: Connection Lost, total connected %i",ConnectionCount() );
            //printf ( "Connections = %i\n", ConnectionCount() );
            //insertIntoNetLog ( theString );

            ss.str("");
            ss<<"Server: Client Connection Lost ("<<nickNameAddressMap[p->systemAddress]<<")";
            networkLog.addToLog(ss.str());
	    sendChatMessage(GFCNETMESSAGETYPE_SYSTEM,"SERVER MESSAGE",ss.str());
            nickNameAddressMap.erase ( nickNameAddressMap.find ( p->systemAddress ) );

            //send a message to all connected clients with the updated nickname list.
            RakNet::BitStream outBS;
            outBS.Write ( ( unsigned char ) GFCNETID_PEERSINSESSION );
            int howMany=ConnectionCount();
            printf ( "Sending %i nicknames\n",howMany );
            outBS.Write ( ( int ) howMany );
            std::map<SystemAddress,std::string>::iterator iter, iterEnd;
            iter=nickNameAddressMap.begin();
            iterEnd=nickNameAddressMap.end();
            int i=0;
            for ( iter; iter != iterEnd;iter++ ) {
                //printf ( "sending %s\n", ( ( *iter ).second ).c_str() );
                StringCompressor::Instance()->EncodeString ( ( ( *iter ).second ).c_str(),GFCNET_MAX_NICKNAME_LENGHT,&outBS );
                i++;
            }


            peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,UNASSIGNED_SYSTEM_ADDRESS,true );



        }
        break;

        case ID_DISCONNECTION_NOTIFICATION: {

            //sprintf ( theString,"-------%s-------\nServer: Client Disconnection, total connected %i\n----------------------------------------------\n",asciiTime().c_str(),ConnectionCount() );
            //printf ( "Connections = %i\n", ConnectionCount() );
            //rmw.log->insert ( theString );

            //Also send a Chat message indicating this peer is now off line

            ss.str("");
            ss<<"Server: Client disconnected ("<<nickNameAddressMap[p->systemAddress]<<")";
            networkLog.addToLog(ss.str());

	    std::string broadcastText;
            broadcastText+=nickNameAddressMap[p->systemAddress].c_str();
            broadcastText+=" has left the session";

            sendChatMessage(GFCNETMESSAGETYPE_SYSTEM,"SERVER MESSAGE",broadcastText);

            printf ( "Removing Nickname: %s\n",nickNameAddressMap[p->systemAddress].c_str() );
            //std::string nickToRemove=nickNameAddressMap[p->systemAddress];
            //std::map<SystemAddress,std::string>::iterator theIter=nickNameAddressMap.find(nickToRemove);
            nickNameAddressMap.erase ( nickNameAddressMap.find ( p->systemAddress ) );



            //send a message to all connected clients with the updated nickname list.
            {
                std::map<SystemAddress,std::string>::iterator iter, iterEnd;
                iter=nickNameAddressMap.begin();
                iterEnd=nickNameAddressMap.end();
                int i=0;
                RakNet::BitStream outBS;
                outBS.Write ( ( unsigned char ) GFCNETID_PEERSINSESSION );
                int howMany=ConnectionCount();
                printf ( "Sending %i nicknames\n",howMany );
                outBS.Write ( ( int ) howMany );

                for ( iter; iter != iterEnd;iter++ ) {
                    printf ( "sending %s\n", ( ( *iter ).second ).c_str() );
                    StringCompressor::Instance()->EncodeString ( ( ( *iter ).second ).c_str(),GFCNET_MAX_NICKNAME_LENGHT,&outBS );
                    i++;
                }


                peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,UNASSIGNED_SYSTEM_ADDRESS,true );
            }


        }
        break;
//
        case GFCNETID_LOADEDFXSHASHES: {

            printf ( "Got a LoadedFXHashes\n" );

            //SystemAddress sa=p->systemAddress;
            ss.str("");
            ss<<"Server: Checking FX hashes from client "<<nickNameAddressMap[p->systemAddress].c_str();
            networkLog.addToLog(ss.str());
            //printf ( "Server: Checking hashes from client %s\n",nickNameAddressMap[p->systemAddress].c_str() );

            printf ( "Creating bitstream\n" );
            RakNet::BitStream myBitStream ( p->data, p->length, true );
            char theText[40];
            int theInt;
            unsigned char typeID;

            //myBitStream.Read(typeID);
            myBitStream.IgnoreBits ( 8 );

            //how many hashes?

            int numHashes=0;
            //myBitStream.PrintBits();
            myBitStream.Read ( numHashes );
            printf ( "Got %i hashes\n", numHashes );
            std::set<std::string> clientsHashes;
            std::set<std::string> serversMissingFXSet;

            std::map<std::string, int> fxHashMap=fxManager.getHashMap();
            printf("fxManager.getHashmap returned a hash map with %i members\n",fxHashMap.size());
            //find which FXs the Server is missing.
            for ( int i=0; i< ( int ) numHashes; i++ ) {

                StringCompressor::Instance()->DecodeString ( theText,40,&myBitStream );
                clientsHashes.insert ( theText );

                if ( fxHashMap.find ( theText ) !=fxHashMap.end() ) {
					printf("Hash %i found in server\n",i);

                } else {

                    serversMissingFXSet.insert ( theText );

                }


            }

            //find which FXs the client is missing.
            //iterate through all of the servers FXs, and find the ones the client is missing.
            std::vector<std::string> fxhashes=fxManager.getHashes();
            {
                std::vector<std::string>::iterator iter=fxhashes.begin(),end=fxhashes.end();

                for ( iter;iter!=end ;iter++ ) {

                    if ( clientsHashes.find ( *iter ) !=clientsHashes.end() ) {
                        //printf ( "Client has FX: %s (%s)\n",iter->name,iter->md5Hash.c_str() );
                    } else {
                        ss.str("");
                        ss<<"Client missing FX "<<*iter;
                        networkLog.addToLog(ss.str());
                        clientsMissingFXsMap[p->systemAddress].insert ( *iter );
                    }


                }

            }

            //send each missing FX from server to the client (request the ones we are missing)
            {
                ss.str("");
                ss << "Server: Missing and requesting "<<serversMissingFXSet.size()<<" FXs";
                networkLog.addToLog(ss.str());
                RakNet::BitStream requestBS;
                requestBS.Write ( ( unsigned char ) GFCNETID_REQUESTFXS );
                //how many?
                requestBS.Write ( ( int ) serversMissingFXSet.size() );
                //write the hashes of the missing FXs
                std::set<std::string>::iterator iter=serversMissingFXSet.begin(), end=serversMissingFXSet.end();
                for ( iter;iter!=end ;iter++ ) {
                    StringCompressor::Instance()->EncodeString ( iter->c_str(),40,&requestBS );
                }
                peer->Send ( &requestBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,p->systemAddress,0 );
            }

        }
        break;

        case GFCNETID_LOADEDLUTSHASHES: {

            printf ( "Got a LoadedLUTHashes\n" );

            //SystemAddress sa=p->systemAddress;
            ss.str("");
            ss<<"Server: Checking LUT hashes from client "<<nickNameAddressMap[p->systemAddress].c_str();
            networkLog.addToLog(ss.str());
            //printf ( "Server: Checking hashes from client %s\n",nickNameAddressMap[p->systemAddress].c_str() );

            printf ( "Creating bitstream\n" );
            RakNet::BitStream myBitStream ( p->data, p->length, true );
            char theText[40];
            int theInt;
            unsigned char typeID;

            //myBitStream.Read(typeID);
            myBitStream.IgnoreBits ( 8 );

            //how many hashes?

            int numHashes=0;
            //myBitStream.PrintBits();
            myBitStream.Read ( numHashes );
            printf ( "Got %i hashes\n", numHashes );
            std::set<std::string> clientsHashes;
            std::set<std::string> serversMissingLUTSet;

            std::map<std::string, int> lutHashMap=lutManager.getHashMap();
            printf("lutManager.getHashmap returned a hash map with %i members\n",lutHashMap.size());
            //find which LUTs the Server is missing.
            for ( int i=0; i< ( int ) numHashes; i++ ) {

                StringCompressor::Instance()->DecodeString ( theText,40,&myBitStream );
                clientsHashes.insert ( theText );

                if ( lutHashMap.find ( theText ) !=lutHashMap.end() ) {

                } else {

                    serversMissingLUTSet.insert ( theText );

                }


            }

            //find which LUTs the client is missing.
            //iterate through all of the servers LUTs, and find the ones the client is missing.
            std::vector<std::string> luthashes=lutManager.getHashes();
            {
                std::vector<std::string>::iterator iter=luthashes.begin(),end=luthashes.end();

                for ( iter;iter!=end ;iter++ ) {

                    if ( clientsHashes.find ( *iter ) !=clientsHashes.end() ) {
                        //printf ( "Client has LUT: %s (%s)\n",iter->name,iter->md5Hash.c_str() );
                    } else {
                        ss.str("");
                        ss<<"Client missing LUT "<<*iter;
                        networkLog.addToLog(ss.str());
                        clientsMissingLUTsMap[p->systemAddress].insert ( *iter );
                    }


                }

            }

            //send each missing LUT from server to the client (request the ones we are missing)
            {
                ss.str("");
                ss << "Server: Missing and requesting "<<serversMissingLUTSet.size()<<" LUTs";
                networkLog.addToLog(ss.str());
                RakNet::BitStream requestBS;
                requestBS.Write ( ( unsigned char ) GFCNETID_REQUESTLUTS );
                //how many?
                requestBS.Write ( ( int ) serversMissingLUTSet.size() );
                //write the hashes of the missing LUTs
                std::set<std::string>::iterator iter=serversMissingLUTSet.begin(), end=serversMissingLUTSet.end();
                for ( iter;iter!=end ;iter++ ) {
                    StringCompressor::Instance()->EncodeString ( iter->c_str(),40,&requestBS );
                }
                peer->Send ( &requestBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,p->systemAddress,0 );
            }

        }
        break;
//
//
//
        case GFCNETID_REQUESTEDFXS: {
            printf ( "Server: Got a GFCNETID_REQUESTEDFXS\n" );

            RakNet::BitStream bs ( p->data,p->length,0 );

            int howMany;
            bs.IgnoreBits ( 8 );

            bs.Read ( howMany );

            printf ( " parsing and loading %i FXs\n",howMany );

            for ( int i=0;i<howMany;i++ ) {

                gfcFX theFX;
                unserializeFX ( &bs ); //TODO: Unserialize should not take an FX, it should use the fxManager to load the unserialized FX
            }

            //serialize the FXs the client is missing and send them; The FXs this client is missing is stored in the clientsMissingFXsMap[p->systemAddress] set

            //iterate through all the clientsMissingFXsMap[p->systemAddress] strings.
            RakNet::BitStream outBS;
            outBS.Write ( ( unsigned char ) GFCNETID_MISSINGFXS ); //send back the clients missing FXs
            			
			std::vector<gfcFX> fxToSend;
			std::set<std::string>::iterator iter=clientsMissingFXsMap[p->systemAddress].begin(), end=clientsMissingFXsMap[p->systemAddress].end();

			for ( iter; iter!=end; iter++ ) {
				gfcFX tmpFX=fxManager.getFXbyHash(*iter);
				//check that this is not the empty FX, it could happen when synchronizing fxs.
				if (tmpFX.name!="")
				{
					fxToSend.push_back(tmpFX);
				}								
			}
			
			outBS.Write ( ( int ) fxToSend.size() ); //how many fxs is the client missing;
			for (int fxIter=0;fxIter<fxToSend.size();fxIter++)
			{
				 serializeFX ( &(fxToSend[fxIter]),&outBS );
			}
			
			/*std::set<std::string>::iterator iter=clientsMissingFXsMap[p->systemAddress].begin(), end=clientsMissingFXsMap[p->systemAddress].end();
            for ( iter;iter!=end ;iter++ ) {
                gfcFX tmpFX=fxManager.getFXbyHash(*iter);

                serializeFX ( &tmpFX,&outBS );
            }*/

            clientsMissingFXsMap[p->systemAddress].clear();

            peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,p->systemAddress,0 );

            //if we received at least one FX, then start the FXsic process again with everybody else.
            if ( howMany )
                startFXSinc ( p->systemAddress,false );
			else
			{
				networkLog.addToLog("Server: FX Sinc Complete");
				RakNet::BitStream outBS2;
				outBS2.Write ( ( unsigned char ) GFCNETID_FXSINCCOMPLETE );
				peer->Send ( &outBS2,HIGH_PRIORITY,RELIABLE_ORDERED,0,p->systemAddress,0 );

				//here we should start the LUT sinc
				startLUTSinc ( p->systemAddress,false );
			}

        }
        break;

        case GFCNETID_REQUESTEDLUTS: {
            printf ( "Server: Got a GFCNETID_REQUESTEDLUTS\n" );

            RakNet::BitStream bs ( p->data,p->length,0 );

            int howMany;
            bs.IgnoreBits ( 8 );

            bs.Read ( howMany );

            printf ( " parsing and loading %i LUTs\n",howMany );

            for ( int i=0;i<howMany;i++ ) {

                unserializeLUT ( &bs );

            }

            //serialize the LUTs the client is missing and send them; The LUTs this client is missing is stored in the clientsMissingLUTsMap[p->systemAddress] set

            //iterate through all the clientsMissingLUTsMap[p->systemAddress] strings.
            RakNet::BitStream outBS;
            outBS.Write ( ( unsigned char ) GFCNETID_MISSINGLUTS ); //send back the clients missing LUTs
            outBS.Write ( ( int ) clientsMissingLUTsMap[p->systemAddress].size() ); //how many lutss is the client missing;
            std::set<std::string>::iterator iter=clientsMissingLUTsMap[p->systemAddress].begin(), end=clientsMissingLUTsMap[p->systemAddress].end();

            for ( iter;iter!=end ;iter++ ) {
                CubeLUT tmpLUT=lutManager.getLUTbyHash(*iter);
                serializeLUT ( &tmpLUT, &outBS );
            }

            clientsMissingLUTsMap[p->systemAddress].clear();

            peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,p->systemAddress,0 );

            //if we received at least one LUT, then start the LUTsinc process again with everybody else.
            if ( howMany )
                startLUTSinc ( p->systemAddress,true );
			else
			{
				RakNet::BitStream outBS2;
				outBS2.Write ( ( unsigned char ) GFCNETID_LUTSSINCCOMPLETE );
				peer->Send ( &outBS2,HIGH_PRIORITY,RELIABLE_ORDERED,0,p->systemAddress,false );

				networkLog.addToLog("Server: LUT Sinc Complete");
				
				//here we should start the Stack sinc
				startStackSinc(p->systemAddress,false );				
			}

        }
        break;
//
		case GFCNETID_RECEIVEDFXSTACKS:{
			

			networkLog.addToLog("Server: Stack Sinc Complete");
			
			RakNet::BitStream outBS2;
			outBS2.Write ( ( unsigned char ) GFCNETID_SENDSTACKSINCFINISHED );
			peer->Send ( &outBS2,HIGH_PRIORITY,RELIABLE_ORDERED,0,p->systemAddress,false );

			//now we start the playlist merge, send just to this guy.
			startPlaylistMerge(p->systemAddress,false);
		}
		break;

		case GFCNETID_SENDPLAYLISTFORMERGE:
		{
			//read the playlist and merge it with ours, 
			RakNet::BitStream bs ( p->data,p->length,0 );
			
			bs.IgnoreBits ( 8 );

			int howLong;
			bs.ReadCompressed( howLong);
			
			howLong+=5;
			char *tmpPl=new char[howLong];
			StringCompressor::Instance()->DecodeString ( tmpPl,howLong,&bs );
			std::string thePl=tmpPl;
			delete [] tmpPl;
		
			std::string ourPl=playlistManager.getPlaylistAsString();
			
			//if the new guy's playlist is different, merge it and send the new merged playlist to everyone, 
			if (ourPl!=thePl)
			{
				networkManager.setTakeNotifications(false);
				playlistManager.mergePlaylist(thePl);
				networkManager.setTakeNotifications(true);

				RakNet::BitStream outBS;
				outBS.Write ( ( unsigned char ) GFCNETID_MERGEDPLAYLISTS );

				std::string newPl=playlistManager.getPlaylistAsString();
				int newPlLength=newPl.length();
				outBS.WriteCompressed(newPlLength);
				StringCompressor::Instance()->EncodeString ( newPl.c_str(),newPlLength,&outBS );

				//send the new playlist to everybody.
				peer->Send( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,UNASSIGNED_SYSTEM_ADDRESS,true);
			}

			//this client is ready...
			this->clientsReadyMap[p->systemAddress]=1;

			//...check if others are too.
			int allClientsReady=1;

			for ( std::map<SystemAddress,int>::iterator iter=clientsReadyMap.begin();iter != clientsReadyMap.end();iter++ ){

				if ( ( *iter ).second!= 1 ) {
					allClientsReady=0;
				}
			}

			if (allClientsReady)
			{

				//broadcast playlistMergeFinished, and broadcast all-ready
				{
					RakNet::BitStream outBS;
					outBS.Write ( ( unsigned char ) GFCNETID_PLAYLISTMERGEFINISHED );
					//broadcast! and then wait for the client's response
					peer->Send( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,UNASSIGNED_SYSTEM_ADDRESS,true);
				}
				{
					RakNet::BitStream outBS;
					outBS.Write ( ( unsigned char ) GFCNETID_SENDALLREADY );
					//broadcast! and then wait for the client's response
					peer->Send( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,UNASSIGNED_SYSTEM_ADDRESS,true);

					//END OF SYNC!
				}
			}

		}
		break;

        case ID_NEW_INCOMING_CONNECTION: {
            char theString[3200];
            sprintf ( theString,"-------%s-------\nServer: New Connection, total connected %i\n----------------------------------------------\n",asciiTime(false).c_str(),ConnectionCount() );
            printf ( "Connections = %i\n", ConnectionCount() );
            //rmw.log->insert ( theString );
        }
        break;

        case GFCNETID_NICKNAMESEND: {
            //store the nickname in the nickNameAddressMap
            char receivedNickname[GFCNET_MAX_NICKNAME_LENGHT];
            RakNet::BitStream bs ( p->data,p->length,true );
            RakNet::BitStream outBS;
            bool nickNameAlreadyTaken=false;
            bs.IgnoreBits ( 8 );
            StringCompressor::Instance()->DecodeString ( receivedNickname,GFCNET_MAX_NICKNAME_LENGHT,&bs );
			int theColor;
			bs.ReadCompressed(theColor); //read our pointer color
            //CHECK TO PREVENT NICKNAME DUPLICATES

			for ( std::map<SystemAddress,std::string>::iterator iter=nickNameAddressMap.begin();iter != nickNameAddressMap.end();iter++ )
			{

                if ( ( *iter ).second== receivedNickname ) {
                    //Somene with this nickname already logged in, send an empty message with GFCNETID_NICKALREADYINUSE header and disconnect

                    /*gfcNetNickAlreadyTakenMessage message;
                    message.mytypeID=GFCNETID_NICKALREADYINUSE;
                    peer->Send ( ( const char* ) &message,sizeof ( gfcNetNickAlreadyTakenMessage ),HIGH_PRIORITY,RELIABLE_ORDERED,0,p->systemAddress,false );
                    peer->CloseConnection ( p->systemAddress,true );*/

                    outBS.Write ( GFCNETID_NICKALREADYINUSE );
                    peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,p->systemAddress,false );
                    peer->CloseConnection ( p->systemAddress,true );
                    nickNameAlreadyTaken=true;
                    break;
                }
			}

            if ( nickNameAlreadyTaken )
                break;

            nickNameAddressMap[p->systemAddress]= receivedNickname;
			colorAddressMap[p->systemAddress]=assignColor(theColor);

            printf ( "Nickname added: %s\nColorAdded%i\n",nickNameAddressMap[p->systemAddress].c_str(),theColor);
			
            //send a message to all connected clients with the updated nickname list.
            {
                std::map<SystemAddress,std::string>::iterator iter, iterEnd;
                iter=nickNameAddressMap.begin();
                iterEnd=nickNameAddressMap.end();
                int i=0;
                outBS.Write ( ( unsigned char ) GFCNETID_PEERSINSESSION );
                int howMany=ConnectionCount();
                printf ( "Sending %i nicknames\n",howMany );
                outBS.Write ( ( int ) howMany );

                for ( iter; iter != iterEnd;iter++ ) {
                    printf ( "sending %s\n", ( ( *iter ).second ).c_str() );
                    StringCompressor::Instance()->EncodeString ( ( ( *iter ).second ).c_str(),GFCNET_MAX_NICKNAME_LENGHT,&outBS );
                    i++;
                }
                peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,UNASSIGNED_SYSTEM_ADDRESS,true );
            }

            //Also send a Chat message indicating this peer is now on line
            std::string broadcastText;
            broadcastText+=nickNameAddressMap[p->systemAddress].c_str();

            broadcastText+=" has joined session";

            sendChatMessage(GFCNETMESSAGETYPE_SYSTEM,"SERVER MESSAGE",broadcastText);

			this->clientsReadyMap[p->systemAddress]=0;
			
			//broadcast that a new player has joined and that we are only just starting the sync (except to the new guy (p->systemAddress))
			{
				RakNet::BitStream outBSNewPlayer;
				outBSNewPlayer.Write(( unsigned char ) GFCNETID_NEWPEERINSESSION);
				peer->Send ( &outBSNewPlayer,HIGH_PRIORITY,RELIABLE_ORDERED,0,p->systemAddress,true );
			}

            startFXSinc ( p->systemAddress,false );
            //startLUTSinc ( p->systemAddress,false );


        }
        break;

        case GFCNETID_CHATMESSAGE: {
            //Decode the message
            RakNet::BitStream bs ( p->data,p->length,false );
            bs.IgnoreBits ( 8 );
            unsigned char messageType;
            bs.Read(messageType);
            char tempChatMessage[GFCNET_MAX_TEXT_LENGHT];
            StringCompressor::Instance()->DecodeString ( tempChatMessage,GFCNET_MAX_TEXT_LENGHT,&bs );

//             switch ( messageType ) {
//             case GFCNETMESSAGETYPE_NORMAL:
//                 printf("Server got normal message\n");
//                 break;
// 
//             case GFCNETMESSAGETYPE_SYSTEM:
//                 printf("Server got system message\n");
//                 break;
// 
// 
//             case GFCNETMESSAGETYPE_LOAD:
//                 printf("Server got load message\n");
//                 break;
//             }

            sendChatMessage(messageType,nickNameAddressMap[p->systemAddress],tempChatMessage);

            break;
        }

					
					case GFCNETID_POINTERINFOMESSAGE:
					{

						//server appends nickname and sends to all, except original sender

						//printf("Server got a GFCNETID_POINTERINFOMESSAGE\n");
						RakNet::BitStream bs ( p->data,p->length,true );
						bs.IgnoreBits ( 8 );
						RakNet::BitStream outBS;

						int theInt;
						float theFloat;



						outBS.Write ( ( unsigned char ) GFCNETID_POINTERINFOBROADCASTMESSAGE );
						bs.ReadCompressed ( theInt );
						outBS.WriteCompressed ( ( int ) theInt ); //quadID
						bs.ReadCompressed ( theInt );
						outBS.WriteCompressed ( ( int ) theInt ); //x
						bs.ReadCompressed ( theInt );
						outBS.WriteCompressed ( ( int ) theInt ); //y
						bs.Read ( theFloat );
						outBS.Write ( ( float ) theFloat ); //scale

						// The client's pointer message ends at scale — it does not
						// encode a nickname (SendPointerInfoMessage writes only
						// quad/x/y/scale). Don't decode one here; the broadcast
						// nickname comes from nickNameAddressMap below.
						StringCompressor::Instance()->EncodeString ( nickNameAddressMap[p->systemAddress].c_str(),GFCNET_MAX_TEXT_LENGHT,&outBS );

						//also send the color
						
						int theColor=colorAddressMap[p->systemAddress];
						//printf("Bcasting color %i\n",theColor);
						outBS.WriteCompressed (theColor); //color
						
						
						//peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,p->systemAddress,true ); //dont send to sender
						peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,UNASSIGNED_SYSTEM_ADDRESS,true ); //send to sender

					}
					break;
//					
					case GFCNETID_SENDREMOTEPOINTERCOLOR:
					{
						RakNet::BitStream bs ( p->data,p->length,true );
						bs.IgnoreBits ( 8 );
						
						int theInt;
						
						bs.ReadCompressed ( theInt );

						printf("%s changed color to %i\n",nickNameAddressMap[p->systemAddress].c_str(),theInt);
						colorAddressMap[p->systemAddress]=assignColor(theInt);

					}
					break;

					///these cases simply forward the bitstream to all except original sender
					case GFCNETID_COLORCORRECTIONMESSAGE:
					case GFCNETID_OTHERSTATESMESSAGE:
					case GFCNETID_PLAYPAUSEMESSAGE:
					case GFCNETID_TRANSFORMATIONMESSAGE:
					case GFCNETID_FXADDMESSAGE:
					case GFCNETID_FXCOMMONMESSAGE:
					case GFCNETID_FXATTRIBMESSAGE:
					case GFCNETID_PLAYLISTITEMLOADMESSAGE:
					case GFCNETID_FXSTACKMESSAGE:
					case GFCNETID_SENDPLAYLIST:
					case GFCNETID_PLAYLISTEVENTOTHER:
					{
						//server sends to all, except original sender
						//printf("Server forwarding!\n");
						RakNet::BitStream bs ( p->data,p->length,false );
						peer->Send ( &bs,HIGH_PRIORITY,RELIABLE_ORDERED,0,p->systemAddress,true );
					}
					break;
										
						
        }

        peer->DeallocatePacket ( p );
        p = peer->Receive();
    }

}

std::string gfcNetworkServer::getPassowrd() {
    return this->password;
}

int gfcNetworkServer::getPort() {
    return this->port;
}

std::string gfcNetworkServer::getName() {
    return this->name;
}

unsigned int gfcNetworkServer::ConnectionCount() {
    return peer->NumberOfConnections();
}

int gfcNetworkServer::getConnectionCount() {
    return ConnectionCount();
}

void gfcNetworkServer::disableGUI() {
    // GUI enable/disable is managed by Qt — no-op
}

void gfcNetworkServer::enableGUI() {
    // GUI enable/disable is managed by Qt — no-op
}

void gfcNetworkServer::startFXSinc(SystemAddress sysaddress, bool broadcast) {
    networkLog.addToLog("Server: Starting FXSinc");
    RakNet::BitStream outBS;
    outBS.Write ( ( unsigned char ) GFCNETID_REQUESTFXHASHES );
	if (broadcast)
	{
		peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,UNASSIGNED_SYSTEM_ADDRESS,broadcast );
	}
	else
	{
		peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,sysaddress,broadcast );
	}
	
}

/**
 * Starts a broadcast FXSinc
 */
void gfcNetworkServer::startFXSinc() {
    startFXSinc(peer->GetSystemAddressFromIndex(0),true);
}

void gfcNetworkServer::startLUTSinc(SystemAddress sysaddress, bool broadcast) {
    networkLog.addToLog("Server: Starting LUTSinc");
    RakNet::BitStream outBS;
    outBS.Write ( ( unsigned char ) GFCNETID_REQUESTLUTSHASHES );
    peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,sysaddress,broadcast );
}

/**
 * Starts a broadcast LUTSinc
 */
void gfcNetworkServer::startLUTSinc() {
    startLUTSinc(peer->GetSystemAddressFromIndex(0),true);
}


void gfcNetworkServer::startStackSinc(SystemAddress sysaddress, bool broadcast) {
	networkLog.addToLog("Server: Starting Stack Sinc");
	RakNet::BitStream outBS;
	outBS.Write ( ( unsigned char ) GFCNETID_SENDFXTACKS);
	
	//write the serialized stacks here
	std::string stackString=plateManager.getPlateFXStacksAsString();
	
	int stackLen=stackString.length();
	outBS.WriteCompressed(stackLen);
	StringCompressor::Instance()->EncodeString( stackString.c_str(),stackLen,&outBS );	

	peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,sysaddress,broadcast );
}

/**
* Starts a broadcast startStackinc
*/
void gfcNetworkServer::startStackSinc() {
	startStackSinc(peer->GetSystemAddressFromIndex(0),true);
}

void gfcNetworkServer::startPlaylistMerge(SystemAddress sysaddress, bool broadcast) {
	networkLog.addToLog("Server: Starting Playlist Merge");
	
	//we need to clear the playerReady map, as of now, nobody is ready.
	this->clientsReadyMap.erase(clientsReadyMap.begin(),clientsReadyMap.end());
	
	RakNet::BitStream outBS;
	outBS.Write ( ( unsigned char ) GFCNETID_REQUESTPLAYLIST);
	
	peer->Send ( &outBS,HIGH_PRIORITY,RELIABLE_ORDERED,0,sysaddress,broadcast );
}

/**
* Starts a broadcast PlaylistMerge
*/
void gfcNetworkServer::startPlaylistMerge() {
	startPlaylistMerge(peer->GetSystemAddressFromIndex(0),true);
}

void gfcNetworkServer::sendChatMessage(unsigned char type, std::string sender, std::string message) {
    //server sends messageID, messageType, asciiTime, sender and message
    //The message is reconstruted on the clients side and used as deemes apropiately by the client, depending on the messageType
    RakNet::BitStream outBS2;
    outBS2.Write ( ( unsigned char ) GFCNETID_CHATBROADCASTMESSAGE ); //messageID for chat broadcasts
    outBS2.Write((unsigned char) type);  //messageType
    StringCompressor::Instance()->EncodeString ( asciiTime(true).c_str(),GFCNET_MAX_TEXT_LENGHT,&outBS2 ); //time
    StringCompressor::Instance()->EncodeString ( sender.c_str(),GFCNET_MAX_TEXT_LENGHT,&outBS2 ); //sender nickname (can be servers own notification messages)
    StringCompressor::Instance()->EncodeString ( message.c_str(),GFCNET_MAX_TEXT_LENGHT,&outBS2 ); //message
    peer->Send ( &outBS2,HIGH_PRIORITY,RELIABLE_ORDERED,0,UNASSIGNED_SYSTEM_ADDRESS,true ); //send!
}

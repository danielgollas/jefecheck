#include "gfcnetworkserver.h"
#include "gfcTransportFactory.h"
#include "gfcWire.h"
#include "gfcWireMessages.h"

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
    transport_ = jefe::net::makeTransport(); // kind from JEFECHECK_TRANSPORT (JEF-24)
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
        this->coordinatorMode=params->coordinatorMode;
        this->coordinatorUrl=params->coordinatorUrl;
    }

    this->port=thePort;
    this->password=thePassword;

    stop();

    // JEF-27: (re)build the transport from the server params. Default
    // (coordinatorMode=false) reproduces the JEF-24 LAN-host path exactly;
    // coordinator mode selects the WebRTC transport, which creates a cloud
    // session (the coordinator assigns the code, read via
    // getAssignedSessionCode()). Rebuilt here (not in the ctor) because the
    // config is only known once params arrive.
    {
        jefe::net::TransportConfig tcfg;
        tcfg.coordinatorMode=this->coordinatorMode;
        tcfg.coordinatorUrl=this->coordinatorUrl;
        tcfg.password=thePassword;
        transport_ = jefe::net::makeTransport(tcfg);
    }

    transport_->startHost ( ( unsigned short ) thePort, thePassword, GFCNET_MAX_CLIENTS );

    std::stringstream ss;
    ss << "Server Started with Max Incoming: "<<GFCNET_MAX_CLIENTS;
    networkLog.addToLog(ss.str());
}

void gfcNetworkServer::initializeWidgets() {
}

void gfcNetworkServer::stop() {
    transport_->stopHost();
    nickNameAddressMap.clear();
    // GUI updates (IP, start/stop button, status) are managed by Qt
}

void gfcNetworkServer::Update() {
    jefe::net::TransportEvent ev;

    while ( transport_->poll ( ev ) ) {
        std::stringstream ss (std::stringstream::in | std::stringstream::out);
        switch ( ev.type ) {
        case jefe::net::TransportEventType::PeerLost: {

            //printf ( "Server: Client Connection Lost!\n" );
            //sprintf ( theString,"Server: Connection Lost, total connected %i",ConnectionCount() );
            //printf ( "Connections = %i\n", ConnectionCount() );
            //insertIntoNetLog ( theString );

            ss.str("");
            ss<<"Server: Client Connection Lost ("<<nickNameAddressMap[ev.peer]<<")";
            networkLog.addToLog(ss.str());
	    sendChatMessage(GFCNETMESSAGETYPE_SYSTEM,"SERVER MESSAGE",ss.str());
            nickNameAddressMap.erase ( nickNameAddressMap.find ( ev.peer ) );

            //send a message to all connected clients with the updated nickname list.
            jefe::wire::Writer outW;
            jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_PEERSINSESSION );
            int howMany=ConnectionCount();
            printf ( "Sending %i nicknames\n",howMany );
            outW.writeU32 ( ( uint32_t ) howMany );
            std::map<jefe::net::PeerId,std::string>::iterator iter, iterEnd;
            iter=nickNameAddressMap.begin();
            iterEnd=nickNameAddressMap.end();
            int i=0;
            for ( iter; iter != iterEnd;iter++ ) {
                //printf ( "sending %s\n", ( ( *iter ).second ).c_str() );
                outW.writeString ( ( *iter ).second );
                i++;
            }


            if ( outW.ok() ) transport_->send ( outW.data(), ( int ) outW.size(), jefe::net::kInvalidPeerId, true );



        }
        break;

        case jefe::net::TransportEventType::PeerDisconnected: {

            //sprintf ( theString,"-------%s-------\nServer: Client Disconnection, total connected %i\n----------------------------------------------\n",asciiTime().c_str(),ConnectionCount() );
            //printf ( "Connections = %i\n", ConnectionCount() );
            //rmw.log->insert ( theString );

            //Also send a Chat message indicating this peer is now off line

            ss.str("");
            ss<<"Server: Client disconnected ("<<nickNameAddressMap[ev.peer]<<")";
            networkLog.addToLog(ss.str());

	    std::string broadcastText;
            broadcastText+=nickNameAddressMap[ev.peer].c_str();
            broadcastText+=" has left the session";

            sendChatMessage(GFCNETMESSAGETYPE_SYSTEM,"SERVER MESSAGE",broadcastText);

            printf ( "Removing Nickname: %s\n",nickNameAddressMap[ev.peer].c_str() );
            //std::string nickToRemove=nickNameAddressMap[ev.peer];
            //std::map<jefe::net::PeerId,std::string>::iterator theIter=nickNameAddressMap.find(nickToRemove);
            nickNameAddressMap.erase ( nickNameAddressMap.find ( ev.peer ) );



            //send a message to all connected clients with the updated nickname list.
            {
                std::map<jefe::net::PeerId,std::string>::iterator iter, iterEnd;
                iter=nickNameAddressMap.begin();
                iterEnd=nickNameAddressMap.end();
                int i=0;
                jefe::wire::Writer outW;
                jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_PEERSINSESSION );
                int howMany=ConnectionCount();
                printf ( "Sending %i nicknames\n",howMany );
                outW.writeU32 ( ( uint32_t ) howMany );

                for ( iter; iter != iterEnd;iter++ ) {
                    printf ( "sending %s\n", ( ( *iter ).second ).c_str() );
                    outW.writeString ( ( *iter ).second );
                    i++;
                }


                if ( outW.ok() ) transport_->send ( outW.data(), ( int ) outW.size(), jefe::net::kInvalidPeerId, true );
            }


        }
        break;

        case jefe::net::TransportEventType::PeerConnected: {
            char theString[3200];
            sprintf ( theString,"-------%s-------\nServer: New Connection, total connected %i\n----------------------------------------------\n",asciiTime(false).c_str(),ConnectionCount() );
            printf ( "Connections = %i\n", ConnectionCount() );
            //rmw.log->insert ( theString );
        }
        break;

        case jefe::net::TransportEventType::Data: {
        // JEF-23: ev.bytes is a jefe::wire frame (the transport already
        // stripped the RakNet envelope byte). Parse the frame header; a
        // truncated buffer or version mismatch is a silent drop.
        jefe::wire::Reader r ( ev.bytes.data(), ev.bytes.size() );
        uint16_t msgType = 0;
        if ( !jefe::wire::readFrameHeader ( r, msgType ) ) break;
        switch ( msgType ) {
//
        case GFCNETID_LOADEDFXSHASHES: {

            printf ( "Got a LoadedFXHashes\n" );

            ss.str("");
            ss<<"Server: Checking FX hashes from client "<<nickNameAddressMap[ev.peer].c_str();
            networkLog.addToLog(ss.str());
            //printf ( "Server: Checking hashes from client %s\n",nickNameAddressMap[ev.peer].c_str() );

            //how many hashes?

            uint32_t numHashes=0;
            if ( !r.readU32 ( numHashes ) ) break; // malformed: skip silently
            printf ( "Got %i hashes\n", (int)numHashes );
            std::set<std::string> clientsHashes;
            std::set<std::string> serversMissingFXSet;

            std::map<std::string, int> fxHashMap=fxManager.getHashMap();
            printf("fxManager.getHashmap returned a hash map with %i members\n",(int)fxHashMap.size());
            //find which FXs the Server is missing.
            bool readOk=true;
            for ( uint32_t i=0; i<numHashes; i++ ) {

                std::string theText;
                if ( !r.readString ( theText ) ) { readOk=false; break; }
                clientsHashes.insert ( theText );

                if ( fxHashMap.find ( theText ) !=fxHashMap.end() ) {
					printf("Hash %i found in server\n",(int)i);

                } else {

                    serversMissingFXSet.insert ( theText );

                }


            }
            if ( !readOk ) break; // truncated hash list: no set updates, no request sent

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
                        clientsMissingFXsMap[ev.peer].insert ( *iter );
                    }


                }

            }

            //send each missing FX from server to the client (request the ones we are missing)
            {
                ss.str("");
                ss << "Server: Missing and requesting "<<serversMissingFXSet.size()<<" FXs";
                networkLog.addToLog(ss.str());
                jefe::wire::Writer requestW;
                jefe::wire::beginFrame ( requestW, ( uint16_t ) GFCNETID_REQUESTFXS );
                //how many?
                requestW.writeU32 ( ( uint32_t ) serversMissingFXSet.size() );
                //write the hashes of the missing FXs
                std::set<std::string>::iterator iter=serversMissingFXSet.begin(), end=serversMissingFXSet.end();
                for ( iter;iter!=end ;iter++ ) {
                    requestW.writeString ( *iter );
                }
                if ( requestW.ok() ) transport_->send ( requestW.data(), ( int ) requestW.size(), ev.peer, false );
            }

        }
        break;

        case GFCNETID_LOADEDLUTSHASHES: {

            printf ( "Got a LoadedLUTHashes\n" );

            ss.str("");
            ss<<"Server: Checking LUT hashes from client "<<nickNameAddressMap[ev.peer].c_str();
            networkLog.addToLog(ss.str());
            //printf ( "Server: Checking hashes from client %s\n",nickNameAddressMap[ev.peer].c_str() );

            //how many hashes?

            uint32_t numHashes=0;
            if ( !r.readU32 ( numHashes ) ) break; // malformed: skip silently
            printf ( "Got %i hashes\n", (int)numHashes );
            std::set<std::string> clientsHashes;
            std::set<std::string> serversMissingLUTSet;

            std::map<std::string, int> lutHashMap=lutManager.getHashMap();
            printf("lutManager.getHashmap returned a hash map with %i members\n",(int)lutHashMap.size());
            //find which LUTs the Server is missing.
            bool readOk=true;
            for ( uint32_t i=0; i<numHashes; i++ ) {

                std::string theText;
                if ( !r.readString ( theText ) ) { readOk=false; break; }
                clientsHashes.insert ( theText );

                if ( lutHashMap.find ( theText ) !=lutHashMap.end() ) {

                } else {

                    serversMissingLUTSet.insert ( theText );

                }


            }
            if ( !readOk ) break; // truncated hash list: no set updates, no request sent

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
                        clientsMissingLUTsMap[ev.peer].insert ( *iter );
                    }


                }

            }

            //send each missing LUT from server to the client (request the ones we are missing)
            {
                ss.str("");
                ss << "Server: Missing and requesting "<<serversMissingLUTSet.size()<<" LUTs";
                networkLog.addToLog(ss.str());
                jefe::wire::Writer requestW;
                jefe::wire::beginFrame ( requestW, ( uint16_t ) GFCNETID_REQUESTLUTS );
                //how many?
                requestW.writeU32 ( ( uint32_t ) serversMissingLUTSet.size() );
                //write the hashes of the missing LUTs
                std::set<std::string>::iterator iter=serversMissingLUTSet.begin(), end=serversMissingLUTSet.end();
                for ( iter;iter!=end ;iter++ ) {
                    requestW.writeString ( *iter );
                }
                if ( requestW.ok() ) transport_->send ( requestW.data(), ( int ) requestW.size(), ev.peer, false );
            }

        }
        break;
//
//
//
        case GFCNETID_REQUESTEDFXS: {
            printf ( "Server: Got a GFCNETID_REQUESTEDFXS\n" );

            uint32_t howMany=0;
            if ( !r.readU32 ( howMany ) ) break; // malformed: skip silently

            printf ( " parsing and loading %i FXs\n",(int)howMany );

            for ( uint32_t i=0;i<howMany;i++ ) {

                // Legacy semantics: the count is the REQUESTED count; the
                // sender only serialized the FXs it actually had loaded, so
                // running out of payload mid-loop is a NORMAL condition —
                // stop reading and continue the sync flow below.
                if ( !unserializeFX ( r ) ) break; //TODO: Unserialize should not take an FX, it should use the fxManager to load the unserialized FX
            }

            //serialize the FXs the client is missing and send them; The FXs this client is missing is stored in the clientsMissingFXsMap[ev.peer] set

            //iterate through all the clientsMissingFXsMap[ev.peer] strings.
            jefe::wire::Writer outW;
            jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_MISSINGFXS ); //send back the clients missing FXs

			std::vector<gfcFX> fxToSend;
			std::set<std::string>::iterator iter=clientsMissingFXsMap[ev.peer].begin(), end=clientsMissingFXsMap[ev.peer].end();

			for ( iter; iter!=end; iter++ ) {
				gfcFX tmpFX=fxManager.getFXbyHash(*iter);
				//check that this is not the empty FX, it could happen when synchronizing fxs.
				if (tmpFX.name!="")
				{
					fxToSend.push_back(tmpFX);
				}
			}

			outW.writeU32 ( ( uint32_t ) fxToSend.size() ); //how many fxs is the client missing;
			for (int fxIter=0;fxIter<fxToSend.size();fxIter++)
			{
				 serializeFX ( &(fxToSend[fxIter]),outW );
			}

            clientsMissingFXsMap[ev.peer].clear();

            if ( outW.ok() ) transport_->send ( outW.data(), ( int ) outW.size(), ev.peer, false );

            //if we received at least one FX, then start the FXsic process again with everybody else.
            if ( howMany )
                startFXSinc ( ev.peer,false );
			else
			{
				networkLog.addToLog("Server: FX Sinc Complete");
				jefe::wire::Writer outW2;
				jefe::wire::beginFrame ( outW2, ( uint16_t ) GFCNETID_FXSINCCOMPLETE );
				if ( outW2.ok() ) transport_->send ( outW2.data(), ( int ) outW2.size(), ev.peer, false );

				//here we should start the LUT sinc
				startLUTSinc ( ev.peer,false );
			}

        }
        break;

        case GFCNETID_REQUESTEDLUTS: {
            printf ( "Server: Got a GFCNETID_REQUESTEDLUTS\n" );

            uint32_t howMany=0;
            if ( !r.readU32 ( howMany ) ) break; // malformed: skip silently

            printf ( " parsing and loading %i LUTs\n",(int)howMany );

            for ( uint32_t i=0;i<howMany;i++ ) {

                // Same requested-count semantics as REQUESTEDFXS above:
                // payload may legitimately run out early — stop and continue.
                if ( !unserializeLUT ( r ) ) break;

            }

            //serialize the LUTs the client is missing and send them; The LUTs this client is missing is stored in the clientsMissingLUTsMap[ev.peer] set

            //iterate through all the clientsMissingLUTsMap[ev.peer] strings.
            jefe::wire::Writer outW;
            jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_MISSINGLUTS ); //send back the clients missing LUTs
            outW.writeU32 ( ( uint32_t ) clientsMissingLUTsMap[ev.peer].size() ); //how many lutss is the client missing;
            std::set<std::string>::iterator iter=clientsMissingLUTsMap[ev.peer].begin(), end=clientsMissingLUTsMap[ev.peer].end();

            for ( iter;iter!=end ;iter++ ) {
                CubeLUT tmpLUT=lutManager.getLUTbyHash(*iter);
                serializeLUT ( &tmpLUT, outW );
            }

            clientsMissingLUTsMap[ev.peer].clear();

            if ( outW.ok() ) transport_->send ( outW.data(), ( int ) outW.size(), ev.peer, false );

            //if we received at least one LUT, then start the LUTsinc process again with everybody else.
            if ( howMany )
                startLUTSinc ( ev.peer,true );
			else
			{
				jefe::wire::Writer outW2;
				jefe::wire::beginFrame ( outW2, ( uint16_t ) GFCNETID_LUTSSINCCOMPLETE );
				if ( outW2.ok() ) transport_->send ( outW2.data(), ( int ) outW2.size(), ev.peer, false );

				networkLog.addToLog("Server: LUT Sinc Complete");

				//here we should start the Stack sinc
				startStackSinc(ev.peer,false );
			}

        }
        break;
//
		case GFCNETID_RECEIVEDFXSTACKS:{


			networkLog.addToLog("Server: Stack Sinc Complete");

			jefe::wire::Writer outW2;
			jefe::wire::beginFrame ( outW2, ( uint16_t ) GFCNETID_SENDSTACKSINCFINISHED );
			if ( outW2.ok() ) transport_->send ( outW2.data(), ( int ) outW2.size(), ev.peer, false );

			//now we start the playlist merge, send just to this guy.
			startPlaylistMerge(ev.peer,false);
		}
		break;

		case GFCNETID_SENDPLAYLISTFORMERGE:
		{
			//read the playlist and merge it with ours,
			std::string thePl;
			if ( !r.readString ( thePl ) ) break; // malformed: skip silently (client stays not-ready)

			std::string ourPl=playlistManager.getPlaylistAsString();

			//if the new guy's playlist is different, merge it and send the new merged playlist to everyone,
			if (ourPl!=thePl)
			{
				networkManager.setTakeNotifications(false);
				playlistManager.mergePlaylist(thePl);
				networkManager.setTakeNotifications(true);

				jefe::wire::Writer outW;
				jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_MERGEDPLAYLISTS );

				std::string newPl=playlistManager.getPlaylistAsString();
				outW.writeString ( newPl ); //legacy explicit length field dropped: writeString carries the length

				//send the new playlist to everybody.
				if ( outW.ok() ) transport_->send( outW.data(), ( int ) outW.size(), jefe::net::kInvalidPeerId, true);
			}

			//this client is ready...
			this->clientsReadyMap[ev.peer]=1;

			//...check if others are too.
			int allClientsReady=1;

			for ( std::map<jefe::net::PeerId,int>::iterator iter=clientsReadyMap.begin();iter != clientsReadyMap.end();iter++ ){

				if ( ( *iter ).second!= 1 ) {
					allClientsReady=0;
				}
			}

			if (allClientsReady)
			{

				//broadcast playlistMergeFinished, and broadcast all-ready
				{
					jefe::wire::Writer outW;
					jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_PLAYLISTMERGEFINISHED );
					//broadcast! and then wait for the client's response
					if ( outW.ok() ) transport_->send( outW.data(), ( int ) outW.size(), jefe::net::kInvalidPeerId, true);
				}
				{
					jefe::wire::Writer outW;
					jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_SENDALLREADY );
					//broadcast! and then wait for the client's response
					if ( outW.ok() ) transport_->send( outW.data(), ( int ) outW.size(), jefe::net::kInvalidPeerId, true);

					//END OF SYNC!
				}
			}

		}
		break;

        case GFCNETID_NICKNAMESEND: {
            //store the nickname in the nickNameAddressMap
            std::string receivedNickname;
            int theColor=0;
            bool nickNameAlreadyTaken=false;
            if ( !r.readString ( receivedNickname ) ) break; // malformed: skip silently (peer stays unregistered)
			if ( !r.readI32 ( theColor ) ) break; //read our pointer color
            //CHECK TO PREVENT NICKNAME DUPLICATES

			for ( std::map<jefe::net::PeerId,std::string>::iterator iter=nickNameAddressMap.begin();iter != nickNameAddressMap.end();iter++ )
			{

                if ( ( *iter ).second== receivedNickname ) {
                    //Somene with this nickname already logged in, send an empty message with GFCNETID_NICKALREADYINUSE header and disconnect

                    jefe::wire::Writer outW;
                    jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_NICKALREADYINUSE );
                    if ( outW.ok() ) transport_->send ( outW.data(), ( int ) outW.size(), ev.peer, false );
                    transport_->closePeer ( ev.peer,true );
                    nickNameAlreadyTaken=true;
                    break;
                }
			}

            if ( nickNameAlreadyTaken )
                break;

            nickNameAddressMap[ev.peer]= receivedNickname;
			colorAddressMap[ev.peer]=assignColor(theColor);

            printf ( "Nickname added: %s\nColorAdded%i\n",nickNameAddressMap[ev.peer].c_str(),theColor);

            //send a message to all connected clients with the updated nickname list.
            {
                std::map<jefe::net::PeerId,std::string>::iterator iter, iterEnd;
                iter=nickNameAddressMap.begin();
                iterEnd=nickNameAddressMap.end();
                int i=0;
                jefe::wire::Writer outW;
                jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_PEERSINSESSION );
                int howMany=ConnectionCount();
                printf ( "Sending %i nicknames\n",howMany );
                outW.writeU32 ( ( uint32_t ) howMany );

                for ( iter; iter != iterEnd;iter++ ) {
                    printf ( "sending %s\n", ( ( *iter ).second ).c_str() );
                    outW.writeString ( ( *iter ).second );
                    i++;
                }
                if ( outW.ok() ) transport_->send ( outW.data(), ( int ) outW.size(), jefe::net::kInvalidPeerId, true );
            }

            //Also send a Chat message indicating this peer is now on line
            std::string broadcastText;
            broadcastText+=nickNameAddressMap[ev.peer].c_str();

            broadcastText+=" has joined session";

            sendChatMessage(GFCNETMESSAGETYPE_SYSTEM,"SERVER MESSAGE",broadcastText);

			this->clientsReadyMap[ev.peer]=0;

			//broadcast that a new player has joined and that we are only just starting the sync (except to the new guy (ev.peer))
			{
				jefe::wire::Writer outWNewPlayer;
				jefe::wire::beginFrame ( outWNewPlayer, ( uint16_t ) GFCNETID_NEWPEERINSESSION );
				if ( outWNewPlayer.ok() ) transport_->send ( outWNewPlayer.data(), ( int ) outWNewPlayer.size(), ev.peer, true );
			}

            startFXSinc ( ev.peer,false );
            //startLUTSinc ( ev.peer,false );


        }
        break;

        case GFCNETID_CHATMESSAGE: {
            //Decode the message
            unsigned char messageType;
            if ( !r.readU8 ( messageType ) ) break; // malformed: skip silently
            std::string tempChatMessage;
            if ( !r.readString ( tempChatMessage ) ) break; // malformed: skip silently

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

            sendChatMessage(messageType,nickNameAddressMap[ev.peer],tempChatMessage,colorAddressMap[ev.peer]);

            break;
        }


					case GFCNETID_POINTERINFOMESSAGE:
					{

						//server appends nickname and sends to all, except original sender

						//printf("Server got a GFCNETID_POINTERINFOMESSAGE\n");

						// The client's pointer message ends at scale — it does not
						// encode a nickname (SendPointerInfoMessage writes only
						// quad/x/y/scale). Don't decode one here; the broadcast
						// nickname comes from nickNameAddressMap below.
						gfcNetPointerInfo ptrInfo;
						ptrInfo.color=0; //not on this wire message; broadcast color comes from colorAddressMap
						if ( !jefe::wire::decodePointerInfo ( r, ptrInfo ) ) break; // malformed: skip silently

						gfcNetRemotePointerInfo remoteInfo;
						remoteInfo.quadID=ptrInfo.quadID;
						remoteInfo.x=ptrInfo.x;
						remoteInfo.y=ptrInfo.y;
						remoteInfo.scale=ptrInfo.scale;
						remoteInfo.name=nickNameAddressMap[ev.peer];
						remoteInfo.color=colorAddressMap[ev.peer];
						remoteInfo.fadeCounter=0;      //receiver-local, never on the wire
						remoteInfo.headFadeCounter=0;  //receiver-local, never on the wire

						jefe::wire::Writer outW;
						jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_POINTERINFOBROADCASTMESSAGE );
						jefe::wire::encodeRemotePointerInfo ( outW, remoteInfo );

						//transport_->send ( outW.data(), ( int ) outW.size(), ev.peer, true ); //dont send to sender
						if ( outW.ok() ) transport_->send ( outW.data(), ( int ) outW.size(), jefe::net::kInvalidPeerId, true ); //send to sender

					}
					break;
//
					case GFCNETID_SENDREMOTEPOINTERCOLOR:
					{
						int theInt;

						if ( !r.readI32 ( theInt ) ) break; // malformed: skip silently

						printf("%s changed color to %i\n",nickNameAddressMap[ev.peer].c_str(),theInt);
						colorAddressMap[ev.peer]=assignColor(theInt);

					}
					break;

					///these cases simply forward the frame to all except original sender
					case GFCNETID_COLORCORRECTIONMESSAGE:
					case GFCNETID_OTHERSTATESMESSAGE:
					case GFCNETID_PLAYPAUSEMESSAGE:
					case GFCNETID_TRANSFORMATIONMESSAGE:
					case GFCNETID_FXADDMESSAGE:
					case GFCNETID_FXCOMMONMESSAGE:
					case GFCNETID_FXATTRIBMESSAGE:
					case GFCNETID_PLAYLISTITEMLOADMESSAGE:
					case GFCNETID_FXSTACKMESSAGE:
					case GFCNETID_LAYERCHANGEMESSAGE:
					case GFCNETID_SENDPLAYLIST:
					case GFCNETID_PLAYLISTEVENTOTHER:
					{
						//server sends to all, except original sender
						//printf("Server forwarding!\n");
						// ev.bytes is already a complete jefe::wire frame; forward
						// it UNCHANGED (the transport re-prepends the RakNet
						// envelope byte on send).
						transport_->send ( ev.bytes.data(), ( int ) ev.bytes.size(), ev.peer, true );
					}
					break;


        }
        }
        break;

        default:
        break;

        }

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
    return transport_->connectionCount();
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

void gfcNetworkServer::startFXSinc(jefe::net::PeerId peerId, bool broadcast) {
    networkLog.addToLog("Server: Starting FXSinc");
    jefe::wire::Writer outW;
    jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_REQUESTFXHASHES );
	if (broadcast)
	{
		if ( outW.ok() ) transport_->send ( outW.data(), ( int ) outW.size(), jefe::net::kInvalidPeerId, broadcast );
	}
	else
	{
		if ( outW.ok() ) transport_->send ( outW.data(), ( int ) outW.size(), peerId, broadcast );
	}

}

/**
 * Starts a broadcast FXSinc
 */
void gfcNetworkServer::startFXSinc() {
    // Legacy seeded from the RakNet peer's index-0 address accessor; no call
    // sites exist (verified), so seed from the first known peer for equivalence.
    jefe::net::PeerId a0 = nickNameAddressMap.empty() ? jefe::net::kInvalidPeerId
                                                      : nickNameAddressMap.begin()->first;
    startFXSinc(a0,true);
}

void gfcNetworkServer::startLUTSinc(jefe::net::PeerId peerId, bool broadcast) {
    networkLog.addToLog("Server: Starting LUTSinc");
    jefe::wire::Writer outW;
    jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_REQUESTLUTSHASHES );
    if ( outW.ok() ) transport_->send ( outW.data(), ( int ) outW.size(), peerId, broadcast );
}

/**
 * Starts a broadcast LUTSinc
 */
void gfcNetworkServer::startLUTSinc() {
    jefe::net::PeerId a0 = nickNameAddressMap.empty() ? jefe::net::kInvalidPeerId
                                                      : nickNameAddressMap.begin()->first;
    startLUTSinc(a0,true);
}


void gfcNetworkServer::startStackSinc(jefe::net::PeerId peerId, bool broadcast) {
	networkLog.addToLog("Server: Starting Stack Sinc");
	jefe::wire::Writer outW;
	jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_SENDFXTACKS );

	//write the serialized stacks here
	std::string stackString=plateManager.getPlateFXStacksAsString();

	outW.writeString ( stackString ); //legacy explicit length field dropped: writeString carries the length

	if ( outW.ok() ) transport_->send ( outW.data(), ( int ) outW.size(), peerId, broadcast );
}

/**
* Starts a broadcast startStackinc
*/
void gfcNetworkServer::startStackSinc() {
	jefe::net::PeerId a0 = nickNameAddressMap.empty() ? jefe::net::kInvalidPeerId
	                                                  : nickNameAddressMap.begin()->first;
	startStackSinc(a0,true);
}

void gfcNetworkServer::startPlaylistMerge(jefe::net::PeerId peerId, bool broadcast) {
	networkLog.addToLog("Server: Starting Playlist Merge");

	//we need to clear the playerReady map, as of now, nobody is ready.
	this->clientsReadyMap.erase(clientsReadyMap.begin(),clientsReadyMap.end());

	jefe::wire::Writer outW;
	jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_REQUESTPLAYLIST );

	if ( outW.ok() ) transport_->send ( outW.data(), ( int ) outW.size(), peerId, broadcast );
}

/**
* Starts a broadcast PlaylistMerge
*/
void gfcNetworkServer::startPlaylistMerge() {
	jefe::net::PeerId a0 = nickNameAddressMap.empty() ? jefe::net::kInvalidPeerId
	                                                  : nickNameAddressMap.begin()->first;
	startPlaylistMerge(a0,true);
}

void gfcNetworkServer::sendChatMessage(unsigned char type, std::string sender, std::string message, int color) {
    //server sends messageID, messageType, asciiTime, sender and message
    //The message is reconstruted on the clients side and used as deemes apropiately by the client, depending on the messageType
    jefe::wire::Writer outW;
    jefe::wire::beginFrame ( outW, ( uint16_t ) GFCNETID_CHATBROADCASTMESSAGE ); //messageID for chat broadcasts
    outW.writeU8 ( ( unsigned char ) type );  //messageType
    outW.writeString ( asciiTime(true) ); //time
    outW.writeString ( sender ); //sender nickname (can be servers own notification messages)
    outW.writeString ( message ); //message
    outW.writeI32 ( ( int ) color ); //sender's assigned color (0 for system msgs)
    if ( outW.ok() ) transport_->send ( outW.data(), ( int ) outW.size(), jefe::net::kInvalidPeerId, true ); //send!
}

#ifndef GFCNETWORKSERVER_H
#define GFCNETWORKSERVER_H

//#include "network.h"
#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <stdlib.h> // For atoi
#include <cstring> // For strlen
#include <memory>
#include "gfcnetworklog.h"
#include "gfcNetworkStructures.h"
#include "gfcTransport.h"

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/

#define GFCNET_MAX_CLIENTS 50
#define GFCNET_VERSION 1

class gfcNetworkServer{
public:
    gfcNetworkServer();

    ~gfcNetworkServer();

    void initializeWidgets();
    void start(gfcServerParams * params=0);
    void stop();
    unsigned int connectionCount();
    void startFXSinc(jefe::net::PeerId peerId, bool broadcast=false);
    void startLUTSinc ( jefe::net::PeerId peerId, bool broadcast=false );
	void startStackSinc ( jefe::net::PeerId peerId, bool broadcast=false );
	void startPlaylistMerge ( jefe::net::PeerId peerId, bool broadcast=false );
    void startFXSinc();
    void startLUTSinc ();
	void startStackSinc();
	void startPlaylistMerge();
    void Update();

    std::string getName();
    void setName(std::string pname);
    int getPort();
    std::string getPassowrd();
    void setPassword(std::string ppassword);
    int getConnectionCount();
    std::vector<std::string> getParticipantNames();

    // JEF-30: per-peer connection health from the underlying transport (WebRTC
    // real stats / RakNet basic presence). Empty when not hosting.
    std::vector<jefe::net::PeerStats> peerStats() {
        return transport_ ? transport_->peerStats() : std::vector<jefe::net::PeerStats>();
    }
    // Resolve a peer's registered nickname (empty if unknown).
    std::string nicknameForPeer(jefe::net::PeerId peer) {
        auto it = nickNameAddressMap.find(peer);
        return it == nickNameAddressMap.end() ? std::string() : it->second;
    }

    // JEF-27 cloud-coordinator hosting accessors.
    bool getCoordinatorMode() const { return coordinatorMode; }
    std::string getCoordinatorUrl() const { return coordinatorUrl; }
    // The session code the coordinator assigned this host (empty until the
    // create-session round-trip completes, or when not in coordinator mode).
    std::string getAssignedSessionCode() {
        return transport_ ? transport_->assignedSessionCode() : std::string();
    }

    void sendChatMessage(unsigned char type, std::string sender, std::string message, int color = 0);

    void disableGUI();
    void enableGUI();

    private:
    int port;
    std::string name;
    std::unique_ptr<jefe::net::ITransport> transport_;
    std::string password;
    bool coordinatorMode = false;
    std::string coordinatorUrl;
    unsigned int ConnectionCount();
    gfcNetworkLog* log;

	bool middleOfSync;

    std::map<jefe::net::PeerId,std::string> nickNameAddressMap;
	std::map<jefe::net::PeerId,int> colorAddressMap; //color map for pointers

	// Picks a color for a joining/recoloring participant: the preferred color
	// when it's non-default and not already in use, otherwise the first unused
	// color from a fixed distinct palette, otherwise the preferred color
	// (duplicate allowed when the palette is exhausted).
	int assignColor(int preferred);

    std::map<jefe::net::PeerId, std::set<std::string> > clientsMissingFXsMap; /*when we sinc fxs, we analize what fxs we are missing and what fxs the client is missing,
    first we request the ones we are missing, and once we get them (even if they are none) we get send the ones the client is missing.
    Therefore we need to store the ones that the client is missing for a later time, this is what this map of sets of md5 hash strings holds, a set for each connected client*/
    std::map<jefe::net::PeerId, std::set<std::string> > clientsMissingLUTsMap; //the same but for luts.
	std::map<jefe::net::PeerId, int > clientsReadyMap; //keeps a record of what clients are ready after all the syncs that go on when a new client connects. When all are ready then we can start the session;
};

#endif

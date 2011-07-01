#ifndef GFCNETWORKSERVER_H
#define GFCNETWORKSERVER_H

//#include "network.h"
#include <stdio.h>
#include "mainWindow.h"
#include "RakPeerInterface.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <stdlib.h> // For atoi
#include <cstring> // For strlen
#include "Rand.h"
#include "RakNetStatistics.h"
#include "MessageIdentifiers.h"
#include <stdio.h>
#include "GetTime.h"
#include "RakAssert.h"
#include "RakSleep.h"
#include "BitStream.h"
#include "demoversion.h"

#include "gfcnetworklog.h"
#include "gfcnetworkservergui.h"

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/

#ifdef DEMO_VERSION
#define GFCNET_MAX_CLIENTS 2
#define GFCNET_VERSION 0
#else
#define GFCNET_MAX_CLIENTS 50
#define GFCNET_VERSION 1
#endif

class gfcNetworkServer{
public:
    gfcNetworkServer();

    ~gfcNetworkServer();
    
    void initializeWidgets();
    void start(gfcServerParams * params=0);
    void stop();
    unsigned int connectionCount();
    void startFXSinc(SystemAddress sysaddress, bool broadcast=false);
    void startLUTSinc ( SystemAddress sysaddress, bool broadcast=false );
	void startStackSinc ( SystemAddress sysaddress, bool broadcast=false );
	void startPlaylistMerge ( SystemAddress sysaddress, bool broadcast=false );
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
    gfcNetworkServerGUI* myGUI;
    
    void sendChatMessage(unsigned char type, std::string sender, std::string message);
    
    void disableGUI();
    void enableGUI();
    
    private:
    int port;
    std::string name;
    RakPeerInterface *peer;
    std::string password;
    unsigned int ConnectionCount();
    gfcNetworkLog* log;
    
	bool middleOfSync;

    std::map<SystemAddress,std::string> nickNameAddressMap;
	std::map<SystemAddress,int> colorAddressMap; //color map for pointers
    std::map<SystemAddress, std::set<std::string> > clientsMissingFXsMap; /*when we sinc fxs, we analize what fxs we are missing and what fxs the client is missing, 
    first we request the ones we are missing, and once we get them (even if they are none) we get send the ones the client is missing. 
    Therefore we need to store the ones that the client is missing for a later time, this is what this map of sets of md5 hash strings holds, a set for each connected client*/
    std::map<SystemAddress, std::set<std::string> > clientsMissingLUTsMap; //the same but for luts.
	std::map<SystemAddress, std::string > clientsSentPlaylistMap; //keeps a record of what clients already sent their playlist
	std::map<SystemAddress, int > clientsReadyMap; //keeps a record of what clients are ready after all the syncs that go on when a new client connects. When all are ready then we can start the session;
};

#endif

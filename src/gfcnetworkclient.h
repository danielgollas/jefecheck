#ifndef GFCNETWORKCLIENT_H
#define GFCNETWORKCLIENT_H

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
#include "RakNetworkFactory.h"
#include "MessageIdentifiers.h"
#include <stdio.h>
#include "GetTime.h"
#include "RakAssert.h"
#include "RakSleep.h"
#include "BitStream.h"

#include "gfcNetworkStructures.h"
#include "gfcpointerstorage.h"

#include "gfcnetworkclientgui.h"

#include "gfcplaylistitem.h"

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcNetworkClient {
public:
    gfcNetworkClient();

    ~gfcNetworkClient();
    
    void initializeWidgets();
    
    void disableGUI();
    void enableGUI();
    
    void Startup();
    bool Connect(gfcConnectionParams *params=0);
    void Disconnect();

    void Update();
	

	void saveCurrentToRecentIPs();
	void setRecent(std::vector<std::string> recents);
	void setAddress(std::string pip, std::string pport);

    void SendLoadedFXsHashes ( void );
    void SendLoadedLUTsHashes ( void );
    void SendChatMessage ( std::string message, unsigned char type=GFCNETMESSAGETYPE_NORMAL);
    void SendPointerInfoMessage(gfcNetPointerInfo info);
    void SendTransformations(std::vector< gfcNetTransformationInfo > transformations);
	void SendColorCorrections(std::vector<gfcNetPlateColorCorrectionInfo> corrections);
	void SendOtherStatesMessage ( gfcNetOtherStatesInfo message );
    void SendPlayPauseMessage(gfcNetPlayPauseInfo info);
    
	void SendRemotePointerColor(int color);

    void SendFXAddMessage ( gfcNetFXAddInfo info );
    void SendFXCommonMessage ( gfcNetFXCommonInfo info);
    void SendFXAttribMessage ( gfcNetFXAttribInfo info );
    void SendPlaylistItem(gfcPlaylistItem item);
    void SendFXStackMessage(gfcNetFXStackMessage message);
	void SendPlaylistMessage(gfcNetPlaylistMessage message);
	void sendPlaylistEvent(gfcNetPlaylistEvent theEvent);

	bool GetGotMessages();
    bool statusChange;
    bool getIsConnected();
    bool getAttemptingConnection();
    void setIsServerClient(bool value);
    bool getIsServerClient();
    bool getGotNewChatMessage();
    SystemAddress getServerSystemAddress();
    std::vector<gfcChatLogEntry> getChatLog();
    gfcPointerStorage pointers;
    
private:
    
    bool gotMessages;
    gfcNetworkClientGUI* myGUI;
    bool isConnected;
    bool attemptingConnection;
    RakPeerInterface *peer;
    RakNetTime nextSendTime;
    RakNetTime flipConnectionTime;
    int port;
    std::string serverIP;
    std::string password;
    std::string status;
    int statusColor;
    
	bool haveSentMyPlaylist;

    std::map<std::string,gfcNetRemotePointerInfo> nickNamePointerMap;
    
    bool gotNewChatMessage;
    
    bool isServerClient; //tells if this client is the internal client on a server system.
    
    std::string nickName;
    SystemAddress serverSystemAddress;

    std::vector<std::string> peersInSession;
    std::vector<gfcChatLogEntry> chatLog;

};

#endif

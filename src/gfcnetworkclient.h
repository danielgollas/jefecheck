#ifndef GFCNETWORKCLIENT_H
#define GFCNETWORKCLIENT_H

#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <stdlib.h> // For atoi
#include <cstring> // For strlen
#include <memory>

#include "gfcTransport.h"

#include "gfcNetworkStructures.h"
#include "gfcpointerstorage.h"

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
    void SendLayerChangeMessage(int quadID, std::string layerName);
	void SendPlaylistMessage(gfcNetPlaylistMessage message);
	void sendPlaylistEvent(gfcNetPlaylistEvent theEvent);

	bool GetGotMessages();
    bool statusChange;
    bool getIsConnected();
    bool getAttemptingConnection();
    void setIsServerClient(bool value);
    bool getIsServerClient();
    bool getGotNewChatMessage();
    jefe::net::PeerId getServerPeerId();
    std::vector<gfcChatLogEntry> getChatLog();
    std::string getStatus();
    int getStatusColor();
    std::vector<std::string> getPeersInSession();
    std::string getNickName() { return nickName; }
    gfcPointerStorage pointers;

private:
    void setStatusInternal(std::string s, int color);

    bool gotMessages;
    bool isConnected;
    bool attemptingConnection;
    std::unique_ptr<jefe::net::ITransport> transport_;
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
    jefe::net::PeerId serverPeerId_ = jefe::net::kInvalidPeerId;

    std::vector<std::string> peersInSession;
    std::vector<gfcChatLogEntry> chatLog;

};

#endif

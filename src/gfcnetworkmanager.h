#ifndef GFCNETWORKMANAGER_H
#define GFCNETWORKMANAGER_H

#include "gfcNetworkStructures.h"
#include "gfcnetworkclient.h"
#include "gfcnetworkserver.h"
#include "gfcnetworkeventnotification.h"

#include <stdio.h>
#include "gfcNetworkStructures.h"


#include <map>
#include <string>
#include <vector>

enum networkEventTypes{GFCNETEVENT_TRANSFORMS=0, GFCNETEVENT_FX, GFCNETEVENT_OTHER, GFCNETEVENT_COLOR, GFCNETEVENT_NUMOFEVENTTYPES};

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcNetworkManager{
public:
    gfcNetworkManager();

    ~gfcNetworkManager();

    void initializeWidgets();
    
    void update();
    
    void startServer(gfcServerParams *params=0);
    void stopServer();
    void startConnection(gfcConnectionParams *params=0);
    void stopConnection();
    
    std::vector<std::string> participantNames();
    std::string connectionStatusText();

    // JEF-30: per-peer connection health, forwarded to whichever transport is
    // active (server side when hosting, client side when joined). Empty when
    // solo. `peerNickname` resolves a peer's display name (server role only;
    // empty otherwise).
    std::vector<jefe::net::PeerStats> peerStats();
    std::string peerNickname(jefe::net::PeerId peer);

    // JEF-27: the coordinator-assigned cloud session code (host role, coordinator
    // mode only; empty otherwise). Surfaced for the Remote dialog + --coord-test.
    std::string getAssignedSessionCode();

    // JEF-37: joiners waiting for admission, and the host's decision on one.
    // Host role only -- a joiner is never told who else is in the lobby, so
    // these return empty / no-op for everyone else.
    std::vector<jefe::net::PendingJoiner> pendingJoiners();
    void decideJoiner(const std::string& joinerId, bool admit);
    std::vector<std::string> chatLogLines();
    std::vector<std::string> drainErrors();

    void sendChatMessage();
    
    void sendPlayPauseMessage(gfcNetPlayPauseInfo info);
    
    void sendFXAddMessage ( gfcNetFXAddInfo info );
    //void sendSetFXStackMessage ( gfcNetLoadFXStackInfo info );
    void sendFXCommonMessage ( gfcNetFXCommonInfo info);
    void sendFXAttribMessage(gfcNetFXAttribInfo info);
    
	void sendFXStackMessage(gfcNetFXStackMessage message);

    // Broadcasts an EXR layer/channel change on a plate's track so remote
    // peers re-decode the same layer. No-op when solo. Called from the Qt
    // layer-combo path (bridge).
    void sendLayerChange(int quadID, std::string layerName);

    // Queues a live FX-attrib edit to be sent coalesced at the GFCNETEVENT_FX
    // throttle rate (~60Hz), keyed per widget so a slider drag collapses to
    // one send per interval and the trailing value always ships (no desync).
    // Mirrors how COLOR/TRANSFORMS are rate-limited. Called from local edits
    // only (the receive path applies directly, so no echo).
    void queueFXAttrib(const gfcNetFXAttribInfo& info);
        
    void sendSystemChatMessage(std::string message, int type);
    
    void sendPointerInfoMessage(gfcNetPointerInfo message);
    
    bool getConnected();
    bool getIsServer();
    bool consumeGotMessages(); // true if the client processed any inbound packet since last call (drives a viewport repaint)
    bool overlayAnimating();   // true while the chat/status overlay is fading or in chat entry (needs continuous repaint)
    std::vector<std::string> networkLogLines(); // connection-log snapshot for the panel

    struct ChatEntryData { std::string sender, message, timeHHMM; int type; bool isSelf; int color; };
    std::vector<ChatEntryData> chatEntries();
    
    void startFXSinc(); //used by other managers to notify the network manager that a new LUT or FX was loaded.
    void startLUTSinc(); 
    void startStackSinc();
	void startPlaylistMerge();

    void saveChatLog(std::string filename);
    void setRecent(std::vector<std::string> recents);
	
	void sendPlaylistEvent(gfcNetPlaylistEvent theEvent);
	void sendPlaylistItem(gfcPlaylistItem item);
	void sendPlaylist(std::string playlist);
	void setClientAddress(std::string ip, std::string port);
	
	void sendRemotePointerColor(int color);

	void handleAllReady(); //this sets whatever needs to be set when the server tells us that all the players are ready.
	void handleSincStart(); //this sets whatever needs to be set when the server requests we sinc our state, like when we join. 
	void handleNewPlayer(); //when a new player joins. 
    int handleChatEvent();
    std::string gChatTextString;
    int chatLineOffset;
    int chatPosOffset;
    int gChatMode;
    float chatFadeCounter;
    
    //chat drawing variables from preferences
    int chatFontSize;
    bool chatTextBG;
    bool chatAutoFade;
    float chatOpacity;
    int chatDisplayLines;
    float chatFadeDelay;
    
    
    std::string getChatDisplayString();
    
    void draw(int w, int h, bool resized=false);

    void setTakeNotifications(bool value);

	void setSendRemoteLoadRequests(int value);
	int getSendRemoteLoadRequests();



    void notifyEvent(int networkEvent);
    void setEventSendDelay(int networkEvent, float delay);

	int allReady;
	int sincStatus_FX;
	int sincStatus_LUT;
	int sincStatus_Stacks;
	int sincStatus_Playlist;

private:
bool blinkerOn;
float cursorBlinkCounter;
bool connected;
bool isServer;
gfcNetworkClient client;
gfcNetworkServer server;
bool takeNotifications; ///this is turned on and off when we get a message that triggers events that send notifications to prevent message loops. 

void resetSincStatus();




int sendRemoteLoadRequests;

///These are turned on by different managers to indicate we should send each type of message.
///They are not the only type of events that we can send, but they are the ones that should not be 
///sent everytime we update, only every ms so we don't saturate the server and cpu.
///They are turned on by the notifyEvent method.
gfcNetworkEventNotification events[GFCNETEVENT_NUMOFEVENTTYPES];

// Pending live FX-attrib edits, keyed "quad/index/group/var" so repeated
// edits to the same widget within a throttle interval collapse to the latest
// value. Flushed in update() when events[GFCNETEVENT_FX] fires.
std::map<std::string, gfcNetFXAttribInfo> pendingFXAttribs_;

};

#endif

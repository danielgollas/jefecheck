#ifndef GFCNETWORKMANAGER_H
#define GFCNETWORKMANAGER_H

#include "gfcNetworkStructures.h"
#include "gfcnetworkclient.h"
#include "gfcnetworkserver.h"
#include "gfcnetworkeventnotification.h"

#include "RakPeerInterface.h"
#include "MessageIdentifiers.h"
#include "RakNetworkFactory.h"
#include <stdio.h>
#include "gfcNetworkStructures.h"
#include "StringCompressor.h"


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
    std::vector<std::string> chatLogLines();
    std::vector<std::string> drainErrors();

    void sendChatMessage();
    
    void sendPlayPauseMessage(gfcNetPlayPauseInfo info);
    
    void sendFXAddMessage ( gfcNetFXAddInfo info );
    //void sendSetFXStackMessage ( gfcNetLoadFXStackInfo info );
    void sendFXCommonMessage ( gfcNetFXCommonInfo info);
    void sendFXAttribMessage(gfcNetFXAttribInfo info);
    
	void sendFXStackMessage(gfcNetFXStackMessage message);
        
    void sendSystemChatMessage(std::string message, int type);
    
    void sendPointerInfoMessage(gfcNetPointerInfo message);
    
    bool getConnected();
    bool getIsServer();
    
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

};

#endif

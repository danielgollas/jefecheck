#ifndef GFCNETWORKSTRUCTURES_H
#define GFCNETWORKSTRUCTURES_H

#include <string>

#include <stdio.h>

#include <string>
#include <vector>
#include <map>
#include <set>
#include <stdio.h>
#include "BitStream.h"

#include "gfcWire.h"

#include "trilerp.h"

#include "gfcStructures.h"

#include "gfcfx.h"

#include "gfcnetremotepointerinfo.h"

#ifdef _WIN32
#include <windows.h> // Sleep
#else
#include <unistd.h> // usleep
#include <cstdio>
#endif


#define GFCNET_MAX_NICKNAME_LENGHT 50
#define GFCNET_MAX_TEXT_LENGHT 32000
#define GFCNET_CHAT_FADE_SPEED 1
#define GFCNET_POINTER_SEND_FREQUENCY 0.0133333
#define GFCNET_REMOTE_POINTER_FADE_SPEED 4

//#define GFCNET_TRANSFORMATION_SEND_FREQUENCY 0.0333
#define GFCNET_TRANSFORMATION_SEND_FREQUENCY 0.01333

class gfcFX;
enum gfcNetStatusColors{GFCCOLOR_GREEN, GFCCOLOR_YELLOW, GFCCOLOR_RED,GFCCOLOR_GRAY};

//broadcast message id's are only used when the message is transformed by the server.
constexpr unsigned char GFCNET_USER_PACKET_BASE = 91; // == RakNet ID_USER_PACKET_ENUM (verified by probe); static_assert in gfcRakNetTransport.cpp
enum gfcNetPacketEnums{
GFCNETID_NICKNAMESEND=GFCNET_USER_PACKET_BASE,GFCNETID_NICKALREADYINUSE,GFCNETID_PEERSINSESSION, GFCNETID_NEWPEERINSESSION,
GFCNETID_CHATMESSAGE,GFCNETID_CHATBROADCASTMESSAGE,
GFCNETID_POINTERINFOMESSAGE,GFCNETID_POINTERINFOBROADCASTMESSAGE,
GFCNETID_TRANSFORMATIONMESSAGE, GFCNETID_TRANSFORMATIONBROADCASTMESSAGE,
GFCNETID_PLAYPAUSEMESSAGE,GFCNETID_PLAYPAUSEBROADCASTMESSAGE,
GFCNETID_OTHERSTATESMESSAGE,GFCNETID_OTHERSTATESBROADCASTMESSAGE, 
GFCNETID_FXADDMESSAGE,GFCNETID_FXADDBROADCASTMESSAGE,
GFCNETID_FXCOMMONMESSAGE,GFCNETID_FXCOMMONBROADCASTMESSAGE, GFCNETID_FXATTRIBMESSAGE, GFCNETID_FXATTRIBBROADCASTMESSAGE, 
GFCNETID_FXSTACKMESSAGE,GFCNETID_FXSTACKBROADCASTMESSAGE,
GFCNETID_SENDPLAYLIST, GFCNETID_PLAYLISTITEMLOADMESSAGE, GFCNETID_PLAYLISTEVENTOTHER,
GFCNETID_SENDREMOTEPOINTERCOLOR,
GFCNETID_COLORCORRECTIONMESSAGE,GFCNETID_COLORCORRECTIONBROADCASTMESSAGE,
GFCNETID_LAYERCHANGEMESSAGE,

//the following are handshake specific messages
GFCNETID_REQUESTFXHASHES, GFCNETID_LOADEDFXSHASHES, GFCNETID_REQUESTFXS, GFCNETID_REQUESTEDFXS, GFCNETID_MISSINGFXS, GFCNETID_FXSINCCOMPLETE,
GFCNETID_REQUESTLUTSHASHES, GFCNETID_LOADEDLUTSHASHES, GFCNETID_REQUESTLUTS, GFCNETID_REQUESTEDLUTS, GFCNETID_MISSINGLUTS, GFCNETID_LUTSSINCCOMPLETE,
GFCNETID_SENDFXTACKS, GFCNETID_RECEIVEDFXSTACKS, GFCNETID_SENDSTACKSINCFINISHED,
GFCNETID_REQUESTPLAYLIST, GFCNETID_SENDPLAYLISTFORMERGE, GFCNETID_MERGEDPLAYLISTS, GFCNETID_PLAYLISTMERGEFINISHED, 
GFCNETID_SENDALLREADY,

};

enum gfcNetChatMessageEnums{
GFCNETMESSAGETYPE_NORMAL,
GFCNETMESSAGETYPE_SYSTEM,
GFCNETMESSAGETYPE_LOAD
};

// legacy BitStream overloads — removed in Task 4 (JEF-23); live client/server
// handlers still call them until Tasks 3/4 port those TUs to jefe::wire.
void serializeFX ( const gfcFX* theFX, RakNet::BitStream* bs );
void unserializeFX ( RakNet::BitStream* bs );

void serializeLUT ( CubeLUT* theLUT, RakNet::BitStream* bs );
void unserializeLUT ( RakNet::BitStream* bs );

// jefe::wire overloads (JEF-23). Field sequences are identical to the legacy
// BitStream versions above, modulo the sanctioned wire changes: strings are
// u32-length-prefixed (StringCompressor dropped), the legacy explicit
// "length" fields preceding each compressed string are dropped (writeString
// carries the length), and the LUT file body travels as length-prefixed raw
// bytes. The unserialize overloads read ALL fields before performing any
// side effects (file writes / manager loads) and return false — with no
// side effects — on a truncated/malformed buffer.
void serializeFX ( const gfcFX* theFX, jefe::wire::Writer& w );
bool unserializeFX ( jefe::wire::Reader& r );

void serializeLUT ( CubeLUT* theLUT, jefe::wire::Writer& w );
bool unserializeLUT ( jefe::wire::Reader& r );

// Extracts the HH:MM token from an asctime-style string
// ("Jul  4 14:32:56 2026" -> "14:32"). Returns the input unchanged
// if no NN:NN run is found. Never throws.
std::string shortTime(const std::string& asctimeStr);

class gfcChatLogEntry{
public:
gfcChatLogEntry() : type(0), color(0) {}   // color 0 = unset
unsigned char type;
std::string time;
std::string sender;
std::string message;
int color;   // sender's server-assigned packed-RGB color (0 = unset -> neutral)

std::string getFormattedString();
};

class gfcServerParams{
public:
char serverName[60];
char password[60];
int port;
};


class gfcConnectionParams{
public:
std::string serverIP;
int port;
std::string password;
std::string nickname;

};

//#pragma pack(push,1)
struct gfcNetSimpleMessage{ //simple message that carries no other info but myTypeID
unsigned char mytypeID;
};
//#pragma pack(pop)

//#pragma pack(push,1)
struct gfcNetNickNameMessage{
unsigned char mytypeID;
char nickName[GFCNET_MAX_NICKNAME_LENGHT];
};
//#pragma pack(pop)

//#pragma pack(push,1)
struct gfcNetPeersInSessionMessage{
unsigned char mytypeID;
int numOfPeers;
char names[1][GFCNET_MAX_NICKNAME_LENGHT];
};
//#pragma pack(pop)

//#pragma pack(push,1)etry, Comics, Trade Journals, Newspapers, Trade Journals
struct gfcNetChatMessage{
unsigned char mytypeID;
char text[256];
};
//#pragma pack(pop)

//#pragma pack(push,1)
struct gfcNetChatBroadcastMessage{
unsigned char mytypeID;
char text[500];
};
//#pragma pack(pop)

//#pragma pack(push,1)
struct gfcNetPointerInfo{
int x;
int y;
int quadID;
float scale;
int color;
};
//#pragma pack(pop)

//#pragma pack(push,1)
class gfcNetTransformationInfo
{
public:
	gfcNetTransformationInfo(){
		tX=tY=rZ=0;
		scale=1;
	}
	float tX;
	float tY;
	float scale;
	float rZ;
};
//#pragma pack(pop)

//#pragma pack(push,1)
struct gfcNetNickAlreadyTakenMessage{
unsigned char mytypeID;
};
//#pragma pack(pop)

//#pragma pack(push,1)
struct gfcNetPointerInfoMessage{
unsigned char mytypeID;
gfcNetPointerInfo info;
};
//#pragma pack(pop)


//#pragma pack(push,1)
struct gfcNetPointerInfoBroadcastMessage{
unsigned char mytypeID;
char nickname[GFCNET_MAX_NICKNAME_LENGHT];
gfcNetPointerInfo info;
};
//#pragma pack(pop)

//#pragma pack(push,1)
struct gfcNetTransformationMessage{
unsigned char mytypeID;
gfcNetTransformationInfo info[4];
};
//#pragma pack(pop)


/*
 When a player receives a play message, it' changes the current frame to "frame", and starts playback. In theory, all clients should always be on the same frame, so the current frame change should be irrelevant. When a player receives a pause message, it will do one of two things, if "frame" is behind the current frame, it will stop and jump current frame to "frame"
 If current frame is bigger (or smaller, depending on the playback direction) than "frame", a flag will be raised to indicate what frame the idle function should stop on, so that we will continue playback until we reach the correct frame. OR NOT, MAYBE IN THE FUTURE< TO MUCH HASTLE TO CORRECT A PROBLEM THAT WILL PROBABLY NEVER ARAISE, AND IF IT DOES, IT'S NOT THAT BIG OF A DEAL. JUST JUMP TO FRAME WHEN PLAY OR PAUSE MESSAGE IS RECEIVED. PLAYER THAT STARTS THE MESSAGE HAS PRIORITY. IN THEORY, A MESSAGE SHOULD NOT TAKE MORE THAN 1/24 SECONDS TO REACH ALL PLAYERS.
*/
//#pragma pack(push,1)
struct gfcNetPlayPauseMessage{
unsigned char mytypeID;
bool play; //start play, or pause.
int frame; //the frame to start playback from in case it's start play message, or the frame where we should stop on in case of pause message
};
//#pragma pack(pop)

/*
	Contains many rarely changed quadrant states which can be sent in bulk since they are not sent frequently.
*/
//#pragma pack(push,1)
struct gfcNetQuadrantStateInfo{
int quadID;
bool flip;
bool flop;
float rz;
char aspect[50];
bool crop;
int weight;
bool r;
bool g;
bool b;
bool a;
unsigned char track;
};
//#pragma pack(pop)

/*
	Contains many rarely changed track states which can be sent in bulk since they are not sent frequently.
*/
//#pragma pack(push,1)
struct gfcNetTrackStateInfo{
	int frameOffset;
	int holdMode;
	int holdFrame;
};
//#pragma pack(pop)

/*
	Contains many rarely changed system states which can be sent in bulk since they are not sent frequently.
*/
//#pragma pack(push,1)
struct gfcNetOtherStatesMessage{
unsigned char mytypeID;
gfcNetQuadrantStateInfo quadInfo[4]; //one quadrant state info per quadrant.
gfcNetTrackStateInfo trackInfo[4]; //one track state info per track;
//common system attributes to all quadrants.
 int layout;
 int playbackMode;
 float targetFPS;
 int from;
 int to;
};
//#pragma pack(pop)

///Contains info about the playback manager to be transmited over a remote session
struct gfcNetPlaybackInfo{
 int playbackMode;
 int loopPriority;
 float targetFPS;
 int from;
 int to;
 int inPoint;
 int outPoint;
};

struct gfcNetPlayPauseInfo{
bool play; //start play, or pause.
int direction;
int frame; //the frame to start playback from in case it's start play message, or the frame where we should stop on in case of pause message
};

class gfcNetPlateColorCorrectionInfo{


public:
	gfcNetPlateColorCorrectionInfo(){
		lutName="";
		gamma=1.0;
		exposure=0.0;
		brightness=1.0;
		contrast=1.0;
		saturation=1.0;
	}

	void saveToNode(XMLNode &pnode) const
	{
		XMLNode xCCNode=pnode.addChild("CC");
		xCCNode.addAttribute("lutName",lutName.c_str());
		xCCNode.addAttribute("gamma",ftos(gamma).c_str());
		xCCNode.addAttribute("exposure",ftos(exposure).c_str());
		xCCNode.addAttribute("brightness",ftos(brightness).c_str());
		xCCNode.addAttribute("contrast",ftos(contrast).c_str());
		xCCNode.addAttribute("saturation",ftos(saturation).c_str());
	}
	
	void loadFromNode(XMLNode &pnode)
	{
		
		lutName=pnode.getAttribute("lutName");
		gamma=atof(pnode.getAttribute("gamma"));
		exposure=atof(pnode.getAttribute("exposure"));
		brightness=atof(pnode.getAttribute("brightness"));
		contrast=atof(pnode.getAttribute("contrast"));
		saturation=atof(pnode.getAttribute("saturation"));
	}

	int quadID;
	std::string lutName;
	float gamma;
	float exposure;
	float brightness;
	float contrast;
	float saturation;
};

struct gfcNetPlateStateInfo{
int quadID;
bool flip;
bool flop;
float rz;
std::string aspect;
bool crop;
bool r;
bool g;
bool b;
bool a;
unsigned char track;
};

struct gfcNetOtherStatesInfo{
	gfcNetPlaybackInfo playbackInfo;
	std::vector<gfcNetPlateStateInfo> plateStateInfo;
	std::vector<gfcNetTrackStateInfo> trackStateInfo;
	int layout;
	
};


struct gfcNetFXAppliedID{
int quadID; //what quadrant this FX message belongs to
int index; //the index of the FX in the 
};



void printOtherStatesMessage(gfcNetOtherStatesMessage *message);

/*
 Contains information to identify the FX in a remote system's fxArray (available fxs, not applied ones).
*/
struct gfcNetFXID{
int quadID; //what quadrant this FX message belongs to
std::string hash; //the Hash that id's this FX
};


struct gfcNetFXAddInfo{
gfcNetFXID id;
};

struct gfcNetFXStackMessage
{
	int quadID; //what stack does this FX belong to
	std::string theStack;

};

struct gfcNetPlaylistMessage 
{
	std::string thePlaylist;
};


struct gfcNetPlaylistEvent
{
   int selectedItem;
};

/*
This message will moify the not often changed properties of the FX, like deleting it, on/off, reset, move up/down.
*/

struct gfcNetFXCommonInfo{
gfcNetFXAppliedID id;

int onOff;  //if !=0, means modify the on/Off state of the fx:0 do nothing, 1 is off, 2 is on.
bool reset;  //if !=0 means reset;
int upDown; //if !=0, means modify the position of the FX:0 do nothing, 1 is up, 2 is down.
bool remove; //if !=0, means delete the fx
};


/*
This message will moify the often changed properties of the FX, floats, bools, textures, luts etc.
*/
struct gfcNetFXAttribInfo{
gfcNetFXAppliedID id;
std::string groupName; //what group this attribute belongs to
std::string variableName; //what variable we are modifying.
unsigned char attribType; //FX_GUI_FLOAT, FX_GUI_BOOL etc
float theFloat;
int theInt; //for choices or textures or bools;
std::string lutOrCube; // for LUTs and CUBEs
};





struct gfcNetFXAddMessage{
unsigned char mytypeID;
gfcNetFXID id;
};


/*
This message will moify the not often changed properties of the FX, like deleting it, on/off, reset, move up/down.
*/

struct gfcNetFXCommonMessage{
unsigned char mytypeID;
gfcNetFXAppliedID id;

int onOff;  //if !=0, means modify the on/Off state of the fx:0 do nothing, 1 is off, 2 is on.
bool reset;  //if !=0 means reset;
int upDown; //if !=0, means modify the position of the FX:0 do nothing, 1 is up, 2 is down.
bool remove; //if !=0, means delete the fx
};


/*
This message will moify the often changed properties of the FX, floats, bools, textures, luts etc.
*/
struct gfcNetFXAttribMessage{
unsigned char mytypeID;
gfcNetFXAppliedID id;
char groupName[50]; //what group this attribute belongs to
char variableName[50]; //what variable we are modifying.
unsigned char attribType; //FX_GUI_FLOAT, FX_GUI_BOOL etc
float theFloat;
int theInt; //for choices or textures or bools;
char lutOrCube[60]; // for LUTs and CUBEs
};





#endif //GFCNETWORKSTRUCTURES_H

#include "gfcnetworkmanager.h"
#include "gfcTextRenderer.h"

#include "gfcNetworkStructures.h"

#include "gfcnetworklog.h"
extern gfcNetworkLog networkLog;

#include "gfcplaybackmanager.h"
extern gfcPlaybackManager playbackManager;

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

#include "gfctrackmanager.h"
extern gfcTrackManager trackManager;

#include <sstream>
#include <chrono>
#include <random>
#include <thread>

namespace {
/**
 * JEF-37: a fresh per-session secret for the host's own loopback client.
 *
 * 32 hex characters (128 bits) from std::random_device — enough that guessing
 * it is not a strategy even for someone who already knows the session code,
 * and short enough to survive the coordinator's display-name length cap.
 * Regenerated per session, so a leaked one is worthless the moment the session
 * ends. random_device is used directly rather than seeding a PRNG: this runs
 * once per session, so its cost is irrelevant and its unpredictability is not.
 */
std::string makeSelfJoinNonce() {
    static const char* kHex = "0123456789abcdef";
    std::random_device rd;
    std::string out;
    out.reserve(32);
    for (int i = 0; i < 32; ++i) out.push_back(kHex[rd() & 0xf]);
    return out;
}

std::vector<std::string> wrapToWidth(const std::string& text, int maxW) {
    std::vector<std::string> lines;
    std::istringstream iss(text);
    std::string word, cur;
    while (iss >> word) {
        std::string trial = cur.empty() ? word : cur + " " + word;
        if (cur.empty() || (int)textRenderer().textWidth(trial.c_str()) <= maxW)
            cur = trial;
        else { lines.push_back(cur); cur = word; }
    }
    if (!cur.empty()) lines.push_back(cur);
    if (lines.empty()) lines.push_back("");
    return lines;
}
inline void unpackRGB(int packed, float& r, float& g, float& b) {
    if (packed == 0) { r = g = b = 0.6f; return; }   // neutral
    r = ((packed >> 24) & 0xff) / 255.0f;
    g = ((packed >> 16) & 0xff) / 255.0f;
    b = ((packed >>  8) & 0xff) / 255.0f;
}
}  // namespace


gfcNetworkManager networkManager;




gfcNetworkManager::gfcNetworkManager()
{
    chatFontSize=12;
    chatTextBG=true;
    chatAutoFade=true;
    chatOpacity=0.75;
    chatDisplayLines=8;
    chatFadeDelay=25;   // seconds the chat overlay holds before fading (tunable in Preferences)
    takeNotifications=true;
	blinkerOn=false;

	// Connection/chat state read by pumpNetwork() from the first tick.
	connected=false;
	isServer=false;
	gChatMode=0;
	chatFadeCounter=0;
	cursorBlinkCounter=0;
	// The overlay bubble draw reads these for the visible-message window and
	// the typing-cursor position; never assigned elsewhere, so init to 0 to
	// avoid reading indeterminate values (garbage would blank the overlay).
	chatLineOffset=0;
	chatPosOffset=0;

	sendRemoteLoadRequests=true;

	allReady=0;
	sincStatus_FX=0;
	sincStatus_LUT=0;
	sincStatus_Stacks=0;
	sincStatus_Playlist=0;
	
}


gfcNetworkManager::~gfcNetworkManager()
{
}

void gfcNetworkManager::resetSincStatus()
{
	allReady=0;
	sincStatus_FX=0;
	sincStatus_LUT=0;
	sincStatus_Stacks=0;
	sincStatus_Playlist=0;
}

void gfcNetworkManager::setSendRemoteLoadRequests(int value){
	sendRemoteLoadRequests=value;
}
int gfcNetworkManager::getSendRemoteLoadRequests()
{
	return sendRemoteLoadRequests;
}



void gfcNetworkManager::startServer(gfcServerParams * params)
{

	if ( connected && !isServer) {
            return;
        }
        
        //1. Startup Server and disable client gui so we can't connect as client from the gui when acting as server
        client.disableGUI();
		
		resetSincStatus();
        
        // JEF-37: mint this session's self-join nonce BEFORE the server starts,
        // so it is already in place when the loopback client knocks. Without
        // one, a host with knocking on (the default) waits to admit its own
        // client and the session never gains a single participant.
        if (params && params->coordinatorMode && params->selfJoinNonce.empty())
            params->selfJoinNonce = makeSelfJoinNonce();

		server.start(params);
        
        
        //2. Connect to out own server from the client, since the machine that acts as server is also a client.
        //We use the connect method using params so we can override the client's GUI params.
        networkLog.addToLog("Starting Loopback Client...");
        gfcConnectionParams clientParams;
        clientParams.nickname=server.getName();
        clientParams.serverIP="127.0.0.1";
        clientParams.port=server.getPort();
        clientParams.password=server.getPassowrd();
        // JEF-27: in coordinator mode the loopback client joins the host's own
        // cloud session by its assigned code instead of dialing 127.0.0.1. The
        // code is assigned ASYNCHRONOUSLY by the coordinator (a background
        // rtc::WebSocket thread sets it once the create-session round-trip
        // completes), so at this point it is very likely still empty. Wait for
        // it here — bounded — before connecting the loopback client with a valid
        // code. The wait needs no app-side pumping: the coordinator socket runs
        // on libdatachannel's own threads, so getAssignedSessionCode() flips
        // non-empty on its own. LAN/RakNet hosting (coordinatorMode=false) skips
        // the wait entirely, so that path is unchanged.
        clientParams.coordinatorMode=server.getCoordinatorMode();
        clientParams.coordinatorUrl=server.getCoordinatorUrl();
        // Present the nonce to the COORDINATOR only. `nickname` above is what
        // participants see, and it is untouched — the two names are separate
        // precisely so this one can be a secret.
        if (params) clientParams.coordDisplayName=params->selfJoinNonce;
        if (server.getCoordinatorMode()) {
            const int kCodeTimeoutMs = 5000;
            for (int t = 0; t < kCodeTimeoutMs; t += 20) {
                if (!server.getAssignedSessionCode().empty()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            // NO CODE => NO SESSION. The coordinator refused create-session
            // (auth-required, insufficient-credits) or never answered.
            //
            // Bailing out here is load-bearing: the tail of this function used
            // to set connected=true unconditionally, so a refused session still
            // flipped the UI into its session view — an empty participant list,
            // chat that went nowhere, and an End Session button with nothing to
            // end. "Connected" has to mean a session exists, or every symptom
            // of a failed start looks like a bug somewhere else.
            if (server.getAssignedSessionCode().empty()) {
                networkLog.addToLog(
                    "Cloud session was refused by the coordinator (no session "
                    "code assigned). Check that you are signed in and have "
                    "credits.");
                server.stop();
                client.enableGUI();
                isServer = false;
                connected = false;
                return;
            }
        }
        clientParams.sessionCode=server.getAssignedSessionCode();
        client.setIsServerClient(true);
        client.Connect(&clientParams);
        networkLog.addToLog("Loopback Client Started");
	isServer=true;
	connected=true;

}

void gfcNetworkManager::initializeWidgets()
{
	server.initializeWidgets();
	client.initializeWidgets();
}

void gfcNetworkManager::stopServer()
{
	client.Disconnect();
	server.stop();
	isServer=false;
	connected=false;
	pendingFXAttribs_.clear();   // drop un-flushed edits so they can't replay
	client.enableGUI();


}

void gfcNetworkManager::stopConnection()
{
    client.Disconnect();
    isServer = false;
    connected = false;
    pendingFXAttribs_.clear();   // drop un-flushed edits so they can't replay
    server.enableGUI();
}

std::vector<std::string> gfcNetworkManager::participantNames() {
    if (isServer) return server.getParticipantNames();
    return client.getPeersInSession();
}

std::string gfcNetworkManager::getAssignedSessionCode() {
    return server.getAssignedSessionCode();
}

std::vector<jefe::net::PendingJoiner> gfcNetworkManager::pendingJoiners() {
    if (!isServer) return {};
    return server.getPendingJoiners();
}

void gfcNetworkManager::decideJoiner(const std::string& joinerId, bool admit) {
    // Guarded by isServer, not merely by "the UI only shows this to a host":
    // admit/deny is a host-only action at the coordinator too, and a joiner
    // sending one would earn a protocol error for a button it should never
    // have had.
    if (!isServer) return;
    server.decideJoiner(joinerId, admit);
}

// JEF-30: forward to the active transport. A host's stats come from the SERVER
// transport (its view of every joined peer); a joiner's from the CLIENT
// transport (its single peer, the host). Solo → empty.
std::vector<jefe::net::PeerStats> gfcNetworkManager::peerStats() {
    if (!connected) return {};
    return isServer ? server.peerStats() : client.peerStats();
}

std::string gfcNetworkManager::peerNickname(jefe::net::PeerId peer) {
    return isServer ? server.nicknameForPeer(peer) : std::string();
}

std::string gfcNetworkManager::connectionStatusText() {
    if (isServer) return connected ? "Hosting (server)" : "Not hosting";
    return client.getStatus();   // e.g. "Online!", "Attempting Connection...", "Offline"
}

std::vector<std::string> gfcNetworkManager::chatLogLines() {
    std::vector<std::string> out;
    for (auto& e : client.getChatLog())
        out.push_back(e.sender + ": " + e.message);
    return out;
}

std::vector<gfcNetworkManager::ChatEntryData> gfcNetworkManager::chatEntries() {
    std::vector<ChatEntryData> out;
    const std::string me = client.getNickName();
    for (auto& e : client.getChatLog()) {
        ChatEntryData d;
        d.sender   = e.sender;
        d.message  = e.message;
        d.timeHHMM = shortTime(e.time);
        d.type     = e.type;
        d.isSelf   = (!e.sender.empty() && e.sender == me);
        d.color    = e.color;
        out.push_back(d);
    }
    return out;
}

std::vector<std::string> gfcNetworkManager::drainErrors() {
    // Errors already surface through client status strings (RED). Reserved
    // for a dedicated error queue; empty for now so callers compile.
    return {};
}

bool gfcNetworkManager::getConnected()
{
	return connected;
}

bool gfcNetworkManager::getIsServer()
{
	return isServer;
}

bool gfcNetworkManager::overlayAnimating()
{
	// The chat/status overlay is fading out (chatFadeCounter counting down) or
	// the user is typing a chat message — either needs the idle tick to keep
	// repainting so the fade actually animates instead of jumping.
	return chatFadeCounter > 0 || gChatMode != 0;
}

bool gfcNetworkManager::consumeGotMessages()
{
	// client.GetGotMessages() returns true and clears the flag if any packet was
	// processed since the last call. Used to repaint the receiver's viewport when
	// mirrored state arrives (otherwise QOpenGLWidget only repaints on local input).
	return client.GetGotMessages();
}

std::vector<std::string> gfcNetworkManager::networkLogLines()
{
	extern gfcNetworkLog networkLog;
	return networkLog.getLog();
}

void gfcNetworkManager::update()
{
	//update client and server 
	{
		server.Update();
		client.Update();
	}
	
	//check for client status changes
	if(client.statusChange)
	{
		client.statusChange=false;
		if(!client.getIsConnected() && !client.getAttemptingConnection())
		{
			server.enableGUI();
			if(!isServer) connected = false;   // client peer dropped
		}
		
		if(client.getIsConnected())
		{
			connected=true;
		}
		
		if(client.getGotNewChatMessage())
		{
			chatFadeCounter=chatFadeDelay/GFCNET_CHAT_FADE_SPEED;
		}
	}
	
	
	//Check the event notification flags from the rest of the program and send appropiate messages.
	if(connected)
	{
	// JEF-4: the multi-step asset-sync readiness handshake (FX/LUT/stack/playlist
	// merge -> SENDALLREADY) does not complete in the Qt port, so `allReady` was
	// staying 0 forever and draw() rendered a blocking gray "please wait" box over
	// the frames. Mirroring/chat/pointers do NOT depend on allReady, so treat the
	// session as ready once connected. Full asset-sync completion is a follow-up
	// ticket (out of JEF-4 scope).
	allReady=1;
	float timeStep=playbackManager.getTimestep();
	//check transformations
	if(events[GFCNETEVENT_TRANSFORMS].readyForSend(timeStep))
	{
		events[GFCNETEVENT_TRANSFORMS].processed();
		//printf("Sending transforms\n");
		client.SendTransformations(plateManager.getTransformations());
	}

	if (events[GFCNETEVENT_COLOR].readyForSend(timeStep))
	{
		events[GFCNETEVENT_COLOR].processed();
		client.SendColorCorrections(plateManager.getColorCorrections());
	}
	
	
	//check fx — flush the coalesced live param edits at the throttle rate.
	if(events[GFCNETEVENT_FX].readyForSend(timeStep))
	{
		events[GFCNETEVENT_FX].processed();
		for (auto& kv : pendingFXAttribs_)
			client.SendFXAttribMessage(kv.second);
		pendingFXAttribs_.clear();
	}
	
	
	
	
	//check other
	if(events[GFCNETEVENT_OTHER].readyForSend(timeStep))
	{
		events[GFCNETEVENT_OTHER].processed();
		//printf("Sending other\n");
		gfcNetOtherStatesInfo tmp;
		tmp.layout=plateManager.getFramingMode();
		tmp.playbackInfo=playbackManager.getPlaybackInfo();
		tmp.plateStateInfo=plateManager.getPlateStateInfo();
		tmp.trackStateInfo=trackManager.getTrackStateInfo();
		client.SendOtherStatesMessage(tmp);
	}

	
	
	


	}
	
	//update the chat fade even if not connected so we can fade out when disconnected
	//also update the chat blinker
	{
		if ( gChatMode==0 ){
			cursorBlinkCounter=0;
			chatFadeCounter-=playbackManager.getTimestep()*GFCNET_CHAT_FADE_SPEED*chatAutoFade;
			if (chatFadeCounter>0 && chatFadeCounter<chatOpacity)
			{
				plateManager.setChanged();
			}
		}
		else
		{
			cursorBlinkCounter+=playbackManager.getTimestep();

			if ( cursorBlinkCounter>0.25 ){
				blinkerOn=!blinkerOn;
				cursorBlinkCounter=0;
				plateManager.setChanged();
			}
		}
	}


}

void gfcNetworkManager::startConnection(gfcConnectionParams * params)
{
	resetSincStatus();
	if(client.getAttemptingConnection() || client.getIsConnected()) //if already trying to connect or connected, disconnect
	{
		client.Disconnect();
		connected=false;
	}
	else
	{
	
	client.Connect(params);
	server.disableGUI();
	}
}

void gfcNetworkManager::startFXSinc()
{
	//if we are a server, we start an FX sinc...
	if(this->connected){
		if(this->isServer)
		{
			server.startFXSinc(client.getServerPeerId(),true); //broadcaste the event
		}
		else{//...if we are a client, we send a Loaded FX hashes.
			client.SendLoadedFXsHashes();
		}
	}
}

void gfcNetworkManager::startLUTSinc()
{
	//if we are a server, we start a LUT sinc...
	if(this->connected){
		if(this->isServer)
		{
			server.startLUTSinc(client.getServerPeerId(),true);	//broadcaste the event
		}
		else{//...if we are a client, we send a Loaded LUT hashes.
			client.SendLoadedLUTsHashes();
		}
	}
}

void gfcNetworkManager::startStackSinc()
{
	//if we are a server, we send the stack
	if(this->connected){
		if(this->isServer)
		{
			server.startFXSinc(client.getServerPeerId(),true); //broadcaste the event
		}
		else{//...if we are a client, we send a Loaded FX hashes.
			client.SendLoadedFXsHashes();
		}
	}
}


void gfcNetworkManager::sendChatMessage()
{	
	//printf("sending: %s\n",gChatTextString.c_str());
	client.SendChatMessage(gChatTextString); //this simply sends the message in the chatString buffer to the server
}

std::string gfcNetworkManager::getChatDisplayString()
{
		return "";
}

void setupAnfPrintSincStatus(int value, std::string sincWaitString,int w, int h)
{
	gfc_gl_font(FL_HELVETICA, 24);
	if (value==1)
	{
		sincWaitString+="...Done\n";
		textRenderer().setColor(0.0, 0.7, 0.0, 1.0);
	}
	else
	{
		textRenderer().setColor(0.9, 0.9, 0.9, 1.0);
	}
	gfc_gl_draw(sincWaitString.c_str(), -w/4+10, -h/4+10, w/2-10, h/2-10, FL_ALIGN_CENTER | FL_ALIGN_TOP | FL_ALIGN_WRAP | FL_ALIGN_INSIDE);
}

void gfcNetworkManager::draw(int w, int h, bool resized)
{
	 //START DRAWING THE ONLINE STUFF
    {        
        //DRAW CONNECTION STATUS STUFF
		if (connected && !allReady)
		{
			//draw the status box with the current sync state

			glPushAttrib(GL_ALL_ATTRIB_BITS);
			
			glMatrixMode ( GL_PROJECTION );
			glPushMatrix();
			glLoadIdentity();
			glOrtho ( -w /2.0, w /2.0, -h /2.0, h /2.0, -5000.0, 5000.0 );

			glMatrixMode ( GL_MODELVIEW );
			glPushMatrix();
			glLoadIdentity();
			glViewport ( 0,0,w,h );

			
			glEnable ( GL_BLEND );
			glDisable ( GL_TEXTURE_RECTANGLE_ARB );
			glBlendFunc ( GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA );

		

			 
			//glColor4f ( 0.1,0.1,0.1,chatFadeCounter>chatOpacity?chatOpacity:chatFadeCounter );

			//draw a dark gray box with a light gray frame
			float darkGray=0.2;
			float boxOpacity=0.75;
			glColor4f( darkGray,darkGray,darkGray,boxOpacity);
			gl_rectf( -w/4, -h/4, w/2, h/2 );
			GLint oldLineWidth;
			glGetIntegerv(GL_LINE_WIDTH,&oldLineWidth);
			glLineWidth(3);
			glColor4f( darkGray*2,darkGray*2,darkGray*2,boxOpacity);
			gl_rect( -w/4, -h/4, w/2, h/2 );
			glLineWidth(oldLineWidth);
			
			//build the string
			std::string sincWaitString="\nPlease wait until all users are ready\n\n";
			
			gfc_gl_font(FL_HELVETICA, 18);
			textRenderer().setColor(1, 1, 1, 1.0);
			gfc_gl_draw(sincWaitString.c_str(), -w/4+10, -h/4+10, w/2-10, h/2-10, FL_ALIGN_CENTER | FL_ALIGN_TOP | FL_ALIGN_WRAP | FL_ALIGN_INSIDE);

			sincWaitString="\n\n\nSyncing FXs";			
			setupAnfPrintSincStatus(sincStatus_FX,sincWaitString,w,h);

			sincWaitString="\n\n\n\nSyncing LUTs";
			setupAnfPrintSincStatus(sincStatus_LUT,sincWaitString,w,h);
			
			sincWaitString="\n\n\n\n\nSyncing FX Stacks";
			setupAnfPrintSincStatus(sincStatus_Stacks,sincWaitString,w,h);

			sincWaitString="\n\n\n\n\n\nMerging Playlists";
			setupAnfPrintSincStatus(sincStatus_Playlist,sincWaitString,w,h);
			
			if (sincStatus_LUT && sincStatus_FX && sincStatus_Stacks && sincStatus_Playlist && !allReady)
			{
				gfc_gl_font(FL_HELVETICA, 18);
				textRenderer().setColor(1, 1, 1, 1.0);
				sincWaitString="\n\n\n\n\n\n\n\n\nWaiting for other users\n";
				gfc_gl_draw(sincWaitString.c_str(), -w/4+10, -h/4+10, w/2-10, h/2-10, FL_ALIGN_CENTER | FL_ALIGN_WRAP | FL_ALIGN_INSIDE);
			}
			
			//printf("\n\n****Sinc Status****\n\nFX:%i\nLUT:%i\nStack:%i\nPlaylist:%i\nAllReady:%i\nString:\n%s\n",sincStatus_FX,sincStatus_LUT,sincStatus_Stacks,sincStatus_Playlist,allReady,sincWaitString.c_str());

			

			

			

			

			

			//glColor4f ( 1,1,1,chatFadeCounter>chatOpacity?chatOpacity:chatFadeCounter );

			//gl_draw ( chatDisplayString.c_str(),-w /2+10,-h /2 + 10,w,( gl_height() ) * ( logSize-beginLine+1+gChatMode ),Fl_Align ( FL_ALIGN_LEFT | FL_ALIGN_BOTTOM | FL_ALIGN_WRAP | FL_ALIGN_INSIDE) );
			glDisable ( GL_BLEND );
			glPopMatrix(); //pop modelview
			glMatrixMode ( GL_PROJECTION );
			glPopMatrix();//pop matrix
			glMatrixMode ( GL_MODELVIEW );

			glPopAttrib();
		}
		
        
        //DRAW CHAT STUFF
        
	    
            if ( chatFadeCounter>0 || gChatMode!=0 ) {

		glPushAttrib(GL_ALL_ATTRIB_BITS);

                // Work in LOGICAL pixels: gfc_gl_height()/textWidth() report in
                // logical px (they divide by the renderer's DPI scale), so the
                // bubble geometry must use a logical-px ortho or the boxes come
                // out half-size on a 2x Retina display while the text (rendered
                // at physical glyph size) overflows them. The viewport stays at
                // the full framebuffer size; only the ortho extents are logical.
                const float dprS = textRenderer().getDPIScale();
                const int wl = dprS > 0 ? (int)(w / dprS) : w;
                const int hl = dprS > 0 ? (int)(h / dprS) : h;

                glMatrixMode ( GL_PROJECTION );
                glPushMatrix();
                glLoadIdentity();
                glOrtho ( -wl /2.0, wl /2.0, -hl /2.0, hl /2.0, -5000.0, 5000.0 );

                glMatrixMode ( GL_MODELVIEW );
                glPushMatrix();
                glLoadIdentity();
                glViewport ( 0,0,w,h );

                glEnable ( GL_BLEND );
                glDisable ( GL_TEXTURE_RECTANGLE_ARB );
                glBlendFunc ( GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA );

                const float alpha = chatFadeCounter > chatOpacity ? chatOpacity : chatFadeCounter;
                gfc_gl_font(FL_HELVETICA, chatFontSize);
                const int lineH   = (int)gfc_gl_height();
                const int pad     = 6;
                const int margin  = 12;
                const int gap     = 8;
                const int maxW    = (int)(0.6 * wl);
                const std::string me = client.getNickName();

                std::vector<gfcChatLogEntry> log = client.getChatLog();
                int logSize = (int)log.size();
                int begin = logSize - chatDisplayLines - chatLineOffset;
                if (begin < 0) begin = 0;
                int end = logSize - chatLineOffset;
                if (end > logSize) end = logSize;

                // Layout bottom-up: y is the current baseline stack cursor from the bottom.
                int y = -hl / 2 + margin;

                // Typing bubble first (lowest), if composing.
                if (gChatMode == 1) {
                    std::string typed = gChatTextString;
                    if (blinkerOn) typed.insert(std::min((size_t)chatPosOffset, typed.size()), "|");
                    if (typed.empty()) typed = " ";
                    std::vector<std::string> lines = wrapToWidth(typed, maxW - 2 * pad);
                    int bw = 0;
                    for (auto& l : lines) bw = std::max(bw, (int)textRenderer().textWidth(l.c_str()));
                    bw += 2 * pad;
                    int bh = (int)lines.size() * lineH + 2 * pad;
                    int x = wl / 2 - margin - bw;   // self = right
                    glColor4f(0.18f, 0.15f, 0.12f, alpha);      // accent-tint
                    gl_rectf(x, y, bw, bh);
                    textRenderer().setColor(0.91f, 0.72f, 0.52f, alpha);
                    std::string joined; for (auto& l : lines) { joined += l; joined += "\n"; }
                    gfc_gl_draw(joined.c_str(), x + pad, y + pad, bw - 2 * pad, bh - 2 * pad,
                                FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP | FL_ALIGN_INSIDE);
                    y += bh + gap;
                }

                // Messages, newest just above the typing bubble, older stacking upward.
                for (int i = end - 1; i >= begin; --i) {
                    const gfcChatLogEntry& e = log[i];
                    if (e.type != GFCNETMESSAGETYPE_NORMAL) {
                        // System/load: centered dim single line.
                        std::string s = e.message;
                        int tw = (int)textRenderer().textWidth(s.c_str());
                        int x = -tw / 2;
                        textRenderer().setColor(0.5f, 0.5f, 0.5f, alpha);
                        gfc_gl_draw(s.c_str(), x, y, tw + 4, lineH,
                                    FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE);
                        y += lineH + gap;
                        continue;
                    }
                    const bool self = (!e.sender.empty() && e.sender == me);
                    // ASCII separator: the GL text renderer's glyph atlas is
                    // ASCII-only (32-127), so a UTF-8 middle-dot would render as
                    // an invisible gap. The Qt panel (Task 5) uses a real "·"
                    // because QLabel handles UTF-8; the overlay uses " - ".
                    std::string header = (self ? "You" : e.sender) + " - " + shortTime(e.time);
                    std::vector<std::string> lines = wrapToWidth(e.message, maxW - 2 * pad);
                    int bw = (int)textRenderer().textWidth(header.c_str());
                    for (auto& l : lines) bw = std::max(bw, (int)textRenderer().textWidth(l.c_str()));
                    bw += 2 * pad;
                    int bh = (int)(lines.size() + 1) * lineH + 2 * pad;   // +1 header line
                    int x = self ? (wl / 2 - margin - bw) : (-wl / 2 + margin);

                    if (self) glColor4f(0.18f, 0.15f, 0.12f, alpha);       // accent-tint
                    else      glColor4f(0.14f, 0.14f, 0.14f, alpha);       // neutral
                    gl_rectf(x, y, bw, bh);

                    float cr, cg, cb; unpackRGB(e.color, cr, cg, cb);
                    textRenderer().setColor(cr, cg, cb, alpha);
                    gfc_gl_draw(header.c_str(), x + pad, y + bh - pad - lineH, bw - 2 * pad, lineH,
                                FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE);
                    textRenderer().setColor(0.86f, 0.86f, 0.86f, alpha);
                    std::string joined; for (auto& l : lines) { joined += l; joined += "\n"; }
                    gfc_gl_draw(joined.c_str(), x + pad, y + pad, bw - 2 * pad,
                                (int)lines.size() * lineH,
                                FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP | FL_ALIGN_INSIDE);
                    y += bh + gap;
                }

                glDisable ( GL_BLEND );
                glPopMatrix(); //pop modelview
                glMatrixMode ( GL_PROJECTION );
                glPopMatrix();//pop matrix
                glMatrixMode ( GL_MODELVIEW );

		glPopAttrib();
            }
             
        
        
       
        //DRAW REMOTE POINTERS: Remote pointers are drawn in the gfcPlates since they need to have all the transformation.

    }
    //END OF DRAWING THE ONLINE STUFF
}

void gfcNetworkManager::saveChatLog(std::string sfilename)
{
	  if ( sfilename.empty() ) {
        printf ( "Not a filename\n" );
        return;
    }

    

    //if extension is wrong append it.
    /*if ( strrchr ( pfileName,'.' ) ==NULL  || ( ( strcmp ( strrchr ( pfileName,'.' ),".txt" ) !=0 ) ) ) {
        sfileName+=".txt";
        printf ( "Corrected filename %s\n",sfileName.c_str() );
    }*/
    
    sfilename=AppendExtensionToFilename(sfilename,".txt");

    std::ofstream outFile;

    //write jfxFile
    outFile.open ( sfilename.c_str(),std::ostream::out );

    outFile<<"---------------------------------------"<<std::endl;
    outFile<<"JefeCheck Chat Log"<<std::endl;
    outFile<<"Creation Date: "<<asciiTime(false) <<std::endl;
    outFile<<"---------------------------------------"<<std::endl;
    
    std::vector<gfcChatLogEntry>log=client.getChatLog();
    std::vector<gfcChatLogEntry>::iterator iter=log.begin(), end=log.end();
    for ( iter;iter!=end;iter++ ) {
          outFile<<iter->getFormattedString()<<std::endl;
    }
    
    /*for ( int i=0;i<client.getchatLog.size();i++ ) {
        outFile<<chatLog[i]<<std::endl;
    }*/
    
    outFile<<"---------------------------------------"<<std::endl;
    outFile.close();
}

void gfcNetworkManager::sendSystemChatMessage(std::string message, int type)
{
	client.SendChatMessage(message,type);
}

void gfcNetworkManager::sendPointerInfoMessage(gfcNetPointerInfo pointerInfo)
{
	client.SendPointerInfoMessage(pointerInfo);

}



/**
 * Sets the flag for a specific type of event, when the network manager updates and the time
 threshold has passed we will send whatever events are needed.
 * @param networkEvent 
 */
void gfcNetworkManager::notifyEvent(int networkEvent)
{
	if(takeNotifications){
	if(networkEvent<GFCNETEVENT_NUMOFEVENTTYPES)
	{
		events[networkEvent].notified=true;
	}
	}
}

void gfcNetworkManager::setEventSendDelay(int networkEvent, float delay)
{
	if(networkEvent<GFCNETEVENT_NUMOFEVENTTYPES)
	{
		events[networkEvent].delay=delay;
	}
}

void gfcNetworkManager::setTakeNotifications(bool value)
{
	takeNotifications=value;
}



void gfcNetworkManager::sendPlayPauseMessage(gfcNetPlayPauseInfo info)
{
	if(connected && takeNotifications)
		client.SendPlayPauseMessage(info);
}

void gfcNetworkManager::sendFXAddMessage(gfcNetFXAddInfo info)
{
	if(connected && takeNotifications)
	client.SendFXAddMessage(info);
}

void gfcNetworkManager::sendFXStackMessage(gfcNetFXStackMessage message)
{
	if(connected && takeNotifications)
		client.SendFXStackMessage(message);
}

void gfcNetworkManager::sendFXCommonMessage(gfcNetFXCommonInfo info)
{
	if(connected && takeNotifications)
	client.SendFXCommonMessage(info);
}

void gfcNetworkManager::sendFXAttribMessage(gfcNetFXAttribInfo info)
{
	if(connected && takeNotifications)
	    client.SendFXAttribMessage(info);
}

void gfcNetworkManager::sendLayerChange(int quadID, std::string layerName)
{
	if(connected && takeNotifications)
	    client.SendLayerChangeMessage(quadID, layerName);
}

void gfcNetworkManager::queueFXAttrib(const gfcNetFXAttribInfo& info)
{
	if(!connected || !takeNotifications) return;
	// Key per widget so a drag on one param collapses to its latest value
	// while edits to other widgets stay distinct. Flushed in update().
	std::string key = std::to_string(info.id.quadID) + "/" +
	                  std::to_string(info.id.index) + "/" +
	                  info.groupName + "/" + info.variableName;
	pendingFXAttribs_[key] = info;
	notifyEvent(GFCNETEVENT_FX);
}

void gfcNetworkManager::setRecent(std::vector<std::string> recents)
{
	client.setRecent(recents);
}

void gfcNetworkManager::setClientAddress(std::string ip, std::string port)
{
	client.setAddress(ip,port);
}

void gfcNetworkManager::sendPlaylistItem(gfcPlaylistItem item)
{
	if(connected && takeNotifications && sendRemoteLoadRequests)
	    client.SendPlaylistItem(item);
}

void gfcNetworkManager::sendPlaylist(std::string playlist)
{
	if(connected && takeNotifications){
		gfcNetPlaylistMessage tmpMessage;
		tmpMessage.thePlaylist=playlist;
		client.SendPlaylistMessage(tmpMessage);
	}
	
}

void gfcNetworkManager::sendRemotePointerColor(int color){
	if(connected && takeNotifications){
		client.SendRemotePointerColor(color);
	}
}

void gfcNetworkManager::sendPlaylistEvent(gfcNetPlaylistEvent theEvent)
{
	if(connected && takeNotifications){
		client.sendPlaylistEvent(theEvent);
	}
}

void gfcNetworkManager::handleAllReady()
{
	//force the plate manager to redraw.
	plateManager.setChanged();
}

void gfcNetworkManager::handleNewPlayer()
{
	allReady=0;
	plateManager.setChanged();
}

void gfcNetworkManager::handleSincStart()
{
		//force the plate manager to redraw.
		resetSincStatus();
		plateManager.setChanged();
}
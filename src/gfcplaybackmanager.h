// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef GFCPLAYBACKMANAGER_H
#define GFCPLAYBACKMANAGER_H
#include "gfcplaybackgui.h"
#include "gfcNetworkStructures.h"
#include "gfcStructures.h"
#include <chrono>

enum LoopPriorities{GFC_LOOPPRIORITY_SHORTEST,GFC_LOOPPRIORITY_LONGEST,GFC_LOOPPRIORITY_TIMELINE};

/**
Is in charge of keeping track of time, playback status, fps, targetFPS etc. Contains a gfcPlaybackGUI object that connects it's values and action to and from the GUI.

	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcPlaybackManager {
public:
    gfcPlaybackManager();

    ~gfcPlaybackManager();

    // Qt build has no MainWindow global. Constructs the stub
    // gfcPlaybackGUI_Qt and seeds the default frame range, mirroring the
    // Qt branch of the FLTK overload above. Safe to call once at app
    // startup.
    void initializeWidgets();

    void update(); ///this one should run on every cycle, determines timestep, changes the current frame, checks playback boundries etc.
    float getTimestep(); ///gets the timestep for this cycle (from update i-1 to update i).
    int getCurrentFrame();///gets the current frame from the timeline.
    float getCurrentFPS(); ///gets the current playback speed in FPS
    
    gfcNetPlaybackInfo getPlaybackInfo();
    void setPlaybackInfo(gfcNetPlaybackInfo info);
    
	int isPlaying();

    gfcNetPlayPauseInfo getPlayPauseInfo();
    void setPlayPauseInfo(gfcNetPlayPauseInfo info);
    
	void boundCurrentFrameToLimits();
    void setNotifyNetwork(bool value);
    void setTargetFPS();
    void setTargetFPS(float fps);
    /// Exact rational rate (e.g. 24000/1001). Reduces, sets targetFPS and timePerFrame.
    void setTargetRate(int num, int den);
    void getTargetRate(int& num, int& den) const;
    /// While true, a movie load does not replace the player's rate (session restore keeps the session's saved rate).
    void setKeepRateOnLoad(bool keep) { keepRateOnLoad=keep; }
    bool getKeepRateOnLoad() const { return keepRateOnLoad; }
    /// Maps a float fps to a rational: NTSC rates within 0.005 snap to n*1000/1001, otherwise round(fps*1000)/1000 reduced.
    static void rationalFromFPS(float fps, int& num, int& den);
    /// One playback step from `cur` under loop `mode` (LOOPMODEONCE_ID/LOOPMODELOOP_ID/LOOPMODEBOUNCE_ID); BOUNCE flips `direction` at a limit.
    static int advanceFrame(int cur, int& direction, int mode, int startLimit, int endLimit);
    void setPlaybackMode(int mode);
    /// `audible`: when paused, plays this frame's slice of sound (a seek); false for renders.
    void setCurrentFrame(int frame, bool audible=true);
    /// Renders only: moves the frame the plates draw (and its timecode) without
    /// a seek — no cache retarget, no sound, no network message, no timeline
    /// update. The renderer puts the old frame back the same way when done.
    void setRenderFrame(int frame);
    void setDirection(int direction);
    void setToFrame(int frame);
    void setFromFrame(int frame);

	void setInPoint(int frame);
	void setOutPoint(int frame);

    void setLoopPriority(int priority);
    
    int getFromFrame();
    int getToFrame();
    int getInPoint();
    int getOutPoint();
    std::string getTimecodeString();
    float getGUIFrameSize(); ///Kind of ugly design, but better to have it here than have external classes access private members. Returns the class size of a frame in the timeline in pixels, usefull to determine the effect of drag events in the gui with respect to the timeline.
    int getPlaybackMode();
    bool keepTime() const; ///lag policy: true = skip frames to keep time, false = show every frame (slow down). Automatic follows the sound.
    int getLoopPriority();
    float getTargetFPS();
    void startPlayFwd();
    void startPlayRev();
    void pause();
    
    void oneFrameFwd(int num=1);
    void oneFrameRev(int num=1);
    void ffwd();
    void rew();

    void updateToFromFromGUI();
	void updateInOutFromGUI();
    void updateTimelineValueFromGUI();
    void updateCurrentFrameValueFromGUI();
    void updateLoopPriorityFromGUI();
    /// Sends the transport to the frame cache now, changed or not (a fresh
    /// load: its limits are new, and nothing else resends them).
    void cacheResendTransport();
    /// Sends it only when direction, mode, limits or rate changed since the last send.
    void cacheRefreshTransport();
 
      
private: //private made public while debugging

    inline void updateTimestep();
    // Movie sound (audioEngine): the engine's clock leads native-rate forward
    // play; otherwise FPS pacing leads and each forward frame plays its slice.
    void audioSyncTransport();      // player rate + limits/mode, sent when they change
    bool audioTryLead();            // starts leading at currentFrame when the engine can
    void audioStop();               // stops leading and slices
    void audioSeek();               // currentFrame moved by the user
    void recordAdvanceForFPS(int frames);   // measured-FPS readout
    // Pre-play buffer (Keep time only): play starts, or a seek while playing,
    // wait until the next kPrePlaySeconds of frames are resident (at most
    // kPrePlayMaxWait); the sound starts leading when the wait ends.
    static constexpr double kPrePlaySeconds = 0.25;
    static constexpr double kPrePlayMaxWait = 0.5;
    bool buffering_ = false;
    std::chrono::steady_clock::time_point bufferingSince_;
    int prePlayFrames() const;      // ceil(kPrePlaySeconds x targetFPS), at least 1
    // Starts the wait unless the frames are already resident; true while waiting.
    // The engine stops leading for the wait. Called only when keepTime().
    bool beginBuffering();
    // The frame cache's lead skip follows "playing under Keep time, not buffering";
    // the mirror keeps the tick to a plain bool test when nothing changes.
    bool leadSkipOn_ = false;
    void setLeadSkip(bool on);
    // Show every frame held the picture since the last advance: the interval
    // that ends the hold is a real (slow) frame, not a discontinuity.
    bool fpsHeld_ = false;
    // Play (re)started since the last advance: the next interval spans the
    // pause, so it is not a frame interval at all (held or not).
    bool fpsResumed_ = false;
    int audioSentStart=-1, audioSentEnd=-1, audioSentMode=-1, audioSentDir=0;
    // Frame cache (frameCache): the transport (direction, loop mode, limits,
    // rate) is sent when it changes; a jump retargets, a playback step advances.
    void cacheSyncTransport(bool force=false);
    int cacheSentStart=-1, cacheSentEnd=-1, cacheSentMode=-1, cacheSentDir=0;
    float cacheSentFPS=-1.0f;
	gfcTimer timer;
    bool allowNetworkMessages;
    void sendPlayPauseMessage();
    gfcPlaybackGUI* myGUI;
    float targetFPS;
    int targetRateNum=24, targetRateDen=1;
    bool keepRateOnLoad=false;
    int currentFrame;
    int currentDirection;
    double timeStep; ///elapsed time since last update in seconds
    int playing;
    int playbackMode;
    
    //timeline Variables
    int from; //these are the total size of the timeline, the user should not be able to change this.
    int to;

	int inPoint;
	int outPoint;

    double currentFPS;
    int loopPriority; //shortest, longest or timeline
    
	gfcTimer fpsTimer;

    int getEndLimit();
    int getStartLimit();
    
    //auxiliary members used to update
    double timePerFrame; ///time per frame is calculated when targetFPS changes.
    std::string timecodeString;
    
    void updateTimeCode();
    
};

#endif

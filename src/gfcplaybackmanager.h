#ifndef GFCPLAYBACKMANAGER_H
#define GFCPLAYBACKMANAGER_H
#include "gfcplaybackgui.h"
#include "mainWindow.h"
#include "gfcNetworkStructures.h"
#include "gfcStructures.h"

enum LoopPriorities{GFC_LOOPPRIORITY_SHORTEST,GFC_LOOPPRIORITY_LONGEST,GFC_LOOPPRIORITY_TIMELINE};

/**
Is in charge of keeping track of time, playback status, fps, targetFPS etc. Contains a gfcPlaybackGUI object that connects it's values and action to and from the GUI.

	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcPlaybackManager {
public:
    gfcPlaybackManager();

    ~gfcPlaybackManager();

    void initializeWidgets(MainWindow &mw);

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
    void setPlaybackMode(int mode);
    void setCurrentFrame(int frame);
    void setDirection(int direction);
    void setToFrame(int frame);
    void setFromFrame(int frame);

	void setInPoint(int frame);
	void setOutPoint(int frame);

    void setLoopPriority(int priority);
    
    int getFromFrame();
    int getToFrame();
    std::string getTimecodeString();
    float getGUIFrameSize(); ///Kind of ugly design, but better to have it here than have external classes access private members. Returns the class size of a frame in the timeline in pixels, usefull to determine the effect of drag events in the gui with respect to the timeline.
    int getPlaybackMode();
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
 
      
private: //private made public while debugging

    inline void updateTimestep();
	gfcTimer timer;
    bool allowNetworkMessages;
    void sendPlayPauseMessage();
    gfcPlaybackGUI* myGUI;
    float targetFPS;
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

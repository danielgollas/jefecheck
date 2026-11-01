// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfcplaybackmanager.h"
#include <chrono>
#include <cmath>
#include <numeric>
#include "ui/IApplication.h"
namespace { jefe::ui::IApplication& app() { return jefe::ui::IApplication::instance(); } }
#include "qt/gfcplaybackgui_qt.h"

#include <glad/glad.h>


#include "gfctrackmanager.h"
extern gfcTrackManager trackManager;

#include "gfcnetworkmanager.h"
extern gfcNetworkManager networkManager;

#include "gfcplatemanager.h"
extern gfcPlateManager plateManager;

#include "gfcAudioEngine.h"

gfcPlaybackManager::gfcPlaybackManager() {
    myGUI=NULL;
    currentDirection=1;
    playbackMode=LOOPMODELOOP_ID;  // default to looping playback
    loopPriority=GFC_LOOPPRIORITY_SHORTEST;
    allowNetworkMessages=true;
	inPoint=1;
	outPoint=100;
    // Seed the FPS pacing before any GUI exists. Without this targetFPS /
    // timePerFrame are uninitialized: the Qt fps spinbox sets its display
    // value (24) *before* its valueChanged signal is connected, so the
    // engine never receives a starting FPS. A garbage (often ~0) timePerFrame
    // makes `intraFrameCount >= timePerFrame` true every tick, so playback
    // runs at the tick rate (~60fps) instead of the target. Defaulting to
    // 24fps here matches the spinbox default and gives correct pacing from
    // the first frame; the spinbox's valueChanged still overrides it live.
    targetFPS=24.0f;
    timePerFrame=1.0/targetFPS;
	timer.start();
}


gfcPlaybackManager::~gfcPlaybackManager() {
    if (myGUI)
        delete myGUI;
}

void gfcPlaybackManager::initializeWidgets() {
    myGUI=new gfcPlaybackGUI_Qt;
    this->setFromFrame(1);
    this->setToFrame(100);
    this->setCurrentFrame(1);
}

int gfcPlaybackManager::isPlaying()
{
	return playing;
}

void gfcPlaybackManager::update() {
    static double intraFrameCount=0;
    static double fpsCount=0;
    updateTimestep();

    int endLimit=getEndLimit();
    int startLimit=getStartLimit();

    if (!playing) {
        intraFrameCount=0;
        return;
    }

    // Native-rate forward play: the audio clock leads and the picture shows
    // the frame being heard (frames are dropped if drawing falls behind; the
    // sound never waits). Falls back to FPS pacing as soon as the engine can
    // no longer lead (device lost, every native track muted, rate changed).
    if (audioEngine.isLeading()) {
        audioSyncTransport();
        if (audioEngine.canLead()) {
            intraFrameCount=0;
            const int f=audioEngine.leaderFrame();
            currentDirection=audioEngine.leaderDirection();
            if (f!=currentFrame) {
                int moved=std::abs(f-currentFrame);
                if (moved>(endLimit-startLimit)/2) moved=1;   // a loop wrap is one frame
                recordAdvanceForFPS(moved);
                currentFrame=f;
                updateTimeCode();
                plateManager.setChanged();
                boundCurrentFrameToLimits();
            }
            return;
        }
        // Leaving the audio clock: the picture carries on from the frame heard.
        currentFrame=audioEngine.leaderFrame();
        audioEngine.stopLeading();
    }

    intraFrameCount+=timeStep;

    if (intraFrameCount>=timePerFrame) {
        recordAdvanceForFPS(1);

        intraFrameCount-=timePerFrame;

        updateTimeCode();
        plateManager.setChanged();

        // ONCE holds at a limit without counting a frame; every other move counts.
        const int before=currentFrame;
        {
            const bool counted = playbackMode != LOOPMODEONCE_ID ||
                (currentFrame+currentDirection<=endLimit && currentFrame+currentDirection>=startLimit);
            currentFrame = advanceFrame(currentFrame, currentDirection, playbackMode, startLimit, endLimit);
            if (counted) fpsCount++;
        }

        boundCurrentFrameToLimits();

        // Sound follows the picture forward: hand over to the audio clock when
        // a native-rate track can lead (it just loaded, the device opened, a
        // BOUNCE turned forward), otherwise play this frame's slice.
        if (currentDirection>0 && currentFrame!=before && !audioTryLead())
            audioEngine.playSlice(currentFrame);
    }

    // Periodically drain the old window counter so it stays bounded.
    if (fpsCount>=targetFPS*2)
        fpsCount=0;

    //prevent it from trying to "catch up" in case the timestep has been longer that one frame.
    if (intraFrameCount> 2*timePerFrame)
        intraFrameCount=0;
}

// Measured-FPS readout from the wall-clock interval between frame advances
// (`frames` = how many timeline frames this advance moved).
void gfcPlaybackManager::recordAdvanceForFPS(int frames) {
			// Measured-FPS readout (the on-screen "fps:" overlay reads
			// getCurrentFPS()). Derive it from the true wall-clock interval
			// between consecutive frame advances, smoothed with an
			// exponential moving average. This is frame-aligned (no
			// partial-window bias) and the EMA damps the few-ms per-frame
			// OS-timer jitter, so the displayed value sits steady at the
			// target instead of ticking over a fresh 2s window average.
			{
				using dclock = std::chrono::steady_clock;
				static dclock::time_point lastAdvance = dclock::now();
				static dclock::time_point lastDisplay = dclock::now();
				static double emaIntervalMs = -1.0;
				const dclock::time_point nowAdv = dclock::now();
				const double interMs =
					std::chrono::duration<double, std::milli>(nowAdv - lastAdvance).count() / double(frames > 0 ? frames : 1);
				lastAdvance = nowAdv;

				// Ignore discontinuities (first advance, or resuming after a
				// pause/idle gap) so they don't poison the average.
				const double tpfMs = timePerFrame*1000.0;
				if (interMs > 0.0 && interMs < 5.0*tpfMs) {
					if (emaIntervalMs < 0.0) emaIntervalMs = interMs;
					else emaIntervalMs += 0.05*(interMs - emaIntervalMs);
				}

				// The EMA above updates every frame (~24x/s) so it stays
				// smooth, but pushing it to the on-screen readout that often
				// makes the displayed number flicker. Refresh the visible
				// value at a slow, fixed cadence (~2.5 Hz) so it reads as a
				// stable number.
				if (emaIntervalMs > 0.0 &&
					std::chrono::duration<double>(nowAdv - lastDisplay).count() >= 0.4) {
					lastDisplay = nowAdv;
					const double measured = 1000.0/emaIntervalMs;
					// Deadband: when playback is keeping up with the target
					// (within ~2%), report the target exactly. The true
					// cadence dithers ±0.1fps around the target from
					// unavoidable OS-timer/event-loop jitter; without this the
					// readout never settles. A genuine slowdown (heavy decode,
					// dropped frames) falls outside the band and shows the
					// real, smoothed rate — which is what the readout is for.
					double diff = measured - (double)targetFPS;
					if (diff < 0.0) diff = -diff;
					if (targetFPS > 0.0f && diff <= 0.02*(double)targetFPS)
						currentFPS = targetFPS;
					else
						currentFPS = (float)measured;
					myGUI->setCurrentFPS(currentFPS);
				}
			}

}


void gfcPlaybackManager::boundCurrentFrameToLimits()
{
	int endLimit=getEndLimit();
	int startLimit=getStartLimit();

	if (currentFrame>endLimit) {
		currentFrame=endLimit;
		//plateManager.setChanged();

	} else if (currentFrame<startLimit) {
		currentFrame=startLimit;
	}

	if (currentDirection==1) {
		myGUI->setPlayFwdLabel(1);
		myGUI->setPlayRevLabel(0);
	} else {
		myGUI->setPlayFwdLabel(0);
		myGUI->setPlayRevLabel(1);
	}

	myGUI->setTimelineValue(currentFrame);
}

float gfcPlaybackManager::getTimestep() {
    return timeStep;
}

int gfcPlaybackManager::getCurrentFrame() {
    return currentFrame;
}


void gfcPlaybackManager::startPlayFwd() {
    if (playing) {
        if (currentDirection==1) {
            audioStop();
            playing=false;
			fpsTimer.stop();
        } else { //we are playing but not forward, start playing fwd
            //playing=true; //not needed
            currentDirection=1;
            audioTryLead();
        }
    } else { //just start playing fwd
        playing=true;
        currentDirection=1;
		fpsTimer.reset();
        audioTryLead();
    }
    myGUI->setPlayRevLabel(0);
    myGUI->setPlayFwdLabel(playing);
    myGUI->setTimelineValue(currentFrame);
    updateTimeCode();

    sendPlayPauseMessage();
	


}

void gfcPlaybackManager::pause() {
    if (playing) {
        audioStop();
        playing=false;
        myGUI->setPlayRevLabel(0);
        myGUI->setPlayFwdLabel(0);
        myGUI->setTimelineValue(currentFrame);
    } else {
        if (currentDirection==1) {
            startPlayFwd();
        } else {
            startPlayRev();
        }
    }
    updateTimeCode();
    sendPlayPauseMessage();
	
}

void gfcPlaybackManager::startPlayRev() {
    audioStop();   // reverse playback is silent
    if (playing) {
        if (currentDirection==-1) {
            playing=false;
			fpsTimer.stop();
        } else { //we are playing but not Rev, start playing rev
            //playing=true; //not needed
            currentDirection=-1;

        }
    } else { //just start playing rev
        playing=true;
        currentDirection=-1;
		fpsTimer.reset();
    }
    myGUI->setPlayRevLabel(playing);
    myGUI->setPlayFwdLabel(0);
    myGUI->setTimelineValue(currentFrame);
    updateTimeCode();
    sendPlayPauseMessage();
}

void gfcPlaybackManager::oneFrameFwd(int num) {
    //printf("OneFwd\n");
	if (playing)
	{
		pause();
	}
	
    if (currentFrame+num<=to) {
        //advance one frame
        setCurrentFrame(currentFrame+num);
    }
	else
	{
		setCurrentFrame(to);
	}
    updateTimeCode();
	
}

void gfcPlaybackManager::oneFrameRev(int num) {
    //printf("OneBack\n");
	if (playing)
	{
		pause();
	} 
    if (currentFrame-num>=from) {
        //rev one frame
		
        setCurrentFrame(currentFrame-num);
    }
	else
	{
		setCurrentFrame(from);
	}
    updateTimeCode();
	
}

void gfcPlaybackManager::ffwd() {

    setCurrentFrame(getEndLimit());
    updateTimeCode();
	plateManager.setChanged();

}

void gfcPlaybackManager::rew() {
    setCurrentFrame(getStartLimit());
    updateTimeCode();
	plateManager.setChanged();
}

void gfcPlaybackManager::updateTimestep() {
    // Use a high-resolution monotonic clock for the playback timestep.
    // gfcTimer is integer-millisecond (gettimeofday truncated to whole ms),
    // which is fine at a coarse tick but becomes the dominant noise source
    // once the playback timer runs finely (a 4ms tick fed integer-ms deltas
    // of 3/4/5ms — ±12.5% jitter plus a systematic floor undercount). The
    // frame-pacing accumulator needs sub-millisecond deltas to hold a steady
    // FPS, so read steady_clock directly here.
    using clock = std::chrono::steady_clock;
    static clock::time_point tsBaseTime = clock::now();
    const clock::time_point currentTime = clock::now();

    timeStep = std::chrono::duration<double>(currentTime - tsBaseTime).count(); // seconds

    tsBaseTime = currentTime;
}

void gfcPlaybackManager::setPlaybackMode(int mode) {
	plateManager.setChanged();
    this->playbackMode=mode;
    this->myGUI->setPlaybackMode(mode);
    audioSyncTransport();
    networkManager.notifyEvent(GFCNETEVENT_OTHER);
}

void gfcPlaybackManager::setTargetFPS() {
    //this->setTargetFPS(myGUI->getTargetFPS());
    { int n,d; rationalFromFPS(myGUI->getTargetFPS(),n,d); targetRateNum=n; targetRateDen=d; }
    this->targetFPS=float(targetRateNum)/float(targetRateDen);
    this->timePerFrame=double(targetRateDen)/double(targetRateNum);
    audioSyncTransport();
    // printf("TargetFPS: %f (%Lf/frame)",targetFPS,timePerFrame);
    //this->myGUI->setTargetFPS(targetFPS);
    updateTimeCode();
    networkManager.notifyEvent(GFCNETEVENT_OTHER);
}

void gfcPlaybackManager::setTargetFPS(float fps) {
    if (fps>0.0f) {
        int n,d;
        rationalFromFPS(fps,n,d);
        targetRateNum=n; targetRateDen=d;
        this->targetFPS=float(n)/float(d);
        this->timePerFrame=double(d)/double(n);
        audioSyncTransport();
    } else {
        this->targetFPS=fps;
        this->timePerFrame=1.0/targetFPS;
    }
    this->myGUI->setTargetFPS(targetFPS);
    updateTimeCode();
    networkManager.notifyEvent(GFCNETEVENT_OTHER);
}

void gfcPlaybackManager::setTargetRate(int num, int den) {
    if (num<=0 || den<=0) return;
    const int g=std::gcd(num,den);
    targetRateNum=num/g; targetRateDen=den/g;
    this->targetFPS=float(targetRateNum)/float(targetRateDen);
    this->timePerFrame=double(targetRateDen)/double(targetRateNum);
    audioSyncTransport();
    if (myGUI) this->myGUI->setTargetFPS(targetFPS);
    updateTimeCode();
    networkManager.notifyEvent(GFCNETEVENT_OTHER);
}

void gfcPlaybackManager::getTargetRate(int& num, int& den) const {
    num=targetRateNum; den=targetRateDen;
}

void gfcPlaybackManager::rationalFromFPS(float fps, int& num, int& den) {
    static const int ntsc[]={24,30,48,60};
    for (int base : ntsc) {
        const double v=double(base)*1000.0/1001.0;
        if (std::fabs(double(fps)-v)<0.005) { num=base*1000; den=1001; return; }
    }
    long long n=std::llround(double(fps)*1000.0);
    if (n<=0) { num=24; den=1; return; }
    long long d=1000;
    const long long g=std::gcd(n,d);
    num=int(n/g); den=int(d/g);
}

int gfcPlaybackManager::getEndLimit() {

    //get tracks shortest or longest track depending on the loopPriority setting (TODO: remove from the prefs window and put it in the main window controls)
    int tmpEndLimit=1;
    switch ( loopPriority ) {
    case GFC_LOOPPRIORITY_SHORTEST:
        tmpEndLimit=trackManager.getFirstLastLoaded()+1; //this includes the offset for each track, so we get the frame number of the absolutely shortest track on the window. This also obviates tracks with 0 lenght and will only return 0 if the track is in fact ending at frame 0 due to an offset, but should not happen often
        break;

    case GFC_LOOPPRIORITY_LONGEST:
        tmpEndLimit=trackManager.getLastLastLoaded()+1; //the same as above but returns the absolutely farthest ending track.
        break;

    case GFC_LOOPPRIORITY_TIMELINE:
        //tmpEndLimit=to;
		tmpEndLimit=outPoint;
        break;
    }

    if (tmpEndLimit<outPoint)//(tmpEndLimit<to) // timeline has priority no matter what, you can never see beyond it, only within it.
        return tmpEndLimit;
    else
        return outPoint;//return to;
}

int gfcPlaybackManager::getStartLimit() {
    int tmpStartLimit=1;
    switch ( loopPriority ) {
    case GFC_LOOPPRIORITY_SHORTEST:
        tmpStartLimit=trackManager.getLastFirstLoaded()+1; //this includes the offset for each track, so we get the frame number of the absolutely shortest track on the window. This also obviates tracks with 0 lenght and will only return 0 if the track is in fact ending at frame 0 due to an offset, but should not happen often  They are always off by 1 since the ranges start at 0
        break;

    case GFC_LOOPPRIORITY_LONGEST:
        tmpStartLimit=trackManager.getFirstFirstLoaded()+1; //the same as above but returns the absolutely farthest ending track.
        break;

    case GFC_LOOPPRIORITY_TIMELINE:
        //tmpStartLimit=from;
		tmpStartLimit=inPoint;
        break;
    }

    if (tmpStartLimit>inPoint)//(tmpStartLimit>from) // timeline has priority no matter what, you can never see beyond it, only within it.
        return tmpStartLimit;
    else
        return inPoint;//return from;
}

void gfcPlaybackManager::setCurrentFrame(int frame, bool audible) {
	
    this->currentFrame=frame;
    this->myGUI->setTimelineValue(frame);
    this->myGUI->setCurrentFrame(frame);
    if (audible) audioSeek();
    updateTimeCode();
    sendPlayPauseMessage();
	plateManager.setChanged();
}



void gfcPlaybackManager::setOutPoint(int frame)
{
	outPoint=frame;
	if (outPoint<=0){
		outPoint=1;
	}
	if (inPoint>outPoint) {
		setInPoint(outPoint);
	}
	// Note: the Qt timeline maps the [in, out] range to the full widget
	// width (in/out are the zoom control), so out is free to exceed the
	// total `to` without growing it — the view just zooms. We deliberately
	// do NOT grow `to` here.

	myGUI->setTimelineOut(outPoint);
	myGUI->setOutPoint(outPoint);
	audioSyncTransport();
	updateTimeCode();
	networkManager.notifyEvent(GFCNETEVENT_OTHER);
	//trackManager.updateTrackWidgetsFromAndTo(from,to);
	//boundCurrentFrameToLimits();
	plateManager.setChanged();
}

void gfcPlaybackManager::setToFrame(int frame) {
    to=frame;
	if (to<=0){
        to=1;
	}
    if (from>to) {
        setFromFrame(to);
    }
    myGUI->setTo(to);
    myGUI->setTimelineLimits(from, to);
    updateTimeCode();
    networkManager.notifyEvent(GFCNETEVENT_OTHER);
    trackManager.updateTrackWidgetsFromAndTo(from,to);
	boundCurrentFrameToLimits();
	plateManager.setChanged();
}

void gfcPlaybackManager::setFromFrame(int frame) {
    from=frame;
	if (from<=0){
        from=1;
		
	}
    if (from>to) {
        setToFrame(from);
		
    }
    myGUI->setFrom(from);
    myGUI->setTimelineLimits(from, to);
    updateTimeCode();
    networkManager.notifyEvent(GFCNETEVENT_OTHER);
    trackManager.updateTrackWidgetsFromAndTo(from,to);
	boundCurrentFrameToLimits();
	plateManager.setChanged();
}

int gfcPlaybackManager::getInPoint() {
    return inPoint;
}

int gfcPlaybackManager::getOutPoint() {
    return outPoint;
}

void gfcPlaybackManager::setInPoint(int frame)
{
	inPoint=frame;
	if (inPoint<=0){
		inPoint=1;
	}
	if (inPoint>outPoint) {
		setOutPoint(inPoint);
	}
	myGUI->setTimelineIn(inPoint);
	myGUI->setInPoint(inPoint);
	audioSyncTransport();
	updateTimeCode();
	networkManager.notifyEvent(GFCNETEVENT_OTHER);
	//trackManager.updateTrackWidgetsFromAndTo(from,to);
	//boundCurrentFrameToLimits();
	plateManager.setChanged();
}

void gfcPlaybackManager::updateToFromFromGUI() {
    //set internal members to values from GUI
    setFromFrame(myGUI->getFrom());
    setToFrame(myGUI->getTo());
    updateTimeCode();
	plateManager.setChanged();


}

void gfcPlaybackManager::updateInOutFromGUI() {
	//set internal members to values from GUI
	
	setInPoint(myGUI->getInPoint());
	setOutPoint(myGUI->getOutPoint());
	updateTimeCode();
	plateManager.setChanged();
}

void gfcPlaybackManager::setDirection(int direction) {
    const bool turned = playing && direction!=currentDirection;
    if (turned) audioStop();
    currentDirection=direction;
    if (turned && direction>0) audioTryLead();
    myGUI->setPlayRevLabel(playing && direction==1);
    myGUI->setPlayFwdLabel(playing && direction==-1);
    sendPlayPauseMessage();
	

}

void gfcPlaybackManager::updateTimelineValueFromGUI() {
    currentFrame=myGUI->getTimeLineValue();
	
    myGUI->setCurrentFrame(currentFrame);
    audioSeek();

	setInPoint(myGUI->getTimelineInPointValue());
	setOutPoint(myGUI->getTimelineOutPointValue());

    updateTimeCode();
    sendPlayPauseMessage();
	plateManager.setChanged();
}

void gfcPlaybackManager::updateCurrentFrameValueFromGUI() {
    currentFrame=myGUI->getCurrentFrame();
    myGUI->setTimelineValue(currentFrame);
    audioSeek();
    updateTimeCode();
}

int gfcPlaybackManager::getFromFrame() {
    return from;
}

int gfcPlaybackManager::getToFrame() {
    return to;
}

float gfcPlaybackManager::getGUIFrameSize() {
    return myGUI->getFrameSize();
}

std::string gfcPlaybackManager::getTimecodeString() {
    return timecodeString;
}

void gfcPlaybackManager::updateTimeCode() {

	timecodeString="00:00:00:00";
    int hours, minutes, seconds, frms;
    char timeCode[60]="";

    hours=0;
    minutes=0;
    seconds=0;
    if ( ((int)targetFPS)!=0 ) {
		//printf("targetFPS: %i\n",(int)targetFPS);
        frms=(currentFrame)%(int)targetFPS;
        seconds= ( currentFrame/ (int)( targetFPS ) ) %60;
        minutes= ( currentFrame/ (int)( targetFPS*60 ) ) %60;
        hours= ( currentFrame/ (int)( targetFPS*3600 ) );
    }
    sprintf ( timeCode,"%02i:%02i:%02i:%02i",hours, minutes, seconds, frms );
    timecodeString=timeCode;
    myGUI->setSMPT(timecodeString);

}

float gfcPlaybackManager::getCurrentFPS() {
    return currentFPS;
}



int gfcPlaybackManager::getPlaybackMode() {
    return playbackMode;
}

void gfcPlaybackManager::updateLoopPriorityFromGUI() {
    this->setLoopPriority(myGUI->getLoopPriority());
}
gfcNetPlaybackInfo gfcPlaybackManager::getPlaybackInfo() {
    gfcNetPlaybackInfo result;
    result.from=from;
    result.to=to;
    result.targetFPS=targetFPS;
    result.playbackMode=this->playbackMode;
    result.loopPriority=this->loopPriority;
	result.inPoint=this->inPoint;
	result.outPoint=this->outPoint;
    return result;

}

void gfcPlaybackManager::setPlaybackInfo(gfcNetPlaybackInfo info) {
    this->setFromFrame(info.from);
    this->setToFrame(info.to);
    this->setTargetFPS(info.targetFPS);
    this->setPlaybackMode(info.playbackMode);
    this->setLoopPriority(info.loopPriority);
	this->setInPoint(info.inPoint);
	this->setOutPoint(info.outPoint);
}


void gfcPlaybackManager::setLoopPriority(int priority) {
    this->loopPriority=priority;
    myGUI->setLoopPriority(priority);
    networkManager.notifyEvent(GFCNETEVENT_OTHER);
}


gfcNetPlayPauseInfo gfcPlaybackManager::getPlayPauseInfo() {
    gfcNetPlayPauseInfo result;
    result.play=playing;
    result.direction=currentDirection;
    result.frame=currentFrame;
    return result;
}

void gfcPlaybackManager::setPlayPauseInfo(gfcNetPlayPauseInfo info) {
    //printf("Setting playpause info play=%i direction=%i frame=%i\n",info.play, info.direction, info.frame);
    allowNetworkMessages=false; //prevent loops by not sending network messages when setting from a message...

    setCurrentFrame(info.frame);
    setDirection(info.direction);
    playing=!info.play; //by setting it to the opposite, pause will set it to the correct but also take the appropiate measures GUI and playbackwise.
    pause();

    allowNetworkMessages=true;
}

void gfcPlaybackManager::sendPlayPauseMessage() {
    if (allowNetworkMessages)
        networkManager.sendPlayPauseMessage(getPlayPauseInfo());
}


int gfcPlaybackManager::getLoopPriority()
{
	return loopPriority;
}

float gfcPlaybackManager::getTargetFPS()
{
	return targetFPS;
}

// The frame after `cur` for one playback step. BOUNCE flips `direction` at a
// limit; ONCE clamps to the limits and holds. Shared by update() and the audio
// engine's leading clock, so both walk frames identically.
int gfcPlaybackManager::advanceFrame(int cur, int& direction, int mode, int startLimit, int endLimit) {
    switch ( mode ) {
    case LOOPMODEONCE_ID: {
        if (cur+direction<=endLimit && cur+direction>=startLimit) {
            //advance a frame in the direction
            cur+=direction;
        } else {
            if (cur<=startLimit) {
                cur=startLimit;

            } else if (cur>=endLimit) {
                cur=endLimit;
            }
        }
    }
    break;
    case LOOPMODELOOP_ID: {
        if (cur+direction<=endLimit && cur+direction>=startLimit) {
            cur+=direction;
        } else {
            if (cur>=endLimit) {
                cur=startLimit;
            } else {
                if (cur<=startLimit) {
                    cur=endLimit;
                }
            }
        }
    }
    break;
    case LOOPMODEBOUNCE_ID: {
        if (cur+direction<=endLimit && cur+direction>=startLimit) {
            cur+=direction;
        } else {
            direction=-direction;
            cur+=direction;
        }
    }
    break;
    }
    return cur;
}

// ---- movie sound ----

// Sends the player rate and the loop limits/mode to the audio engine when they
// change (getStartLimit/getEndLimit move while tracks load). While the engine
// leads it takes only the limits and mode; its walk keeps its own frame.
void gfcPlaybackManager::audioSyncTransport() {
    audioEngine.setPlayerRate(targetRateNum, targetRateDen);
    const int s0=getStartLimit(), e0=getEndLimit();
    if (s0==audioSentStart && e0==audioSentEnd && playbackMode==audioSentMode && currentDirection==audioSentDir) return;
    audioSentStart=s0; audioSentEnd=e0; audioSentMode=playbackMode; audioSentDir=currentDirection;
    audioEngine.updateTransport(gfcAudioTransport{currentFrame, s0, e0, playbackMode, currentDirection});
}

bool gfcPlaybackManager::audioTryLead() {
    audioEngine.setPlayerRate(targetRateNum, targetRateDen);
    const gfcAudioTransport t{currentFrame, getStartLimit(), getEndLimit(), playbackMode, currentDirection};
    audioSentStart=t.startLimit; audioSentEnd=t.endLimit; audioSentMode=t.loopMode; audioSentDir=t.direction;
    if (currentDirection<=0) { audioEngine.updateTransport(t); return false; }
    audioEngine.startLeading(t);   // a no-op unless the engine can lead
    return audioEngine.isLeading();
}

void gfcPlaybackManager::audioStop() {
    if (audioEngine.isLeading()) currentFrame=audioEngine.leaderFrame();   // stop on the frame heard
    audioEngine.stopLeading();
    audioEngine.stopSlices();
}

// The user moved the playhead: paused, play that frame's slice (step, seek,
// scrub; a newer request replaces one still pending). Playing, restart the
// audio clock from the new frame so it does not pull the picture back.
void gfcPlaybackManager::audioSeek() {
    if (!playing) {
        audioEngine.setPlayerRate(targetRateNum, targetRateDen);
        audioEngine.playSlice(currentFrame);
    } else if (currentDirection>0 && audioEngine.isLeading()) {
        audioTryLead();
    }
}

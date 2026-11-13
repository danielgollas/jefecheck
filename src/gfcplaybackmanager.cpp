// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfcplaybackmanager.h"
#include <algorithm>
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
#include "gfcFrameCache.h"

gfcPlaybackManager::gfcPlaybackManager() {
    myGUI=NULL;
    currentDirection=1;
    playbackMode=LOOPMODELOOP_ID;  // default to looping playback
    loopPriority=GFC_LOOPPRIORITY_SHORTEST;
    allowNetworkMessages=true;
	inPoint=1;
	outPoint=100;
    // Seed the FPS pacing before any GUI exists: the Qt fps spinbox sets 24 before
    // valueChanged is connected, so an uninitialized timePerFrame would play at tick rate.
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
        buffering_=false;
        fpsHeld_=false;
        fpsResumed_=true;
        if (leadSkipOn_) setLeadSkip(false);
        return;
    }

    // Pre-play buffer (Keep time): hold the playhead until the next ~0.25 s is resident or 0.5 s
    // has passed, then let the sound lead.
    if (buffering_) {
        const double waited=std::chrono::duration<double>(std::chrono::steady_clock::now()-bufferingSince_).count();
        if (waited>=kPrePlayMaxWait || trackManager.timelineFramesResident(currentFrame, prePlayFrames(), currentDirection)) {
            buffering_=false;
            if (currentDirection>0) audioTryLead();
        } else {
            intraFrameCount=0;
            return;
        }
    }

    // The frame cache follows each step (and a BOUNCE turn): one atomic store
    // and a wake, no lock, no table walk.
    const int frame0=currentFrame, dir0=currentDirection;
    auto cacheFollow=[&] {
        if (currentFrame==frame0 && currentDirection==dir0) return;
        frameCache.advance(currentFrame, currentDirection);
        cacheSentDir=currentDirection;
    };

    // Native-rate forward play: the audio clock leads. Keep time drops frames to follow it and the sound
    // never waits. Show every frame follows it one frame at a time while each next frame is ready; a frame
    // it would skip or one still loading stops the sound, and pacing holds the picture.
    if (audioEngine.isLeading()) {
        audioSyncTransport();
        if (audioEngine.canLead() && !keepTime()) {
            const int f=audioEngine.leaderFrame();
            int dir=audioEngine.leaderDirection();
            const int next=advanceFrame(currentFrame, dir, playbackMode, startLimit, endLimit);
            if (f==currentFrame || (f==next && trackManager.timelineFrameResident(next))) {
                intraFrameCount=0;
                if (f!=currentFrame) {
                    recordAdvanceForFPS(1);
                    currentFrame=f;
                    currentDirection=dir;
                    updateTimeCode();
                    plateManager.setChanged();
                    boundCurrentFrameToLimits();
                }
                cacheFollow();
                return;
            }
            // The picture stays on the frame shown; pacing looks at the next one this tick.
            audioEngine.stopLeading();
            intraFrameCount=timePerFrame;
        } else if (audioEngine.canLead() && keepTime()) {
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
            cacheFollow();
            return;
        } else {
            // Leaving the audio clock: the picture carries on from the frame heard.
            currentFrame=audioEngine.leaderFrame();
            audioEngine.stopLeading();
        }
    }
    // Pacing leads from here. Keep time still skips ahead whichever clock leads (the cache walk
    // follows the direction); Show every frame never skips. Atomics only.
    {
        const bool skip=keepTime();
        if (leadSkipOn_!=skip) setLeadSkip(skip);
    }

    intraFrameCount+=timeStep;

    if (intraFrameCount>=timePerFrame) {
        // Show every frame: the next frame isn't resident, so hold this one and look again next tick.
        // Only a tick that would advance pays the check.
        if (!keepTime()) {
            int dirCopy=currentDirection;
            const int next=advanceFrame(currentFrame, dirCopy, playbackMode, startLimit, endLimit);
            if (next!=currentFrame && !trackManager.timelineFrameResident(next)) {
                intraFrameCount=timePerFrame;
                fpsHeld_=true;
                return;
            }
        }

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

        // Sound follows the picture forward: hand over to the audio clock when a native-rate track can
        // lead, otherwise play this frame's slice.
        if (currentDirection>0 && currentFrame!=before && !audioTryLead())
            audioEngine.playSlice(currentFrame);
    }

    // Periodically drain the old window counter so it stays bounded.
    if (fpsCount>=targetFPS*2)
        fpsCount=0;

    //prevent it from trying to "catch up" in case the timestep has been longer that one frame.
    if (intraFrameCount> 2*timePerFrame)
        intraFrameCount=0;

    cacheFollow();
}

// Measured-FPS readout from the wall-clock interval between frame advances
// (`frames` = how many timeline frames this advance moved).
void gfcPlaybackManager::recordAdvanceForFPS(int frames) {
			// Measured FPS for the overlay: an EMA of the wall-clock interval between
			// frame advances, so OS-timer jitter doesn't make the readout tick.
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
				// An interval that ends a Show-every-frame hold is a real slow
				// frame (up to 5 s), not a resume after a pause.
				const double tpfMs = timePerFrame*1000.0;
				const bool held = fpsHeld_, resumed = fpsResumed_;
				fpsHeld_ = false;
				fpsResumed_ = false;
				if (!resumed && interMs > 0.0 && (interMs < 5.0*tpfMs || (held && interMs < 5000.0))) {
					if (emaIntervalMs < 0.0) emaIntervalMs = interMs;
					else emaIntervalMs += 0.05*(interMs - emaIntervalMs);
				}

				// The EMA updates every frame; refresh the visible readout at ~2.5 Hz so it
				// does not flicker.
				if (emaIntervalMs > 0.0 &&
					std::chrono::duration<double>(nowAdv - lastDisplay).count() >= 0.4) {
					lastDisplay = nowAdv;
					const double measured = 1000.0/emaIntervalMs;
					// Deadband: within ~2% of the target, report the target (OS-timer jitter keeps
					// the readout from settling); a real slowdown shows the smoothed rate.
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
        fpsResumed_=true;
        // Keep time waits for the pre-play buffer; the sound leads when it ends.
        if (!(keepTime() && beginBuffering()))
            audioTryLead();
    }
    myGUI->setPlayRevLabel(0);
    myGUI->setPlayFwdLabel(playing);
    myGUI->setTimelineValue(currentFrame);
    cacheSyncTransport();
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
        fpsResumed_=true;
        if (keepTime()) beginBuffering();
    }
    myGUI->setPlayRevLabel(playing);
    myGUI->setPlayFwdLabel(0);
    myGUI->setTimelineValue(currentFrame);
    cacheSyncTransport();
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
    // Use steady_clock, not gfcTimer: gfcTimer is integer-millisecond, and the
    // frame-pacing accumulator needs sub-millisecond deltas to hold a steady FPS.
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
    cacheSyncTransport();
    networkManager.notifyEvent(GFCNETEVENT_OTHER);
}

void gfcPlaybackManager::setTargetFPS() {
    //this->setTargetFPS(myGUI->getTargetFPS());
    { int n,d; rationalFromFPS(myGUI->getTargetFPS(),n,d); targetRateNum=n; targetRateDen=d; }
    this->targetFPS=float(targetRateNum)/float(targetRateDen);
    this->timePerFrame=double(targetRateDen)/double(targetRateNum);
    audioSyncTransport();
    cacheSyncTransport();
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
    cacheSyncTransport();
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
    cacheSyncTransport();
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

void gfcPlaybackManager::setRenderFrame(int frame) {
    this->currentFrame=frame;
    updateTimeCode();
}

void gfcPlaybackManager::setCurrentFrame(int frame, bool audible) {
	
    this->currentFrame=frame;
    // A jump: the frame cache's window moves here (new generation, no reload).
    cacheSyncTransport();
    frameCache.retarget(frame);
    trackManager.retargetAudio(frame);   // and the sound fills from here first
    this->myGUI->setTimelineValue(frame);
    this->myGUI->setCurrentFrame(frame);
    // A seek while playing (Keep time) waits for the pre-play buffer; the
    // sound restarts from here when it ends.
    const bool waiting = playing && keepTime() && beginBuffering();
    if (audible && !waiting) audioSeek();
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
	// The Qt timeline maps [in, out] to the full widget width (in/out are the
	// zoom control), so out may exceed the total `to` without growing it.

	myGUI->setTimelineOut(outPoint);
	myGUI->setOutPoint(outPoint);
	audioSyncTransport();
	cacheSyncTransport();
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
	cacheSyncTransport();
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
	cacheSyncTransport();
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
	cacheSyncTransport();
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
    cacheSyncTransport();
    if (turned && direction>0) audioTryLead();
    myGUI->setPlayRevLabel(playing && direction==1);
    myGUI->setPlayFwdLabel(playing && direction==-1);
    sendPlayPauseMessage();
	

}

void gfcPlaybackManager::updateTimelineValueFromGUI() {
    currentFrame=myGUI->getTimeLineValue();
    frameCache.retarget(currentFrame);   // a timeline click is a jump
    trackManager.retargetAudio(currentFrame);   // and the sound fills from here first
	
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
    frameCache.retarget(currentFrame);   // a typed frame is a jump
    trackManager.retargetAudio(currentFrame);   // and the sound fills from here first
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
    cacheSyncTransport();   // the limits follow the loop priority
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

// The frame after `cur` for one step (BOUNCE flips `direction`, ONCE clamps and holds). Shared by
// update() and the audio engine's clock, so both walk frames identically.
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

// Sends the rate and loop limits/mode to the audio engine when they change (the limits move
// while tracks load). While the engine leads it takes only the limits and mode.
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
    // The picture is waiting for the pre-play buffer: its end starts the lead.
    if (buffering_) { audioEngine.updateTransport(t); return false; }
    // Reverse is silent. Show every frame lets the sound lead only once the next
    // ~0.25 s is resident; until then each shown frame plays its own slice.
    if (currentDirection<=0) { audioEngine.updateTransport(t); return false; }
    if (!keepTime() && !trackManager.timelineFramesResident(currentFrame, prePlayFrames(), currentDirection)) {
        audioEngine.updateTransport(t);
        return false;
    }
    audioEngine.startLeading(t);   // a no-op unless the engine can lead
    const bool leading=audioEngine.isLeading();
    // Not buffering (checked above): with Keep time the lead skip is on whether
    // or not the sound took the lead; Show every frame never skips.
    setLeadSkip(playing && keepTime());
    return leading;
}

void gfcPlaybackManager::setLeadSkip(bool on) {
    leadSkipOn_=on;
    frameCache.setLeadSkip(on);
}

void gfcPlaybackManager::audioStop() {
    if (audioEngine.isLeading() && audioEngine.leaderFrame()!=currentFrame) {
        currentFrame=audioEngine.leaderFrame();   // stop on the frame heard
        frameCache.advance(currentFrame);
    }
    audioEngine.stopLeading();
    audioEngine.stopSlices();
    setLeadSkip(false);
}

// The user moved the playhead. Paused: play that frame's slice (a newer request replaces one
// pending). Playing: restart the audio clock there so it doesn't pull the picture back.
void gfcPlaybackManager::audioSeek() {
    if (!playing) {
        audioEngine.setPlayerRate(targetRateNum, targetRateDen);
        audioEngine.playSlice(currentFrame);
    } else if (currentDirection>0) {
        audioTryLead();   // also resumes a lead a pre-play wait stopped
    }
}

int gfcPlaybackManager::prePlayFrames() const {
    return std::max(1, int(std::ceil(kPrePlaySeconds*double(targetFPS))));
}

bool gfcPlaybackManager::beginBuffering() {
    if (trackManager.timelineFramesResident(currentFrame, prePlayFrames(), currentDirection)) {
        buffering_=false;
        return false;
    }
    buffering_=true;
    bufferingSince_=std::chrono::steady_clock::now();
    audioEngine.stopLeading();   // silent while waiting
    setLeadSkip(false);
    return true;
}

// ---- frame cache ----

// Sends the transport to the frame cache when direction/mode/limits/rate changed (the limits move
// as tracks load). The playhead itself travels by retarget() and advance(), which take no lock.
void gfcPlaybackManager::cacheSyncTransport(bool force) {
    const int s0=getStartLimit(), e0=getEndLimit();
    if (!force && s0==cacheSentStart && e0==cacheSentEnd && playbackMode==cacheSentMode &&
        currentDirection==cacheSentDir && targetFPS==cacheSentFPS)
        return;
    cacheSentStart=s0; cacheSentEnd=e0; cacheSentMode=playbackMode; cacheSentDir=currentDirection; cacheSentFPS=targetFPS;
    frameCache.setTransport(currentFrame, currentDirection, playbackMode, s0, e0, targetFPS);
}

void gfcPlaybackManager::cacheResendTransport() {
    cacheSyncTransport(true);
}

void gfcPlaybackManager::cacheRefreshTransport() {
    cacheSyncTransport(false);
}

bool gfcPlaybackManager::keepTime() const
{
    return sett.lagPolicy==1 || (sett.lagPolicy==0 && trackManager.anyTrackHasAudio());
}

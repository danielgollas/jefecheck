#include "gfcplaybackmanager.h"
#include <chrono>
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
    static double fpsTimerCount=0;
	

    static double fpsCount=0;
    updateTimestep();

    int endLimit=getEndLimit();
    int startLimit=getStartLimit();

    if (!playing) { //TODO: sleep should not be handled here, figure a better way, probably timing if there hasn't been a callback in a while then go to sleep.
/*

#ifdef WIN32
                    app().waitForEvents(0.001);
#endif

#ifdef __APPLE__
                    //on mac we have to sleep a whole lot apparently
                    app().waitForEvents(0.02);
#endif

#ifdef linux
                    app().waitForEvents(0.0001);
#endif*/
        intraFrameCount=0;
		fpsTimerCount=0;

		

    } else {
        intraFrameCount+=timeStep;
        fpsTimerCount+=timeStep;
				
		

        if (intraFrameCount>=timePerFrame) {

			// Measured-FPS readout (the "fps:" overlay reads getCurrentFPS()): an EMA of
			// the wall-clock interval between frame advances.
			{
				using dclock = std::chrono::steady_clock;
				static dclock::time_point lastAdvance = dclock::now();
				static dclock::time_point lastDisplay = dclock::now();
				static double emaIntervalMs = -1.0;
				const dclock::time_point nowAdv = dclock::now();
				const double interMs =
					std::chrono::duration<double, std::milli>(nowAdv - lastAdvance).count();
				lastAdvance = nowAdv;

				// Ignore discontinuities (first advance, or resuming after a
				// pause/idle gap) so they don't poison the average.
				const double tpfMs = timePerFrame*1000.0;
				if (interMs > 0.0 && interMs < 5.0*tpfMs) {
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

            intraFrameCount-=timePerFrame;

			updateTimeCode();
			plateManager.setChanged();
			

            switch ( playbackMode ) {
            case LOOPMODEONCE_ID: {
                if (currentFrame+currentDirection<=endLimit && currentFrame+currentDirection>=startLimit) {
                    //advance a frame in the direction
                    currentFrame+=currentDirection;
                    fpsCount++;
                } else {
                    if (currentFrame<=startLimit) {
                        currentFrame=startLimit;

                    } else if (currentFrame>=endLimit) {
                        currentFrame=endLimit;
                    }
                }
            }
            break;
            case LOOPMODELOOP_ID: {
                if (currentFrame+currentDirection<=endLimit && currentFrame+currentDirection>=startLimit) {
                    currentFrame+=currentDirection;
                    fpsCount++;
                } else {
                    if (currentFrame>=endLimit) {
                        currentFrame=startLimit;
                        fpsCount++;
                    } else {
                        if (currentFrame<=startLimit) {
                            currentFrame=endLimit;
                            fpsCount++;
                        }
                    }
                }
            }
            break;
            case LOOPMODEBOUNCE_ID: {
                if (currentFrame+currentDirection<=endLimit && currentFrame+currentDirection>=startLimit) {
                    currentFrame+=currentDirection;
                    fpsCount++;
                } else {
                    currentDirection=-currentDirection;
                    currentFrame+=currentDirection;
                    fpsCount++;
                }
            }
            break;
            }

           			
			boundCurrentFrameToLimits();
			
		/*	#ifdef WIN32
                    app().waitForEvents(0.001);
#endif

#ifdef __APPLE__
                    //on mac we have to sleep a whole lot apparently
                    app().waitForEvents(0.002);
#endif

#ifdef linux
                    app().waitForEvents(0.001);
#endif*/
        }
		
		
		
		// currentFPS is maintained by the EMA at the frame-advance point above;
		// drain the old window counters so they stay bounded.
		if (fpsCount>=targetFPS*2)
		{
			fpsTimerCount=0;
			fpsCount=0;
		}
		
		/*if (fpsTimerCount>=1.0) 
		{
			char tmpNum[6];
			sprintf(tmpNum,"%.5f",fpsCount/fpsTimerCount);
			currentFPS=atof(tmpNum);
			//currentFPS=timeStep;
			myGUI->setCurrentFPS(currentFPS);
			fpsTimerCount=0;
			fpsCount=0;
		}*/
		/*double elapsed=fpsTimer.getElapsedSecs(true);
		//
		if (elapsed>=1.0)
		{
		currentFPS=currentFPS=atof(tmpNum);
		//currentFPS=elapsed;
		printf("%i frames: %Lfs=%f\n",fpsCount,elapsed,currentFPS);
		fpsTimer.reset();
		fpsCount=0;
		myGUI->setCurrentFPS(currentFPS);
		}*/
		
		//prevent it from trying to "catch up" in case the timestep has been longer that one frame.
		if (intraFrameCount> 2*timePerFrame)
            intraFrameCount=0;

		

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
            playing=false;
			fpsTimer.stop();
        } else { //we are playing but not forward, start playing fwd
            //playing=true; //not needed
            currentDirection=1;
        }
    } else { //just start playing fwd
        playing=true;
        currentDirection=1;
		fpsTimer.reset();

    }
    myGUI->setPlayRevLabel(0);
    myGUI->setPlayFwdLabel(playing);
    myGUI->setTimelineValue(currentFrame);
    updateTimeCode();

    sendPlayPauseMessage();
	


}

void gfcPlaybackManager::pause() {
    if (playing) {
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
    networkManager.notifyEvent(GFCNETEVENT_OTHER);
}

void gfcPlaybackManager::setTargetFPS() {
    //this->setTargetFPS(myGUI->getTargetFPS());
    this->targetFPS=myGUI->getTargetFPS();
    this->timePerFrame=1.0/targetFPS;
    // printf("TargetFPS: %f (%Lf/frame)",targetFPS,timePerFrame);
    //this->myGUI->setTargetFPS(targetFPS);
    updateTimeCode();
    networkManager.notifyEvent(GFCNETEVENT_OTHER);
}

void gfcPlaybackManager::setTargetFPS(float fps) {
    this->targetFPS=fps;
    this->timePerFrame=1.0/targetFPS;
    this->myGUI->setTargetFPS(targetFPS);
    updateTimeCode();
    networkManager.notifyEvent(GFCNETEVENT_OTHER);
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

void gfcPlaybackManager::setCurrentFrame(int frame) {
	
    this->currentFrame=frame;
    this->myGUI->setTimelineValue(frame);
    this->myGUI->setCurrentFrame(frame);
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
    currentDirection=direction;
    myGUI->setPlayRevLabel(playing && direction==1);
    myGUI->setPlayFwdLabel(playing && direction==-1);
    sendPlayPauseMessage();
	

}

void gfcPlaybackManager::updateTimelineValueFromGUI() {
    currentFrame=myGUI->getTimeLineValue();
	
    myGUI->setCurrentFrame(currentFrame);

	setInPoint(myGUI->getTimelineInPointValue());
	setOutPoint(myGUI->getTimelineOutPointValue());

    updateTimeCode();
    sendPlayPauseMessage();
	plateManager.setChanged();
}

void gfcPlaybackManager::updateCurrentFrameValueFromGUI() {
    currentFrame=myGUI->getCurrentFrame();
    myGUI->setTimelineValue(currentFrame);
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
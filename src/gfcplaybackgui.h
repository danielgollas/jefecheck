#ifndef GFCPLAYBACKGUI_H
#define GFCPLAYBACKGUI_H

#include <string>
/**
Abstract class that is used to subclass to specific PlaybackGUI implementations. Particularly PlaybackGUI_FLTK, that connect the GUI with the gfcPlaybackManager

	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcPlaybackGUI{
public:
    gfcPlaybackGUI();

    ~gfcPlaybackGUI();

    virtual void assignTimeLineWidget(void* widget)=0;
    virtual void assignCurrentFrameWidget(void* widget)=0;
    virtual void assignFromWidget(void* widget)=0;
    virtual void assignToWidget(void* widget)=0;

	virtual void assignInPointWidget(void* widget)=0;
	virtual void assignOutPointWidget(void* widget)=0;

    virtual void assignTargetFPSWidget(void* widget)=0;
    virtual void assignCurrentFPSWidget(void* widget)=0;
    virtual void assignSMPTWidget(void* widget)=0;
    virtual void assignPlayFwdButtonWidget(void * widget)=0;
    virtual void assignPlayRevButtonWidget(void * widget)=0;
    virtual void assignFFwdButtonWidget(void * widget)=0;
    virtual void assignRwdButtonWidget(void * widget)=0;
    virtual void assignOneBackButtonWidget(void * widget)=0;
    virtual void assignOneFwdButtonWidget(void * widget)=0;
    virtual void assignPlaybackModeWidgets(void *once, void *loop, void *swing)=0;
    virtual void assignLoopPriorityWidget(void * widget)=0;
    
    virtual int getTimeLineValue()=0;
	virtual int getTimelineInPointValue()=0;
	virtual int getTimelineOutPointValue()=0;
    virtual int getCurrentFrame()=0;
    virtual int getFrom()=0;
    virtual int getTo()=0;

	virtual int getInPoint()=0;
	virtual int getOutPoint()=0;

    virtual float getTargetFPS()=0;
    virtual int getPlaybackMode()=0;
    virtual int getLoopPriority()=0;
    virtual float getFrameSize()=0;
    virtual void setTimelineValue(int pvalue)=0;
    virtual void setTimelineLimits(int min, int max)=0;
	virtual void setTimelineInOut(int in, int out)=0;
	virtual void setTimelineIn(int in)=0;
	virtual void setTimelineOut(int out)=0;
    virtual void setCurrentFrame(int pvalue)=0;
    virtual void setFrom(int pvalue)=0;
    virtual void setTo(int pvalue)=0;

	virtual void setInPoint(int pvalue)=0;
	virtual void setOutPoint(int pvalue)=0;

    virtual void setTargetFPS(float pvalue)=0;
    virtual void setCurrentFPS(float pvalue)=0;
    virtual void setSMPT(std::string pvalue)=0;
    virtual void setPlayFwdLabel(int pvalue)=0;
    virtual void setPlayRevLabel(int pvalue)=0;
    virtual void setPlaybackMode(int pvalue)=0;
    virtual void setLoopPriority(int pvalue)=0;
    
};

#endif

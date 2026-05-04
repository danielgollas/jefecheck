#ifndef GFCPLAYBACKGUI_FLTK_H
#define GFCPLAYBACKGUI_FLTK_H

#include "gfcplaybackgui.h"

#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Value_Output.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Value_Input.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Counter.H>
#include <FL/Fl_Choice.H>
 
#include <FL/Fl_Group.H> //needed for FL/Fl_Input_Choice.H
#include <FL/Fl_Input_Choice.H>
#include <FL/Fl_Slider.H>
#include "Fl_Slider_Timeline_gfc.h"
/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcPlaybackGUI_FLTK : public gfcPlaybackGUI
{
public:
    gfcPlaybackGUI_FLTK();

    ~gfcPlaybackGUI_FLTK();

    virtual void assignTimeLineWidget(void* widget);
    virtual void assignCurrentFrameWidget(void* widget);
    virtual void assignFromWidget(void* widget);
    virtual void assignToWidget(void* widget);

	virtual void assignInPointWidget(void* widget);
	virtual void assignOutPointWidget(void* widget);

    virtual void assignTargetFPSWidget(void* widget);
    virtual void assignCurrentFPSWidget(void* widget);
    virtual void assignSMPTWidget(void* widget);
    virtual void assignPlayFwdButtonWidget(void * widget);
    virtual void assignPlayRevButtonWidget(void * widget);
    virtual void assignFFwdButtonWidget(void * widget);
    virtual void assignRwdButtonWidget(void * widget);
    virtual void assignOneBackButtonWidget(void * widget);
    virtual void assignOneFwdButtonWidget(void * widget);
    virtual void assignPlaybackModeWidgets(void *once, void *loop, void *swing);
    virtual void assignLoopPriorityWidget(void * widget);
    
    virtual int getTimeLineValue();

	virtual int getTimelineInPointValue();
	virtual int getTimelineOutPointValue();

    virtual int getCurrentFrame();
    virtual int getFrom();
    virtual int getTo();

	virtual int getInPoint();
	virtual int getOutPoint();

    virtual float getTargetFPS();
    virtual int getPlaybackMode();
    virtual int getLoopPriority();
    virtual float getFrameSize();
    virtual void setTimelineValue(int pvalue);
    virtual void setTimelineLimits(int min, int max);

	virtual void setTimelineInOut(int in, int out);
	virtual void setTimelineIn(int in);
	virtual void setTimelineOut(int out);

    virtual void setCurrentFrame(int pvalue);
    virtual void setFrom(int pvalue);
    virtual void setTo(int pvalue);

	virtual void setInPoint(int pvalue);
	virtual void setOutPoint(int pvalue);

    virtual void setTargetFPS(float pvalue);
    virtual void setCurrentFPS(float pvalue);
    virtual void setSMPT(std::string pvalue);
    virtual void setPlayFwdLabel(int pvalue);
    virtual void setPlayRevLabel(int pvalue);
    virtual void setPlaybackMode(int pvalue);
    virtual void setLoopPriority(int pvalue);

    private:
    Fl_Slider_Timeline_gfc* timeLine;
    Fl_Counter* currentFrame;
    Fl_Value_Input* from;
    Fl_Value_Input* to;

	Fl_Value_Input* inPoint;
	Fl_Value_Input* outPoint;

    Fl_Input_Choice* targetFPS;
    Fl_Value_Output* currentFPS;
    Fl_Output* smpt;
    Fl_Button* playFwd;
	Fl_Color playFwdUpColor;
	Fl_Color playFwdDownColor;
    Fl_Button* playRev;
	Fl_Color playRevUpColor;
	Fl_Color playRevDownColor;
    Fl_Button* ff;
    Fl_Button* rwd;
    Fl_Button* oneBack;
    Fl_Button* oneFwd;
    Fl_Check_Button* once;
    Fl_Check_Button* loop;
    Fl_Check_Button* swing;
    Fl_Choice* loopPriority;
};

#endif

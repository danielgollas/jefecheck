#include "gfcplaybackgui_fltk.h"
#include "UIConstants.h"
#include "gfcplaybackmanager.h"
gfcPlaybackGUI_FLTK::gfcPlaybackGUI_FLTK(): gfcPlaybackGUI()
{
}


gfcPlaybackGUI_FLTK::~gfcPlaybackGUI_FLTK()
{
}




void gfcPlaybackGUI_FLTK::assignTimeLineWidget(void * widget)
{
	timeLine=(Fl_Slider_Timeline_gfc*)widget;
}

void gfcPlaybackGUI_FLTK::assignCurrentFrameWidget(void * widget)
{
	currentFrame=(Fl_Counter*)widget;
}

void gfcPlaybackGUI_FLTK::assignFromWidget(void * widget)
{
	from=(Fl_Value_Input*)widget;
}

void gfcPlaybackGUI_FLTK::assignToWidget(void * widget)
{
	to=(Fl_Value_Input*)widget;
}

void gfcPlaybackGUI_FLTK::assignInPointWidget(void * widget)
{
	inPoint=(Fl_Value_Input*)widget;
}

void gfcPlaybackGUI_FLTK::assignOutPointWidget(void * widget)
{
	outPoint=(Fl_Value_Input*)widget;
}

void gfcPlaybackGUI_FLTK::assignTargetFPSWidget(void * widget)
{
	targetFPS=(Fl_Input_Choice*)widget;
}

void gfcPlaybackGUI_FLTK::assignCurrentFPSWidget(void * widget)
{
	currentFPS=(Fl_Value_Output*)widget;
}

void gfcPlaybackGUI_FLTK::assignSMPTWidget(void * widget)
{
	smpt=(Fl_Output*)widget;
}

void gfcPlaybackGUI_FLTK::assignPlayFwdButtonWidget(void * widget)
{

	playFwd=(Fl_Button*)widget;
	if (playFwd)
	{
		this->playFwdDownColor=playFwd->down_color();
		this->playFwdUpColor=playFwd->color();
	}
}

void gfcPlaybackGUI_FLTK::assignPlayRevButtonWidget(void * widget)
{
	playRev=(Fl_Button*)widget;
	if (playRev)
	{
		this->playRevDownColor=playRev->down_color();
		this->playRevUpColor=playRev->color();
	}
	
}

void gfcPlaybackGUI_FLTK::assignFFwdButtonWidget(void * widget)
{
	ff=(Fl_Button*)widget;
}

void gfcPlaybackGUI_FLTK::assignRwdButtonWidget(void * widget)
{
	rwd=(Fl_Button*)widget;
}

void gfcPlaybackGUI_FLTK::assignOneBackButtonWidget(void * widget)
{
	oneBack=(Fl_Button*)widget;
}

void gfcPlaybackGUI_FLTK::assignOneFwdButtonWidget(void * widget)
{
	oneFwd=(Fl_Button*)widget;
}

void gfcPlaybackGUI_FLTK::assignPlaybackModeWidgets(void * ponce, void * ploop, void * pswing)
{
	once=(Fl_Check_Button*)ponce;
	loop=(Fl_Check_Button*)ploop;
	swing=(Fl_Check_Button*)pswing;
}

int gfcPlaybackGUI_FLTK::getTimeLineValue()
{
	return (int)timeLine->value();
}
int gfcPlaybackGUI_FLTK::getTimelineInPointValue()
{
	return timeLine->getInPoint();
}
int gfcPlaybackGUI_FLTK::getTimelineOutPointValue(){
	return timeLine->getOutPoint();
}

int gfcPlaybackGUI_FLTK::getCurrentFrame()
{
	return (int)currentFrame->value();
}

int gfcPlaybackGUI_FLTK::getFrom()
{
	return (int)from->value();
}

int gfcPlaybackGUI_FLTK::getTo()
{
	return (int)to->value();
}

float gfcPlaybackGUI_FLTK::getTargetFPS()
{
	/*float result;
	static std::stringstream ss;
	ss.str(targetFPS->value());
	ss>>result;
	printf("ss.str=%s (float: %f)\n",ss.str().c_str(),result);
	return result;*/
	return strtod(targetFPS->value(),NULL);
}

int gfcPlaybackGUI_FLTK::getPlaybackMode()
{
	if(once->value())
	{
		return LOOPMODEONCE_ID;
	}
	else if(loop->value())
	{
		return LOOPMODELOOP_ID;
	}
	else
	{
		return LOOPMODEBOUNCE_ID;
	}
}

void gfcPlaybackGUI_FLTK::setTimelineValue(int pvalue)
{
	timeLine->value(pvalue);
	this->currentFrame->value(pvalue);
}

void gfcPlaybackGUI_FLTK::setCurrentFrame(int pvalue)
{
	currentFrame->value(pvalue);
}

void gfcPlaybackGUI_FLTK::setFrom(int pvalue)
{
	from->value(pvalue);
}

void gfcPlaybackGUI_FLTK::setTo(int pvalue)
{
	to->value(pvalue);
	
}

void gfcPlaybackGUI_FLTK::setInPoint(int pvalue)
{
	inPoint->value(pvalue);
}

void gfcPlaybackGUI_FLTK::setOutPoint(int pvalue)
{
	outPoint->value(pvalue);
}

int gfcPlaybackGUI_FLTK::getInPoint()
{
	return inPoint->value();
}

int gfcPlaybackGUI_FLTK::getOutPoint()
{
	return outPoint->value();
}

void gfcPlaybackGUI_FLTK::setTargetFPS(float pvalue)
{
	char tmp[20];
	sprintf(tmp,"%.2f",pvalue);
	targetFPS->value(tmp);
}

void gfcPlaybackGUI_FLTK::setCurrentFPS(float pvalue)
{
	currentFPS->value(pvalue);
}

void gfcPlaybackGUI_FLTK::setSMPT(std::string pvalue)
{
	smpt->value(pvalue.c_str());
}

void gfcPlaybackGUI_FLTK::setPlayFwdLabel(int pvalue)
{
	/*if(!pvalue)
	playFwd->copy_label("@>");
	else
	playFwd->copy_label("@||");*/
	
	if(!pvalue)
	playFwd->color(playFwdUpColor);
	else
	playFwd->color(playFwdDownColor);
	playFwd->redraw();
	
}

void gfcPlaybackGUI_FLTK::setPlayRevLabel(int pvalue)
{
	/*if(!pvalue)
	playRev->copy_label("@<");
	else
	playRev->copy_label("@||");*/
	
	
	if(!pvalue)
	playRev->color(playRevUpColor);
	else
	playRev->color(playRevDownColor);
	
	playRev->redraw();
	
}

void gfcPlaybackGUI_FLTK::setPlaybackMode(int pvalue)
{
	switch(pvalue){
		
		case LOOPMODEONCE_ID:
			once->value(1);
			loop->value(0);
			swing->value(0);
			break;
			
		case LOOPMODELOOP_ID:
			once->value(0);
			loop->value(1);
			swing->value(0);
			break;
			
		case LOOPMODEBOUNCE_ID:
			once->value(0);
			loop->value(0);
			swing->value(1);
			break;
		
	}
}



void gfcPlaybackGUI_FLTK::setTimelineLimits(int min, int max)
{
	//timeLine->bounds((double)min,(double)max);
	timeLine->minimum(min);
	timeLine->maximum(max);
	//timeLine->size(timeLine->w()+1,timeLine->h()+1);
//	printf("Set Timeline to %f(%f) %f(%f)\n",(double)min,timeLine->minimum(),(double)max,timeLine->maximum());
	timeLine->slider_size( 1.0/ ( max - min +1 ) );
	//timeLine->redraw();
}

void gfcPlaybackGUI_FLTK::setTimelineInOut(int inP, int outP)
{
	
	/*timeLine->minimum(min);
	timeLine->maximum(max);*/
	timeLine->setInPoint(inP);
	timeLine->setOutPoint(outP);
	timeLine->damage();
	timeLine->redraw();

	//timeLine->slider_size( 1.0/ ( max - min +1 ) );
	
}

void gfcPlaybackGUI_FLTK::setTimelineIn(int inP)
{

	
	timeLine->setInPoint(inP);
	timeLine->damage();
	timeLine->redraw();

}

void gfcPlaybackGUI_FLTK::setTimelineOut(int outP)
{


	timeLine->setOutPoint(outP);
	timeLine->damage();
	timeLine->redraw();

}

float gfcPlaybackGUI_FLTK::getFrameSize()
{
	return (this->timeLine->w()/(float)(this->timeLine->maximum()-this->timeLine->minimum()+1));
}

void gfcPlaybackGUI_FLTK::assignLoopPriorityWidget(void * widget)
{
	loopPriority=(Fl_Choice*)widget;
}

int gfcPlaybackGUI_FLTK::getLoopPriority()
{
	switch(loopPriority->value())
	{
		case 0:
			return GFC_LOOPPRIORITY_SHORTEST;
		break;
		
		case 1:
			return GFC_LOOPPRIORITY_LONGEST;
		break;
		
		case 2:
			return GFC_LOOPPRIORITY_TIMELINE;

		default:
			return GFC_LOOPPRIORITY_SHORTEST;
		break;
	}

}
void gfcPlaybackGUI_FLTK::setLoopPriority(int pvalue)
{
	switch(pvalue)
	{
		case GFC_LOOPPRIORITY_SHORTEST:
			loopPriority->value(0);
		break;
		
		case GFC_LOOPPRIORITY_LONGEST:
			loopPriority->value(1);
		break;
		
		case GFC_LOOPPRIORITY_TIMELINE:
			loopPriority->value(2);
		break;
	}
}
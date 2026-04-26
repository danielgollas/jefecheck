#include "gfcplaybackgui_qt.h"

gfcPlaybackGUI_Qt::gfcPlaybackGUI_Qt() {}
gfcPlaybackGUI_Qt::~gfcPlaybackGUI_Qt() {}

void gfcPlaybackGUI_Qt::assignTimeLineWidget(void*) {}
void gfcPlaybackGUI_Qt::assignCurrentFrameWidget(void*) {}
void gfcPlaybackGUI_Qt::assignFromWidget(void*) {}
void gfcPlaybackGUI_Qt::assignToWidget(void*) {}
void gfcPlaybackGUI_Qt::assignInPointWidget(void*) {}
void gfcPlaybackGUI_Qt::assignOutPointWidget(void*) {}
void gfcPlaybackGUI_Qt::assignTargetFPSWidget(void*) {}
void gfcPlaybackGUI_Qt::assignCurrentFPSWidget(void*) {}
void gfcPlaybackGUI_Qt::assignSMPTWidget(void*) {}
void gfcPlaybackGUI_Qt::assignPlayFwdButtonWidget(void*) {}
void gfcPlaybackGUI_Qt::assignPlayRevButtonWidget(void*) {}
void gfcPlaybackGUI_Qt::assignFFwdButtonWidget(void*) {}
void gfcPlaybackGUI_Qt::assignRwdButtonWidget(void*) {}
void gfcPlaybackGUI_Qt::assignOneBackButtonWidget(void*) {}
void gfcPlaybackGUI_Qt::assignOneFwdButtonWidget(void*) {}
void gfcPlaybackGUI_Qt::assignPlaybackModeWidgets(void*, void*, void*) {}
void gfcPlaybackGUI_Qt::assignLoopPriorityWidget(void*) {}

int   gfcPlaybackGUI_Qt::getTimeLineValue()         { return 0; }
int   gfcPlaybackGUI_Qt::getTimelineInPointValue()  { return 0; }
int   gfcPlaybackGUI_Qt::getTimelineOutPointValue() { return 0; }
int   gfcPlaybackGUI_Qt::getCurrentFrame()          { return 0; }
int   gfcPlaybackGUI_Qt::getFrom()                  { return 0; }
int   gfcPlaybackGUI_Qt::getTo()                    { return 0; }
int   gfcPlaybackGUI_Qt::getInPoint()               { return 0; }
int   gfcPlaybackGUI_Qt::getOutPoint()              { return 0; }
float gfcPlaybackGUI_Qt::getTargetFPS()             { return 24.0f; }
int   gfcPlaybackGUI_Qt::getPlaybackMode()          { return 0; }
int   gfcPlaybackGUI_Qt::getLoopPriority()          { return 0; }
float gfcPlaybackGUI_Qt::getFrameSize()             { return 0.0f; }

void gfcPlaybackGUI_Qt::setTimelineValue(int) {}
void gfcPlaybackGUI_Qt::setTimelineLimits(int, int) {}
void gfcPlaybackGUI_Qt::setTimelineInOut(int, int) {}
void gfcPlaybackGUI_Qt::setTimelineIn(int) {}
void gfcPlaybackGUI_Qt::setTimelineOut(int) {}
void gfcPlaybackGUI_Qt::setCurrentFrame(int) {}
void gfcPlaybackGUI_Qt::setFrom(int) {}
void gfcPlaybackGUI_Qt::setTo(int) {}
void gfcPlaybackGUI_Qt::setInPoint(int) {}
void gfcPlaybackGUI_Qt::setOutPoint(int) {}
void gfcPlaybackGUI_Qt::setTargetFPS(float) {}
void gfcPlaybackGUI_Qt::setCurrentFPS(float) {}
void gfcPlaybackGUI_Qt::setSMPT(std::string) {}
void gfcPlaybackGUI_Qt::setPlayFwdLabel(int) {}
void gfcPlaybackGUI_Qt::setPlayRevLabel(int) {}
void gfcPlaybackGUI_Qt::setPlaybackMode(int) {}
void gfcPlaybackGUI_Qt::setLoopPriority(int) {}

// Qt skeleton for gfcPlaybackGUI. All methods stubbed. See docs/MIGRATION.md.
#ifndef GFCPLAYBACKGUI_QT_H
#define GFCPLAYBACKGUI_QT_H

#include "gfcplaybackgui.h"

class gfcPlaybackGUI_Qt : public gfcPlaybackGUI {
public:
    gfcPlaybackGUI_Qt();
    ~gfcPlaybackGUI_Qt();

    void assignTimeLineWidget(void*) override;
    void assignCurrentFrameWidget(void*) override;
    void assignFromWidget(void*) override;
    void assignToWidget(void*) override;
    void assignInPointWidget(void*) override;
    void assignOutPointWidget(void*) override;
    void assignTargetFPSWidget(void*) override;
    void assignCurrentFPSWidget(void*) override;
    void assignSMPTWidget(void*) override;
    void assignPlayFwdButtonWidget(void*) override;
    void assignPlayRevButtonWidget(void*) override;
    void assignFFwdButtonWidget(void*) override;
    void assignRwdButtonWidget(void*) override;
    void assignOneBackButtonWidget(void*) override;
    void assignOneFwdButtonWidget(void*) override;
    void assignPlaybackModeWidgets(void*, void*, void*) override;
    void assignLoopPriorityWidget(void*) override;

    int getTimeLineValue() override;
    int getTimelineInPointValue() override;
    int getTimelineOutPointValue() override;
    int getCurrentFrame() override;
    int getFrom() override;
    int getTo() override;
    int getInPoint() override;
    int getOutPoint() override;
    float getTargetFPS() override;
    int getPlaybackMode() override;
    int getLoopPriority() override;
    float getFrameSize() override;

    void setTimelineValue(int) override;
    void setTimelineLimits(int, int) override;
    void setTimelineInOut(int, int) override;
    void setTimelineIn(int) override;
    void setTimelineOut(int) override;
    void setCurrentFrame(int) override;
    void setFrom(int) override;
    void setTo(int) override;
    void setInPoint(int) override;
    void setOutPoint(int) override;
    void setTargetFPS(float) override;
    void setCurrentFPS(float) override;
    void setSMPT(std::string) override;
    void setPlayFwdLabel(int) override;
    void setPlayRevLabel(int) override;
    void setPlaybackMode(int) override;
    void setLoopPriority(int) override;
};

#endif

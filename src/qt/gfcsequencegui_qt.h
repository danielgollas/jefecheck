#ifndef GFCSEQUENCEGUI_QT_H
#define GFCSEQUENCEGUI_QT_H

#include "gfcsequencegui.h"

class gfcSequenceGUI_Qt : public gfcSequenceGUI {
public:
    gfcSequenceGUI_Qt();
    ~gfcSequenceGUI_Qt();

    std::string getFilename() override;
    int getFrom() override;
    int getTo() override;
    float getScale() override;
    float getGamma() override;
    std::vector<int> getLutList() override;
    int getStream() override;
    int getCompression() override;
    int getFilter() override;
    int getAppendOption() override;
    int getCrop() override;
    int getWindowVisible() override;
    int getChannel() override;
    std::string getChannelName() override;

    void setFromToBounds(int Min, int Max, bool setToMinAndMax=false) override;
    void setToFrame(int) override;
    void setFromFrame(int) override;
    void setFilename(std::string) override;
    void setScale(std::string) override;
    void setEstimates(std::string) override;
    void setAppendOption(int) override;
    void setFilter(int) override;
    void setCrop(int) override;
    void setCompression(int) override;
    void setChannelOptions(std::vector<std::string>) override;
    void setChannel(int) override;
    void setChannel(std::string) override;

    void activateAbortButton() override;
    void deactivateAbortButton() override;

    void clearAllValues() override;

    void setTotalFramesToLoad(int firstFrame, int howMany) override;
    void setLoadedRange(int) override;
    void setTrackOffset(int) override;
    void setTrackVisibleRange(int, int) override;
    void setTrackRange(int, int) override;
    void setTrackLabel(std::string) override;

    void updateTrackWidget() override;

    void setOffset(int) override;
    void setPlayHead(int) override;
    void setRecentlyLoaded(std::vector<std::string> &filenames) override;

    void assignFilenameWidget(void*) override;
    void assignFromWidget(void*) override;
    void assignToWidget(void*) override;
    void assignScaleWidget(void*) override;
    void assignGammaWidget(void*) override;
    void assignAOIWidget(void*) override;
    void assignStreamWidget(void*) override;
    void assignFilterWidget(void*) override;
    void assignAbortWidget(void*) override;
    void assignCompressionWidget(void*) override;
    void assignSliderWidget(void*) override;
    void assignBrowseWidget(void*) override;
    void assignWindowWidget(void*) override;
    void assignEstimatesWidget(void*) override;
    void assignRecentButton(void*) override;
    void assignUnloadAndClearButton(void*) override;
    void assignMoreOptionsButton(void*) override;
    void assignChannelOptionsWidget(void*) override;
    void assignStartButtonWidget(void*) override;

    int widgetBelongsToMe(void* theWidget) override;

private:
    // Minimal state needed by gfcSequence::loadPreview /
    // getLoadParamsFromGUI in the Qt build. Filled in from the drop
    // handler / future Qt panels; reads come back via getXxx().
    std::string filename_;
    std::string channelName_;
    int   from_         = 1;
    int   to_           = 1;
    int   filter_       = 0;
    int   compression_  = 0;
    int   append_       = 0;
    int   crop_         = 0;
    int   channel_      = -1;
    // params.scale is a percentage (100 = full size). FLTK's load window
    // defaults the slider to 100; mirror that here so dropped images
    // load at full resolution instead of getting resized to 1%.
    float scale_        = 100.0f;
    float gamma_        = 1.0f;
};

#endif

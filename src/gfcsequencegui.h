#ifndef GFCSEQUENCEGUI_H
#define GFCSEQUENCEGUI_H

#include <string>
#include <vector>
#include "gfcrectang.h"


/**
All sequences contain a sequenceGUI object in order to access the GUI elements that affect it. The class is implemented by subclases for a specific GUI toolkit. 

	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/
class gfcSequenceGUI{
public:
    gfcSequenceGUI();

    ~gfcSequenceGUI();
    
    /*
    get functions get the value of the GUI widget
    set functions set the value of the GUI widget
    assign functions set the pointer to the actual widget, will probably only be used when initializing.
    */
    
    virtual std::string getFilename()=0;
    virtual int getFrom()=0;
    virtual int getTo()=0;
    virtual float getScale()=0;
    virtual float getGamma()=0;
    virtual std::vector<int> getLutList()=0;
    
    virtual int getStream()=0;
    virtual int getCompression()=0;
    virtual int getFilter()=0;
    virtual int getAppendOption()=0;
    virtual int getCrop()=0;
    virtual int getWindowVisible()=0;
    virtual int getChannel()=0;
    virtual std::string getChannelName()=0;
    
    virtual void setFromToBounds(int Min, int Max, bool setToMinAndMax=false)=0;
    virtual void setToFrame(int value)=0;
    virtual void setFromFrame(int value)=0;
    virtual void setFilename(std::string filename)=0;
    virtual void setScale(std::string scale)=0;
    virtual void setEstimates(std::string estimates)=0;
    virtual void setAppendOption(int option)=0;
    virtual void setFilter(int filter)=0;
    virtual void setCrop(int crop)=0;
    virtual void setCompression(int compression)=0;
    virtual void setChannelOptions(std::vector<std::string> options)=0;
    virtual void setChannel(int value)=0;
    virtual void setChannel(std::string name)=0;
    
    
    virtual void activateAbortButton()=0;
    virtual void deactivateAbortButton()=0;
    
    
    
    virtual void clearAllValues()=0;
    
    //track functions
    virtual void setTotalFramesToLoad(int firstFrame, int howMany)=0;
    virtual void setLoadedRange(int numberOfLoadedFrames)=0;
    virtual void setTrackOffset(int offset)=0;
    virtual void setTrackVisibleRange(int from, int to)=0;
    virtual void setTrackRange(int start, int end)=0;
    virtual void setTrackLabel(std::string label)=0;

    virtual void updateTrackWidget()=0;

    virtual void setOffset(int offset)=0;
    virtual void setPlayHead(int timelineValue)=0;
    virtual void setRecentlyLoaded(std::vector<std::string> &filenames)=0;
    
    
    virtual void assignFilenameWidget(void *widget)=0;
    virtual void assignFromWidget(void *widget)=0;
    virtual void assignToWidget(void *widget)=0;
    virtual void assignScaleWidget(void *widget)=0;
    virtual void assignGammaWidget(void *widget)=0;
    virtual void assignAOIWidget(void *widget)=0;
    virtual void assignStreamWidget(void *widget)=0;
    virtual void assignFilterWidget(void *widget)=0;
    virtual void assignAbortWidget(void *widget)=0;
    virtual void assignCompressionWidget(void *widget)=0;
    virtual void assignSliderWidget(void *widget)=0;
    virtual void assignBrowseWidget(void *widget)=0;
    virtual void assignWindowWidget(void *widget)=0;
    virtual void assignEstimatesWidget(void *widget)=0;
    virtual void assignRecentButton(void *widget)=0;
    virtual void assignUnloadAndClearButton(void *widget)=0;
    virtual void assignMoreOptionsButton(void *widget)=0;
    virtual void assignChannelOptionsWidget(void *widget)=0;
	virtual void assignStartButtonWidget(void *widget)=0;
    
    virtual int widgetBelongsToMe(void* theWidget)=0;
    
    
    void setAOI(int x, int y, int w, int h);
    void setAOI(gfcRectang rectang);
    void setAOI(void);
    gfcRectang getAOI();
    
    
    
    private:
    	gfcRectang aoi;
    	bool updateTrackWidgetFlag;

};

#endif

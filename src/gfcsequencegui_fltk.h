#ifndef GFCSEQUENCEGUI_FLTK_H
#define GFCSEQUENCEGUI_FLTK_H

#include "gfcsequencegui.h"
#include <string>
#include <set>

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_File_Input.H>
#include <FL/Fl_Value_Slider.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Input_Choice.H>
#include <FL/Fl_Value_Input.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Input.H>
#include "trackwidget.h"
#include "gfcSliderInput.h"
/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/
class gfcSequenceGUI_FLTK : public gfcSequenceGUI {
public:
    gfcSequenceGUI_FLTK();

    ~gfcSequenceGUI_FLTK();

    virtual float getGamma();
    virtual float getScale();

    virtual int getCompression();
    virtual int getFilter();
    virtual int getFrom();
    virtual int getStream();
    virtual int getTo();
    virtual int getAppendOption();
    virtual int getCrop();
    virtual int getWindowVisible();
    virtual int getChannel();
    virtual std::string getChannelName();
    
    virtual void setFromToBounds(int Min, int Max, bool setToMinAndMax=false);
    virtual void setToFrame(int value);
    virtual void setFromFrame(int value);
    
    virtual std::vector< int > getLutList();
    virtual std::string getFilename();
    virtual void activateAbortButton();
    virtual void setRecentlyLoaded(std::vector<std::string> &filenames);
    virtual void deactivateAbortButton();
    //virtual void setLoadedRange(int start, int end);
    virtual void setOffset(int offset);
    virtual void setPlayHead(int timelineValue);
    virtual void setFilename(std::string filename);
    virtual void setScale(std::string scale);
    virtual void setEstimates(std::string estimates);
    virtual void setAppendOption(int option);
    virtual void setFilter(int filter);
    virtual void setCrop(int crop);
    virtual void setCompression(int compression);
    virtual void setChannelOptions(std::vector<std::string> options);
    virtual void setChannel(int value);
    virtual void setChannel(std::string name);
    
    
    virtual void clearAllValues();
    
    //track functions
    virtual void setTotalFramesToLoad(int firstFrame, int howMany);
    virtual void setLoadedRange(int numberOfLoadedFrames);
    virtual void setTrackOffset(int offset);
    virtual void setTrackVisibleRange(int from, int to);
    virtual void setTrackRange(int start, int end);
    virtual void setTrackLabel(std::string label);
    
    virtual void updateTrackWidget();

    virtual void assignFilenameWidget(void *widget);
    virtual void assignFromWidget(void *widget);
    virtual void assignToWidget(void *widget);
    virtual void assignScaleWidget(void *widget);
    virtual void assignGammaWidget(void *widget);
    virtual void assignAOIWidget(void *widget);
    virtual void assignStreamWidget(void *widget);
    virtual void assignFilterWidget(void *widget);
    virtual void assignAbortWidget(void *widget);
    virtual void assignCompressionWidget(void *widget);
    virtual void assignSliderWidget(void *widget);
    virtual void assignBrowseWidget(void *widget);
    virtual void assignWindowWidget(void *widget);
    virtual void assignEstimatesWidget(void *widget);
    virtual void assignRecentButton(void *widget);
    virtual void assignUnloadAndClearButton(void *widget);
    virtual void assignMoreOptionsButton(void *widget);
    virtual void assignChannelOptionsWidget(void *widget);
	virtual void assignStartButtonWidget(void *widget);
    
    virtual int widgetBelongsToMe(void* theWidget);
    
    gfcSliderInput *loadFromSpinner;
    gfcSliderInput *loadToSpinner;
    Fl_Choice *filterChooser;
    Fl_Choice *appendModeChooser;

    Fl_Check_Button *appendCheckBox;
    Fl_Check_Button *EraseCheckBox;
    Fl_Check_Button *LeaveCheckBox;
    Fl_Check_Button *ReplaceCheckBox;

    Fl_Input_Choice *scaleChooser;
    Fl_Button *browseButton;
    Fl_Button *reloadButton;
    Fl_Check_Button *aoiButton;
    Fl_Value_Input *gammaSlider;
    Fl_Choice *availableLuts;
    Fl_Choice *queuedLuts;
    Fl_Button *queueButton;
    Fl_Button *unQueueButton;
    Fl_Choice *compression;
    Fl_Input *fileNameInput;
    Fl_Check_Button *streamButton;
	Fl_Button *startButton;
    
    Fl_Choice *channels;
    
    Fl_Double_Window* window;
    
    Fl_Button *abortButton;
    TrackWidget *slider;
    
    Fl_Output *estimates;
    Fl_Menu_Button *recent;
    
    Fl_Button *unloadAndClear;
    Fl_Button *moreOptions;
    std::set<void*> containingWidgets; ///stores pointers to all the widgets. Helps identify what widgets belong to this GUI.
	
};

#endif

#ifndef GFCPLATEGUI_FLTK_H
#define GFCPLATEGUI_FLTK_H

#include "gfcplategui.h"

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Light_Button.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Value_Output.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Round_Button.H>
#include <FL/Fl_Value_Input.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Counter.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input_Choice.H>
#include <FL/Fl_Menu_Bar.H>
#include "Fl_Button_RGBA_gfc.h"
#include "Fl_Choice_gfc.h"

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/
class gfcPlateGUI_FLTK : public gfcPlateGUI {
public:
    gfcPlateGUI_FLTK();

    ~gfcPlateGUI_FLTK();
	
	virtual int getActive();
    virtual float getRZ();
    virtual gfcRectang getAOI();
    virtual int getCrop();
    virtual int getFlip();
    virtual int getFlop();
    virtual int getOffset();
    virtual int getSequenceID();
    virtual int getTX();
    virtual int getTY();
    virtual float getScale();
    virtual std::string getAspectString();
    virtual float getAspect();
	virtual int getRGBA();
    virtual bool getChannelR();
    virtual bool getChannelG();
    virtual bool getChannelB();
    virtual bool getChannelA();
    virtual bool getShowPreview();
    
	virtual int getLUT();
	virtual std::string getLUTName();
	virtual float getGamma();
	virtual float getExposure();
	virtual float getBrightness();
	virtual float getContrast();
	virtual float getSaturation();

    virtual void setScale(float pscale);
    virtual void setTX(float ptx);
    virtual void setTY(float pty);
    virtual void setRZ(float prz);
    virtual void setFlip(int pflip);
    virtual void setFlop(int pflop);
    virtual void setCrop(int pcrop);
    virtual void setOffset(int poffset);
    virtual void setTrackChoice(int pchoice);
    virtual void setAspectChoice(std::string paspect);
    virtual void setGroupPosition(int x,int y);
	virtual void setActive(int value);
    virtual void setActiveVisible(int mode);
	virtual void setGroupVisible(int mode);
    virtual void setRGBA(int value);
	virtual void setChannelR(bool value);
    virtual void setChannelG(bool value);
    virtual void setChannelB(bool value);
    virtual void setChannelA(bool value);
    
	virtual void setLUT(int value);
	virtual void clearLUTs();
	virtual void addLUTOption(std::string name);
	virtual void setGamma(float value);
	virtual void setExposure(float value);
	virtual void setBrightness(float value);
	virtual void setContrast(float value);
	virtual void setSaturation(float value);


    virtual void assignTXWidget(void* widget);
    virtual void assignTYWidget(void* widget);
    virtual void assignRZWidget(void* widget);
    virtual void assignZoom(void* widget);
    virtual void assignFlipWidget(void* widget);
    virtual void assignFlopWidget(void* widget);
    virtual void assignCropWidget(void* widget);
	virtual void assignRGBAWidget(void *widget);
    virtual void assignChannelRWidget(void* widget);
    virtual void assignChannelGWidget(void* widget);
    virtual void assignChannelBWidget(void* widget);
    virtual void assignChannelAWidget(void* widget);
    virtual void assignOffsetWidget(void* widget);
    virtual void assignTrackChoiceWidget(void* widget);
    virtual void assignAspectChoiceWidget(void* widget);
    virtual void assignGroupWidget(void* widget);
	virtual void assignActiveWidget(void* widget);

	virtual void assignLUTWidget(void *widget);
	virtual void assignGammaWidget(void *widget);
	virtual void assignExposureWidget(void *widget);
	virtual void assignBrightnessWidget(void *widget);
	virtual void assignContrastWidget(void *widget);
	virtual void assignSaturationWidget(void *widget);

private:
    virtual void assignShowPreviewWidget(void* widget);
    
    Fl_Choice *trackChoice;
    Fl_Input_Choice *aspectChoice;
    Fl_Button *cropPosButton;
    Fl_Value_Input *scaleInput;
    Fl_Value_Input *txInput;
    Fl_Value_Input *tyInput;
    Fl_Value_Input *rzInput;
    Fl_Button *flipButton;
    Fl_Button *cropButton;
    Fl_Button *flopButton;
	Fl_Button_RGBA_gfc *rgbaButton;
    Fl_Button *channelRButton;
    Fl_Button *channelGButton;
    Fl_Button *channelBButton;
    Fl_Button *channelAButton;
	Fl_Round_Button *activeButton;
    Fl_Group *group;
	
	Fl_Choice_gfc *lutChoice;
	Fl_Value_Input *gammaInput;
	Fl_Value_Input *exposureInput;
	Fl_Value_Input *brightnessInput;
	Fl_Value_Input *contrastInput;
	Fl_Value_Input *saturationInput;

    Fl_Value_Input *offsetInput;
    
    Fl_Double_Window* loadWindow;//the load window is our show preview widget

};

#endif

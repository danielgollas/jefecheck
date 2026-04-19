#ifndef GFCPLATEGUI_H
#define GFCPLATEGUI_H

#include <glad/glad.h>
#include <string>
#include "gfcrectang.h"

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/
class gfcPlateGUI{
public:
    gfcPlateGUI();

    ~gfcPlateGUI();
    
	virtual int getActive()=0;
    virtual int getSequenceID()=0;
    virtual std::string getAspectString()=0;
    virtual float getAspect()=0;
    virtual int getTX()=0;
    virtual int getTY()=0;
    virtual float getScale()=0;
    virtual float getRZ()=0;
    virtual int getFlip()=0;
    virtual int getFlop()=0;
    virtual int getCrop()=0;
    virtual int getOffset()=0;
    virtual gfcRectang getAOI()=0;
	virtual int getRGBA()=0;
    virtual bool getChannelR()=0;
    virtual bool getChannelG()=0;
    virtual bool getChannelB()=0;
    virtual bool getChannelA()=0;
    virtual bool getShowPreview()=0;
    
	virtual void setRGBA(int value)=0;
    virtual void setTX(float ptx)=0;
    virtual void setScale(float pscale)=0;
    virtual void setTY(float pty)=0;
    virtual void setRZ(float prz)=0;
    virtual void setFlip(int pflip)=0;
    virtual void setFlop(int pflop)=0;
    virtual void setCrop(int pcrop)=0;
    virtual void setOffset(int poffset)=0;
    virtual void setTrackChoice(int pchoice)=0;
    virtual void setAspectChoice(std::string paspect)=0;
    virtual void setGroupPosition(int x,int y)=0;
	virtual void setActive(int value)=0;
	virtual void setActiveVisible(int mode)=0;
    virtual void setGroupVisible(int mode)=0;
    virtual void setChannelR(bool value)=0;
    virtual void setChannelG(bool value)=0;
    virtual void setChannelB(bool value)=0;
    virtual void setChannelA(bool value)=0;
    
	virtual void setLUT(int value)=0;
	virtual void clearLUTs()=0;
	virtual void addLUTOption(std::string name)=0;
	virtual void setGamma(float value)=0;
	virtual void setExposure(float value)=0;
	virtual void setBrightness(float value)=0;
	virtual void setContrast(float value)=0;
	virtual void setSaturation(float value)=0;
	
	virtual int getLUT()=0;
	virtual std::string getLUTName()=0;
	virtual float getGamma()=0;
	virtual float getExposure()=0;
	virtual float getBrightness()=0;
	virtual float getContrast()=0;
	virtual float getSaturation()=0;

    virtual void assignTXWidget(void* widget)=0;
    virtual void assignTYWidget(void* widget)=0;
    virtual void assignRZWidget(void* widget)=0;
    virtual void assignZoom(void* widget)=0;
    virtual void assignFlipWidget(void* widget)=0;
    virtual void assignFlopWidget(void* widget)=0;
    virtual void assignCropWidget(void* widget)=0;
	virtual void assignRGBAWidget(void *widget)=0;
    virtual void assignChannelRWidget(void* widget)=0;
    virtual void assignChannelGWidget(void* widget)=0;
    virtual void assignChannelBWidget(void* widget)=0;
    virtual void assignChannelAWidget(void* widget)=0;
    virtual void assignOffsetWidget(void* widget)=0;
    virtual void assignTrackChoiceWidget(void* widget)=0;
    virtual void assignAspectChoiceWidget(void* widget)=0;
    virtual void assignGroupWidget(void* widget)=0;
	virtual void assignActiveWidget(void* widget)=0;
    virtual void assignShowPreviewWidget(void* widget)=0;

	virtual void assignLUTWidget(void *widget)=0;
	virtual void assignGammaWidget(void *widget)=0;
	virtual void assignExposureWidget(void *widget)=0;
	virtual void assignBrightnessWidget(void *widget)=0;
	virtual void assignContrastWidget(void *widget)=0;
	virtual void assignSaturationWidget(void *widget)=0;

};

#endif

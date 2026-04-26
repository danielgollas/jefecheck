// Qt skeleton for gfcPlateGUI. All methods stubbed. See docs/MIGRATION.md.
#ifndef GFCPLATEGUI_QT_H
#define GFCPLATEGUI_QT_H

#include "gfcplategui.h"

class gfcPlateGUI_Qt : public gfcPlateGUI {
public:
    gfcPlateGUI_Qt();
    ~gfcPlateGUI_Qt();

    int getActive() override;
    int getSequenceID() override;
    std::string getAspectString() override;
    float getAspect() override;
    int getTX() override;
    int getTY() override;
    float getScale() override;
    float getRZ() override;
    int getFlip() override;
    int getFlop() override;
    int getCrop() override;
    int getOffset() override;
    gfcRectang getAOI() override;
    int getRGBA() override;
    bool getChannelR() override;
    bool getChannelG() override;
    bool getChannelB() override;
    bool getChannelA() override;
    bool getShowPreview() override;

    void setRGBA(int value) override;
    void setTX(float ptx) override;
    void setScale(float pscale) override;
    void setTY(float pty) override;
    void setRZ(float prz) override;
    void setFlip(int pflip) override;
    void setFlop(int pflop) override;
    void setCrop(int pcrop) override;
    void setOffset(int poffset) override;
    void setTrackChoice(int pchoice) override;
    void setAspectChoice(std::string paspect) override;
    void setGroupPosition(int x, int y) override;
    void setActive(int value) override;
    void setActiveVisible(int mode) override;
    void setGroupVisible(int mode) override;
    void setChannelR(bool value) override;
    void setChannelG(bool value) override;
    void setChannelB(bool value) override;
    void setChannelA(bool value) override;

    void setLUT(int value) override;
    void clearLUTs() override;
    void addLUTOption(std::string name) override;
    void setGamma(float value) override;
    void setExposure(float value) override;
    void setBrightness(float value) override;
    void setContrast(float value) override;
    void setSaturation(float value) override;

    int getLUT() override;
    std::string getLUTName() override;
    float getGamma() override;
    float getExposure() override;
    float getBrightness() override;
    float getContrast() override;
    float getSaturation() override;

    void assignTXWidget(void* widget) override;
    void assignTYWidget(void* widget) override;
    void assignRZWidget(void* widget) override;
    void assignZoom(void* widget) override;
    void assignFlipWidget(void* widget) override;
    void assignFlopWidget(void* widget) override;
    void assignCropWidget(void* widget) override;
    void assignRGBAWidget(void *widget) override;
    void assignChannelRWidget(void* widget) override;
    void assignChannelGWidget(void* widget) override;
    void assignChannelBWidget(void* widget) override;
    void assignChannelAWidget(void* widget) override;
    void assignOffsetWidget(void* widget) override;
    void assignTrackChoiceWidget(void* widget) override;
    void assignAspectChoiceWidget(void* widget) override;
    void assignGroupWidget(void* widget) override;
    void assignActiveWidget(void* widget) override;
    void assignShowPreviewWidget(void* widget) override;

    void assignLUTWidget(void *widget) override;
    void assignGammaWidget(void *widget) override;
    void assignExposureWidget(void *widget) override;
    void assignBrightnessWidget(void *widget) override;
    void assignContrastWidget(void *widget) override;
    void assignSaturationWidget(void *widget) override;
};

#endif

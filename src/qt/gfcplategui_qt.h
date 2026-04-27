// Stateful Qt implementation of gfcPlateGUI. Replaces the previous all-
// stubs version: each setX() updates an internal field; getX() returns
// it. The model class (gfcPlate) reads its state through this interface,
// so a PlateCard_Qt can drive a real plate by calling these setters from
// its widget signals — no bridge between Qt widgets and the rendering
// pipeline beyond this object.
//
// The "assign*Widget" methods are no-ops in the Qt backend. Their FLTK
// counterparts cache widget pointers so the FLTK GUI subclass can read
// values directly; on the Qt side the widgets push values into us via
// setters, so we don't need the back-pointer.
#ifndef GFCPLATEGUI_QT_H
#define GFCPLATEGUI_QT_H

#include "gfcplategui.h"

#include <string>
#include <vector>

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

    // Plate id (0..3). Only used by the optional debug log so the user
    // can see which plate's controls fired during smoke-tests; not part
    // of the abstract gfcPlateGUI surface.
    void setPlateIndex(int idx) { plateIndex_ = idx; }

    // Drives gfcPlate::draw3Drect's preview-vs-loaded branch. The Qt
    // build flips this on for plates pointing at a sequence with a
    // preview frame loaded. Not part of the abstract surface (FLTK
    // reads it from a widget).
    void setShowPreview(bool v) { showPreview_ = v; }

private:
    int plateIndex_ = -1;

    // Identity / source
    int   active_ = 0;
    int   trackChoice_ = -1;

    // Aspect
    std::string aspectString_ = "original";
    float aspect_ = 1.0f;

    // Transforms
    int   tx_ = 0;
    int   ty_ = 0;
    float scale_ = 1.0f;
    float rz_ = 0.0f;
    int   flip_ = 0;
    int   flop_ = 0;
    int   crop_ = 0;
    int   offset_ = 0;

    // Channel masks
    int   rgba_ = 0;
    bool  channelR_ = true;
    bool  channelG_ = true;
    bool  channelB_ = true;
    bool  channelA_ = true;

    // Preview
    bool  showPreview_ = false;

    // Color correction
    int   lut_ = 0;
    std::vector<std::string> lutOptions_;
    float gamma_ = 1.0f;
    float exposure_ = 0.0f;
    float brightness_ = 0.0f;
    float contrast_ = 1.0f;
    float saturation_ = 1.0f;
};

#endif

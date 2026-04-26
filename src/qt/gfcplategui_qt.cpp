// Qt skeleton for gfcPlateGUI. All bodies are stubs; fill in when porting.
#include "gfcplategui_qt.h"

gfcPlateGUI_Qt::gfcPlateGUI_Qt() {}
gfcPlateGUI_Qt::~gfcPlateGUI_Qt() {}

int   gfcPlateGUI_Qt::getActive()       { return 0; }
int   gfcPlateGUI_Qt::getSequenceID()   { return -1; }
std::string gfcPlateGUI_Qt::getAspectString() { return {}; }
float gfcPlateGUI_Qt::getAspect()       { return 1.0f; }
int   gfcPlateGUI_Qt::getTX()           { return 0; }
int   gfcPlateGUI_Qt::getTY()           { return 0; }
float gfcPlateGUI_Qt::getScale()        { return 1.0f; }
float gfcPlateGUI_Qt::getRZ()           { return 0.0f; }
int   gfcPlateGUI_Qt::getFlip()         { return 0; }
int   gfcPlateGUI_Qt::getFlop()         { return 0; }
int   gfcPlateGUI_Qt::getCrop()         { return 0; }
int   gfcPlateGUI_Qt::getOffset()       { return 0; }
gfcRectang gfcPlateGUI_Qt::getAOI()     { return {}; }
int   gfcPlateGUI_Qt::getRGBA()         { return 0; }
bool  gfcPlateGUI_Qt::getChannelR()     { return true; }
bool  gfcPlateGUI_Qt::getChannelG()     { return true; }
bool  gfcPlateGUI_Qt::getChannelB()     { return true; }
bool  gfcPlateGUI_Qt::getChannelA()     { return true; }
bool  gfcPlateGUI_Qt::getShowPreview()  { return false; }

void gfcPlateGUI_Qt::setRGBA(int) {}
void gfcPlateGUI_Qt::setTX(float) {}
void gfcPlateGUI_Qt::setScale(float) {}
void gfcPlateGUI_Qt::setTY(float) {}
void gfcPlateGUI_Qt::setRZ(float) {}
void gfcPlateGUI_Qt::setFlip(int) {}
void gfcPlateGUI_Qt::setFlop(int) {}
void gfcPlateGUI_Qt::setCrop(int) {}
void gfcPlateGUI_Qt::setOffset(int) {}
void gfcPlateGUI_Qt::setTrackChoice(int) {}
void gfcPlateGUI_Qt::setAspectChoice(std::string) {}
void gfcPlateGUI_Qt::setGroupPosition(int, int) {}
void gfcPlateGUI_Qt::setActive(int) {}
void gfcPlateGUI_Qt::setActiveVisible(int) {}
void gfcPlateGUI_Qt::setGroupVisible(int) {}
void gfcPlateGUI_Qt::setChannelR(bool) {}
void gfcPlateGUI_Qt::setChannelG(bool) {}
void gfcPlateGUI_Qt::setChannelB(bool) {}
void gfcPlateGUI_Qt::setChannelA(bool) {}

void gfcPlateGUI_Qt::setLUT(int) {}
void gfcPlateGUI_Qt::clearLUTs() {}
void gfcPlateGUI_Qt::addLUTOption(std::string) {}
void gfcPlateGUI_Qt::setGamma(float) {}
void gfcPlateGUI_Qt::setExposure(float) {}
void gfcPlateGUI_Qt::setBrightness(float) {}
void gfcPlateGUI_Qt::setContrast(float) {}
void gfcPlateGUI_Qt::setSaturation(float) {}

int   gfcPlateGUI_Qt::getLUT()        { return 0; }
std::string gfcPlateGUI_Qt::getLUTName() { return {}; }
float gfcPlateGUI_Qt::getGamma()      { return 1.0f; }
float gfcPlateGUI_Qt::getExposure()   { return 0.0f; }
float gfcPlateGUI_Qt::getBrightness() { return 0.0f; }
float gfcPlateGUI_Qt::getContrast()   { return 1.0f; }
float gfcPlateGUI_Qt::getSaturation() { return 1.0f; }

void gfcPlateGUI_Qt::assignTXWidget(void*) {}
void gfcPlateGUI_Qt::assignTYWidget(void*) {}
void gfcPlateGUI_Qt::assignRZWidget(void*) {}
void gfcPlateGUI_Qt::assignZoom(void*) {}
void gfcPlateGUI_Qt::assignFlipWidget(void*) {}
void gfcPlateGUI_Qt::assignFlopWidget(void*) {}
void gfcPlateGUI_Qt::assignCropWidget(void*) {}
void gfcPlateGUI_Qt::assignRGBAWidget(void*) {}
void gfcPlateGUI_Qt::assignChannelRWidget(void*) {}
void gfcPlateGUI_Qt::assignChannelGWidget(void*) {}
void gfcPlateGUI_Qt::assignChannelBWidget(void*) {}
void gfcPlateGUI_Qt::assignChannelAWidget(void*) {}
void gfcPlateGUI_Qt::assignOffsetWidget(void*) {}
void gfcPlateGUI_Qt::assignTrackChoiceWidget(void*) {}
void gfcPlateGUI_Qt::assignAspectChoiceWidget(void*) {}
void gfcPlateGUI_Qt::assignGroupWidget(void*) {}
void gfcPlateGUI_Qt::assignActiveWidget(void*) {}
void gfcPlateGUI_Qt::assignShowPreviewWidget(void*) {}
void gfcPlateGUI_Qt::assignLUTWidget(void*) {}
void gfcPlateGUI_Qt::assignGammaWidget(void*) {}
void gfcPlateGUI_Qt::assignExposureWidget(void*) {}
void gfcPlateGUI_Qt::assignBrightnessWidget(void*) {}
void gfcPlateGUI_Qt::assignContrastWidget(void*) {}
void gfcPlateGUI_Qt::assignSaturationWidget(void*) {}

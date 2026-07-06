#include "gfcplategui_qt.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace {
// Toggleable noisy log so you can verify in a smoke test that PlateCard
// signals are flowing into the GUI state. Off by default; enable via
// JEFECHECK_QT_PLATE_LOG=1.
bool plateLogEnabled() {
    static int cached = -1;
    if (cached < 0) {
        const char* v = std::getenv("JEFECHECK_QT_PLATE_LOG");
        cached = (v && *v && *v != '0') ? 1 : 0;
    }
    return cached != 0;
}

void plog(int idx, const char* fmt, ...) {
    if (!plateLogEnabled()) return;
    fprintf(stderr, "[plate %d] ", idx);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

float aspectFromString(const std::string& s) {
    if (s == "16:9")    return 16.0f / 9.0f;
    if (s == "4:3")     return 4.0f / 3.0f;
    if (s == "2.39:1")  return 2.39f;
    if (s == "2.35:1")  return 2.35f;
    if (s == "1.85:1")  return 1.85f;
    if (s == "1.37:1")  return 1.37f;
    // "original" / unknown: -1 is gfcPlate's sentinel for "use the frame's
    // native size" (calculatePolySizesCropEtc). Returning 1.0 here forced a
    // square (960x960) poly that squashed the image; -1 keeps native aspect.
    return -1.0f;
}
}  // namespace

gfcPlateGUI_Qt::gfcPlateGUI_Qt() {}
gfcPlateGUI_Qt::~gfcPlateGUI_Qt() {}

int   gfcPlateGUI_Qt::getActive()       { return active_; }
int   gfcPlateGUI_Qt::getSequenceID()   { return trackChoice_; }
std::string gfcPlateGUI_Qt::getAspectString() { return aspectString_; }
float gfcPlateGUI_Qt::getAspect()       { return aspect_; }
int   gfcPlateGUI_Qt::getTX()           { return tx_; }
int   gfcPlateGUI_Qt::getTY()           { return ty_; }
float gfcPlateGUI_Qt::getScale()        { return scale_; }
float gfcPlateGUI_Qt::getRZ()           { return rz_; }
int   gfcPlateGUI_Qt::getFlip()         { return flip_; }
int   gfcPlateGUI_Qt::getFlop()         { return flop_; }
int   gfcPlateGUI_Qt::getCrop()         { return crop_; }
int   gfcPlateGUI_Qt::getOffset()       { return offset_; }
gfcRectang gfcPlateGUI_Qt::getAOI()     { return {}; }
int   gfcPlateGUI_Qt::getRGBA()         { return rgba_; }
bool  gfcPlateGUI_Qt::getChannelR()     { return channelR_; }
bool  gfcPlateGUI_Qt::getChannelG()     { return channelG_; }
bool  gfcPlateGUI_Qt::getChannelB()     { return channelB_; }
bool  gfcPlateGUI_Qt::getChannelA()     { return channelA_; }
bool  gfcPlateGUI_Qt::getShowPreview()  { return showPreview_; }

void gfcPlateGUI_Qt::setRGBA(int v)            { rgba_ = v;       plog(plateIndex_, "RGBA=%d", v); }
void gfcPlateGUI_Qt::setTX(float v)            { tx_ = (int)v;    plog(plateIndex_, "TX=%g", v); }
void gfcPlateGUI_Qt::setScale(float v)         { scale_ = v;      plog(plateIndex_, "scale=%g", v); }
void gfcPlateGUI_Qt::setTY(float v)            { ty_ = (int)v;    plog(plateIndex_, "TY=%g", v); }
void gfcPlateGUI_Qt::setRZ(float v)            { rz_ = v;         plog(plateIndex_, "RZ=%g", v); }
void gfcPlateGUI_Qt::setFlip(int v)            { flip_ = v;       plog(plateIndex_, "flip=%d", v); }
void gfcPlateGUI_Qt::setFlop(int v)            { flop_ = v;       plog(plateIndex_, "flop=%d", v); }
void gfcPlateGUI_Qt::setCrop(int v)            { crop_ = v;       plog(plateIndex_, "crop=%d", v); }
void gfcPlateGUI_Qt::setOffset(int v)          { offset_ = v;     plog(plateIndex_, "offset=%d", v); }
void gfcPlateGUI_Qt::setTrackChoice(int v)     { trackChoice_ = v;plog(plateIndex_, "track=%d", v); }
void gfcPlateGUI_Qt::setAspectChoice(std::string v) {
    aspectString_ = v;
    aspect_ = aspectFromString(v);
    plog(plateIndex_, "aspect=%s (%g)", v.c_str(), aspect_);
}
void gfcPlateGUI_Qt::setGroupPosition(int, int) {}
void gfcPlateGUI_Qt::setActive(int v)          { active_ = v;     plog(plateIndex_, "active=%d", v); }
void gfcPlateGUI_Qt::setActiveVisible(int)     {}
void gfcPlateGUI_Qt::setGroupVisible(int)      {}
void gfcPlateGUI_Qt::setChannelR(bool v)       { channelR_ = v;   plog(plateIndex_, "R=%d", v); }
void gfcPlateGUI_Qt::setChannelG(bool v)       { channelG_ = v;   plog(plateIndex_, "G=%d", v); }
void gfcPlateGUI_Qt::setChannelB(bool v)       { channelB_ = v;   plog(plateIndex_, "B=%d", v); }
void gfcPlateGUI_Qt::setChannelA(bool v)       { channelA_ = v;   plog(plateIndex_, "A=%d", v); }

void gfcPlateGUI_Qt::setLUT(int v)             { lut_ = v;        plog(plateIndex_, "LUT=%d", v); }
void gfcPlateGUI_Qt::clearLUTs()               { lutOptions_.clear(); }
void gfcPlateGUI_Qt::addLUTOption(std::string n) { lutOptions_.push_back(std::move(n)); }
void gfcPlateGUI_Qt::setGamma(float v)         { gamma_ = v;      plog(plateIndex_, "gamma=%g", v); }
void gfcPlateGUI_Qt::setExposure(float v)      { exposure_ = v;   plog(plateIndex_, "exposure=%g", v); }
void gfcPlateGUI_Qt::setBrightness(float v)    { brightness_ = v; plog(plateIndex_, "brightness=%g", v); }
void gfcPlateGUI_Qt::setContrast(float v)      { contrast_ = v;   plog(plateIndex_, "contrast=%g", v); }
void gfcPlateGUI_Qt::setSaturation(float v)    { saturation_ = v; plog(plateIndex_, "saturation=%g", v); }

int   gfcPlateGUI_Qt::getLUT()        { return lut_; }
std::string gfcPlateGUI_Qt::getLUTName() {
    if (lut_ >= 0 && lut_ < (int)lutOptions_.size()) return lutOptions_[lut_];
    return {};
}
float gfcPlateGUI_Qt::getGamma()      { return gamma_; }
float gfcPlateGUI_Qt::getExposure()   { return exposure_; }
float gfcPlateGUI_Qt::getBrightness() { return brightness_; }
float gfcPlateGUI_Qt::getContrast()   { return contrast_; }
float gfcPlateGUI_Qt::getSaturation() { return saturation_; }

// In the Qt backend we don't cache widget pointers — the widgets push
// values into us via setX() from their signal connections, rather than
// us reading values back out via the widget pointer (FLTK pattern).
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

#include "gfcsequencegui_qt.h"

gfcSequenceGUI_Qt::gfcSequenceGUI_Qt() {}
gfcSequenceGUI_Qt::~gfcSequenceGUI_Qt() {}

std::string gfcSequenceGUI_Qt::getFilename()    { return filename_; }
int   gfcSequenceGUI_Qt::getFrom()              { return from_; }
int   gfcSequenceGUI_Qt::getTo()                { return to_; }
float gfcSequenceGUI_Qt::getScale()             { return scale_; }
float gfcSequenceGUI_Qt::getGamma()             { return gamma_; }
std::vector<int> gfcSequenceGUI_Qt::getLutList() { return {}; }
int   gfcSequenceGUI_Qt::getStream()            { return 0; }
int   gfcSequenceGUI_Qt::getCompression()       { return compression_; }
int   gfcSequenceGUI_Qt::getFilter()            { return filter_; }
int   gfcSequenceGUI_Qt::getAppendOption()      { return append_; }
int   gfcSequenceGUI_Qt::getCrop()              { return crop_; }
int   gfcSequenceGUI_Qt::getWindowVisible()     { return 0; }
int   gfcSequenceGUI_Qt::getChannel()           { return channel_; }
std::string gfcSequenceGUI_Qt::getChannelName() { return channelName_; }

void gfcSequenceGUI_Qt::setFromToBounds(int Min, int Max, bool setToMinAndMax) {
    // gfcSequence::findSequenceFiles calls this with the discovered
    // sequence range. Without storing it, getFrom()/getTo() keep
    // their constructor defaults (1,1) and loadSequence loads only the
    // first frame. The FLTK impl writes the range onto its spinner
    // widgets; here we just stash it onto from_/to_ when the caller
    // asks for clamp-to-bounds (the only path that flows through
    // findSequenceFiles).
    if (setToMinAndMax) {
        from_ = Min;
        to_   = Max;
    }
}
void gfcSequenceGUI_Qt::setToFrame(int v)               { to_ = v; }
void gfcSequenceGUI_Qt::setFromFrame(int v)             { from_ = v; }
void gfcSequenceGUI_Qt::setFilename(std::string s)      { filename_ = std::move(s); }
void gfcSequenceGUI_Qt::setScale(std::string s) {
    // FLTK's gfcSequenceGUI_FLTK::setScale stores the percentage
    // string from the Choice widget ("100" / "50" / "25"); getScale()
    // returns the float for the loader. Mirror that contract here so
    // the SequenceLoadBridge's Shift-drop scale modifier actually
    // takes effect — without this body the bridge writes a string
    // that's silently dropped, and getScale keeps returning the
    // constructor's default of 100.0f.
    if (s.empty()) {
        scale_ = 100.0f;
        return;
    }
    try {
        scale_ = std::stof(s);
    } catch (...) {
        scale_ = 100.0f;
    }
}
void gfcSequenceGUI_Qt::setEstimates(std::string)       {}
void gfcSequenceGUI_Qt::setAppendOption(int v)          { append_ = v; }
void gfcSequenceGUI_Qt::setFilter(int v)                { filter_ = v; }
void gfcSequenceGUI_Qt::setCrop(int v)                  { crop_ = v; }
void gfcSequenceGUI_Qt::setCompression(int v)           { compression_ = v; }
void gfcSequenceGUI_Qt::setChannelOptions(std::vector<std::string>) {}
void gfcSequenceGUI_Qt::setChannel(int v)               { channel_ = v; }
void gfcSequenceGUI_Qt::setChannel(std::string s)       { channelName_ = std::move(s); }

void gfcSequenceGUI_Qt::activateAbortButton() {}
void gfcSequenceGUI_Qt::deactivateAbortButton() {}

void gfcSequenceGUI_Qt::clearAllValues() {}

void gfcSequenceGUI_Qt::setTotalFramesToLoad(int, int) {}
void gfcSequenceGUI_Qt::setLoadedRange(int) {}
void gfcSequenceGUI_Qt::setTrackOffset(int) {}
void gfcSequenceGUI_Qt::setTrackVisibleRange(int, int) {}
void gfcSequenceGUI_Qt::setTrackRange(int, int) {}
void gfcSequenceGUI_Qt::setTrackLabel(std::string) {}

void gfcSequenceGUI_Qt::updateTrackWidget() {}

void gfcSequenceGUI_Qt::setOffset(int) {}
void gfcSequenceGUI_Qt::setPlayHead(int) {}
void gfcSequenceGUI_Qt::setRecentlyLoaded(std::vector<std::string>&) {}

void gfcSequenceGUI_Qt::assignFilenameWidget(void*) {}
void gfcSequenceGUI_Qt::assignFromWidget(void*) {}
void gfcSequenceGUI_Qt::assignToWidget(void*) {}
void gfcSequenceGUI_Qt::assignScaleWidget(void*) {}
void gfcSequenceGUI_Qt::assignGammaWidget(void*) {}
void gfcSequenceGUI_Qt::assignAOIWidget(void*) {}
void gfcSequenceGUI_Qt::assignStreamWidget(void*) {}
void gfcSequenceGUI_Qt::assignFilterWidget(void*) {}
void gfcSequenceGUI_Qt::assignAbortWidget(void*) {}
void gfcSequenceGUI_Qt::assignCompressionWidget(void*) {}
void gfcSequenceGUI_Qt::assignSliderWidget(void*) {}
void gfcSequenceGUI_Qt::assignBrowseWidget(void*) {}
void gfcSequenceGUI_Qt::assignWindowWidget(void*) {}
void gfcSequenceGUI_Qt::assignEstimatesWidget(void*) {}
void gfcSequenceGUI_Qt::assignRecentButton(void*) {}
void gfcSequenceGUI_Qt::assignUnloadAndClearButton(void*) {}
void gfcSequenceGUI_Qt::assignMoreOptionsButton(void*) {}
void gfcSequenceGUI_Qt::assignChannelOptionsWidget(void*) {}
void gfcSequenceGUI_Qt::assignStartButtonWidget(void*) {}

int gfcSequenceGUI_Qt::widgetBelongsToMe(void*) { return 0; }

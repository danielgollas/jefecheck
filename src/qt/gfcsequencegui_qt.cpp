#include "gfcsequencegui_qt.h"

gfcSequenceGUI_Qt::gfcSequenceGUI_Qt() {}
gfcSequenceGUI_Qt::~gfcSequenceGUI_Qt() {}

std::string gfcSequenceGUI_Qt::getFilename()    { return {}; }
int   gfcSequenceGUI_Qt::getFrom()              { return 0; }
int   gfcSequenceGUI_Qt::getTo()                { return 0; }
float gfcSequenceGUI_Qt::getScale()             { return 1.0f; }
float gfcSequenceGUI_Qt::getGamma()             { return 1.0f; }
std::vector<int> gfcSequenceGUI_Qt::getLutList() { return {}; }
int   gfcSequenceGUI_Qt::getStream()            { return 0; }
int   gfcSequenceGUI_Qt::getCompression()       { return 0; }
int   gfcSequenceGUI_Qt::getFilter()            { return 0; }
int   gfcSequenceGUI_Qt::getAppendOption()      { return 0; }
int   gfcSequenceGUI_Qt::getCrop()              { return 0; }
int   gfcSequenceGUI_Qt::getWindowVisible()     { return 0; }
int   gfcSequenceGUI_Qt::getChannel()           { return 0; }
std::string gfcSequenceGUI_Qt::getChannelName() { return {}; }

void gfcSequenceGUI_Qt::setFromToBounds(int, int, bool) {}
void gfcSequenceGUI_Qt::setToFrame(int) {}
void gfcSequenceGUI_Qt::setFromFrame(int) {}
void gfcSequenceGUI_Qt::setFilename(std::string) {}
void gfcSequenceGUI_Qt::setScale(std::string) {}
void gfcSequenceGUI_Qt::setEstimates(std::string) {}
void gfcSequenceGUI_Qt::setAppendOption(int) {}
void gfcSequenceGUI_Qt::setFilter(int) {}
void gfcSequenceGUI_Qt::setCrop(int) {}
void gfcSequenceGUI_Qt::setCompression(int) {}
void gfcSequenceGUI_Qt::setChannelOptions(std::vector<std::string>) {}
void gfcSequenceGUI_Qt::setChannel(int) {}
void gfcSequenceGUI_Qt::setChannel(std::string) {}

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

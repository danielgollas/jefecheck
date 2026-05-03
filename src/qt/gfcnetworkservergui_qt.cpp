#include "gfcnetworkservergui_qt.h"

gfcNetworkServerGUI_Qt::gfcNetworkServerGUI_Qt() {}
gfcNetworkServerGUI_Qt::~gfcNetworkServerGUI_Qt() {}

void gfcNetworkServerGUI_Qt::assignNameInputWidget(void*) {}
void gfcNetworkServerGUI_Qt::assignPasswordWidget(void*) {}
void gfcNetworkServerGUI_Qt::assignIPOutputWidget(void*) {}
void gfcNetworkServerGUI_Qt::assignPortInputWidget(void*) {}
void gfcNetworkServerGUI_Qt::assignStartStopButtonWidget(void*) {}
void gfcNetworkServerGUI_Qt::assignStatusWidget(void*) {}

std::string gfcNetworkServerGUI_Qt::getName()      { return {}; }
std::string gfcNetworkServerGUI_Qt::getIPAddress() { return {}; }
int         gfcNetworkServerGUI_Qt::getPort()      { return 0; }
std::string gfcNetworkServerGUI_Qt::getPassword()  { return {}; }
void gfcNetworkServerGUI_Qt::setIPAddress(std::string) {}
void gfcNetworkServerGUI_Qt::setStartStopButton(std::string) {}
void gfcNetworkServerGUI_Qt::setStatus(std::string, int) {}

void gfcNetworkServerGUI_Qt::enable() {}
void gfcNetworkServerGUI_Qt::disable() {}

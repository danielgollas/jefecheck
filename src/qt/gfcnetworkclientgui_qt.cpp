#include "gfcnetworkclientgui_qt.h"

gfcNetworkClientGUI_Qt::gfcNetworkClientGUI_Qt() {}
gfcNetworkClientGUI_Qt::~gfcNetworkClientGUI_Qt() {}

void gfcNetworkClientGUI_Qt::assignNameInputWidget(void*) {}
void gfcNetworkClientGUI_Qt::assignPasswordWidget(void*) {}
void gfcNetworkClientGUI_Qt::assignIPInputWidget(void*) {}
void gfcNetworkClientGUI_Qt::assignPortInputWidget(void*) {}
void gfcNetworkClientGUI_Qt::assignStartStopButtonWidget(void*) {}
void gfcNetworkClientGUI_Qt::assignRemoteRecent(void*) {}
void gfcNetworkClientGUI_Qt::assignStatusWidget(void*) {}
void gfcNetworkClientGUI_Qt::assignPeersInSessionWidget(void*) {}

std::string gfcNetworkClientGUI_Qt::getName()      { return {}; }
std::string gfcNetworkClientGUI_Qt::getIPAddress() { return {}; }
int         gfcNetworkClientGUI_Qt::getPort()      { return 0; }
std::string gfcNetworkClientGUI_Qt::getPassword()  { return {}; }
void gfcNetworkClientGUI_Qt::setIPAddress(std::string) {}
void gfcNetworkClientGUI_Qt::setName(std::string) {}
void gfcNetworkClientGUI_Qt::setPort(int) {}
void gfcNetworkClientGUI_Qt::setStartStopButton(std::string) {}
void gfcNetworkClientGUI_Qt::setStatus(std::string, int) {}
void gfcNetworkClientGUI_Qt::setPeersInSession(std::vector<std::string>) {}
void gfcNetworkClientGUI_Qt::setRecent(std::vector<std::string>) {}
void gfcNetworkClientGUI_Qt::enable() {}
void gfcNetworkClientGUI_Qt::disable() {}

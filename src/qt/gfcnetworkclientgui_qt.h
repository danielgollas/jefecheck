#ifndef GFCNETWORKCLIENTGUI_QT_H
#define GFCNETWORKCLIENTGUI_QT_H

#include "gfcnetworkclientgui.h"

class gfcNetworkClientGUI_Qt : public gfcNetworkClientGUI {
public:
    gfcNetworkClientGUI_Qt();
    ~gfcNetworkClientGUI_Qt();

    void assignNameInputWidget(void*) override;
    void assignPasswordWidget(void*) override;
    void assignIPInputWidget(void*) override;
    void assignPortInputWidget(void*) override;
    void assignStartStopButtonWidget(void*) override;
    void assignRemoteRecent(void*) override;
    void assignStatusWidget(void*) override;
    void assignPeersInSessionWidget(void*) override;

    std::string getName() override;
    std::string getIPAddress() override;
    int getPort() override;
    std::string getPassword() override;
    void setIPAddress(std::string) override;
    void setName(std::string) override;
    void setPort(int) override;
    void setStartStopButton(std::string) override;
    void setStatus(std::string, int) override;
    void setPeersInSession(std::vector<std::string>) override;
    void setRecent(std::vector<std::string>) override;
    void enable() override;
    void disable() override;
};

#endif

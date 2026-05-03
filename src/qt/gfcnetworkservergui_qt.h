#ifndef GFCNETWORKSERVERGUI_QT_H
#define GFCNETWORKSERVERGUI_QT_H

#include "gfcnetworkservergui.h"

class gfcNetworkServerGUI_Qt : public gfcNetworkServerGUI {
public:
    gfcNetworkServerGUI_Qt();
    ~gfcNetworkServerGUI_Qt();

    void assignNameInputWidget(void*) override;
    void assignPasswordWidget(void*) override;
    void assignIPOutputWidget(void*) override;
    void assignPortInputWidget(void*) override;
    void assignStartStopButtonWidget(void*) override;
    void assignStatusWidget(void*) override;

    std::string getName() override;
    std::string getIPAddress() override;
    int getPort() override;
    std::string getPassword() override;
    void setIPAddress(std::string) override;
    void setStartStopButton(std::string) override;
    void setStatus(std::string, int) override;

    void enable() override;
    void disable() override;
};

#endif

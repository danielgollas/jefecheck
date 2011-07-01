#ifndef GFCNETWORKCLIENTGUI_H
#define GFCNETWORKCLIENTGUI_H

#include <string>
#include <vector>
/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcNetworkClientGUI{
public:
    gfcNetworkClientGUI();

    ~gfcNetworkClientGUI();
    
    virtual void assignNameInputWidget(void* widget)=0;
    virtual void assignPasswordWidget(void* widget)=0;
    virtual void assignIPInputWidget(void* widget)=0;
    virtual void assignPortInputWidget(void* widget)=0;
    virtual void assignStartStopButtonWidget(void* widget)=0;
    virtual void assignRemoteRecent(void* widget)=0;
    virtual void assignStatusWidget(void* widget)=0;
    virtual void assignPeersInSessionWidget(void* widget)=0;
    
    virtual std::string getName()=0;
    virtual std::string getIPAddress()=0;
    virtual int getPort()=0;
    virtual std::string getPassword()=0;
    virtual void setIPAddress(std::string paddress)=0;
    virtual void setName(std::string pname)=0;
    virtual void setPort(int pport)=0;
    virtual void setStartStopButton(std::string label)=0;
    virtual void setStatus(std::string, int color)=0;
    virtual void setPeersInSession(std::vector<std::string> peers)=0;
	virtual void setRecent(std::vector<std::string> recents)=0;
    virtual void enable()=0;
    virtual void disable()=0;

};

#endif

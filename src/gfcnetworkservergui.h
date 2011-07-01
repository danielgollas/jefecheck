#ifndef GFCNETWORKSERVERGUI_H
#define GFCNETWORKSERVERGUI_H

#include <string>

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcNetworkServerGUI{
public:
    gfcNetworkServerGUI();

    ~gfcNetworkServerGUI();

    virtual void assignNameInputWidget(void* widget)=0;
    virtual void assignPasswordWidget(void* widget)=0;
    virtual void assignIPOutputWidget(void* widget)=0;
    virtual void assignPortInputWidget(void* widget)=0;
    virtual void assignStartStopButtonWidget(void* widget)=0;
    virtual void assignStatusWidget(void* widget)=0;
    
    virtual std::string getName()=0;
    virtual std::string getIPAddress()=0;
    virtual int getPort()=0;
    virtual std::string getPassword()=0;
    virtual void setIPAddress(std::string address)=0;
    virtual void setStartStopButton(std::string label)=0;
     virtual void setStatus(std::string, int color)=0;
     
    virtual void enable()=0;
    virtual void disable()=0;

};

#endif

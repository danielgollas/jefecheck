#ifndef GFCNETWORKSERVERGUI_FLTK_H
#define GFCNETWORKSERVERGUI_FLTK_H

#include "gfcnetworkservergui.h"
#include <FL/Fl_Output.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Value_Input.H>
#include <FL/Fl_Button.H>

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcNetworkServerGUI_FLTK : public gfcNetworkServerGUI
{
public:
    gfcNetworkServerGUI_FLTK();

    ~gfcNetworkServerGUI_FLTK();

    virtual int getPort();
    virtual std::string getIPAddress();
    virtual std::string getName();
    virtual std::string getPassword();
    virtual void assignIPOutputWidget(void* widget);
    virtual void assignNameInputWidget(void* widget);
    virtual void assignPasswordWidget(void* widget);
    virtual void assignPortInputWidget(void* widget);
    virtual void assignStartStopButtonWidget(void* widget);
     virtual void assignStatusWidget(void* widget);
    
    virtual void setIPAddress(std::string address);
    virtual void setStartStopButton(std::string label);
    virtual void setStatus(std::string, int color);
    
    virtual void enable();
    virtual void disable();
  Fl_Input *serverName;
  Fl_Value_Input *serverPort;
  Fl_Input *serverPassword;
  Fl_Output *serverIP;
  Fl_Button *startStopButton;
  Fl_Output *status;



};

#endif

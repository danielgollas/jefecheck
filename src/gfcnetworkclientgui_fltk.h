#ifndef GFCNETWORKCLIENTGUI_FLTK_H
#define GFCNETWORKCLIENTGUI_FLTK_H

#include <vector>
#include <string>

#include "gfcnetworkclientgui.h"

#include <FL/Fl_Input.H>
#include <FL/Fl_Value_Input.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Check_Button.H>

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcNetworkClientGUI_FLTK : public gfcNetworkClientGUI
{
public:
    gfcNetworkClientGUI_FLTK();

    ~gfcNetworkClientGUI_FLTK();

    virtual int getPort();
    virtual std::string getIPAddress();
    virtual std::string getName();
    virtual std::string getPassword();
    virtual void assignIPInputWidget(void* widget);
    virtual void assignNameInputWidget(void* widget);
    virtual void assignPasswordWidget(void* widget);
    virtual void assignPortInputWidget(void* widget);
    virtual void assignStartStopButtonWidget(void* widget);
    virtual void assignStatusWidget(void* widget);
    virtual void assignPeersInSessionWidget(void* widget);
    
    
    virtual void setIPAddress(std::string paddress);
    virtual void assignRemoteRecent(void* widget);
    virtual void setName(std::string pname);
    virtual void setPort(int pport);
    virtual void setStartStopButton(std::string label);
    virtual void setStatus(std::string, int color);
    virtual void setPeersInSession(std::vector<std::string> peers);
    
	virtual void setRecent(std::vector<std::string> recents);
    
    virtual void enable();
    virtual void disable();
    
    Fl_Input *ip;
  Fl_Value_Input *port;
  Fl_Input *nickname;
  Fl_Button *connectButton;
  Fl_Input *password;
  Fl_Menu_Button *remoteRecent;
  Fl_Output* status;
  Fl_Browser* peersInSession; 

};

#endif

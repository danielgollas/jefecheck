#include "gfcnetworkclientgui_fltk.h"
#include "gfcNetworkStructures.h"
#include <vector>
gfcNetworkClientGUI_FLTK::gfcNetworkClientGUI_FLTK()
{
}


gfcNetworkClientGUI_FLTK::~gfcNetworkClientGUI_FLTK()
{
}


int gfcNetworkClientGUI_FLTK::getPort()
{
	return (int)port->value();
}

std::string gfcNetworkClientGUI_FLTK::getIPAddress()
{
	return this->ip->value();
}

std::string gfcNetworkClientGUI_FLTK::getName()
{
	return this->nickname->value();
}

std::string gfcNetworkClientGUI_FLTK::getPassword()
{
	return this->password->value();
}

void gfcNetworkClientGUI_FLTK::assignIPInputWidget(void* widget)
{
	this->ip=(Fl_Input*)widget;
}

void gfcNetworkClientGUI_FLTK::assignNameInputWidget(void* widget)
{
	this->nickname=(Fl_Input*)widget;
}

void gfcNetworkClientGUI_FLTK::assignPasswordWidget(void* widget)
{
	this->password=(Fl_Input*)widget;
}

void gfcNetworkClientGUI_FLTK::assignPortInputWidget(void* widget)
{
	this->port=(Fl_Value_Input*)widget;
}

void gfcNetworkClientGUI_FLTK::assignStartStopButtonWidget(void* widget)
{
	this->connectButton=(Fl_Button*)widget;
}

void gfcNetworkClientGUI_FLTK::setIPAddress(std::string paddress)
{
	this->ip->value(paddress.c_str());
}

void gfcNetworkClientGUI_FLTK::setName(std::string pname)
{
	this->nickname->value(pname.c_str());
}

void gfcNetworkClientGUI_FLTK::setPort(int pport)
{
	this->port->value((double)pport);
}

void gfcNetworkClientGUI_FLTK::setStartStopButton(std::string label)
{
	this->connectButton->copy_label(label.c_str());
}

void gfcNetworkClientGUI_FLTK::disable()
{
  ip->deactivate();
  port->deactivate();
  nickname->deactivate();
  connectButton->deactivate();
  password->deactivate();
  remoteRecent->deactivate();
}

void gfcNetworkClientGUI_FLTK::enable()
{
	
  ip->activate();
  port->activate();
  nickname->activate();
  connectButton->activate();
  password->activate();
  remoteRecent->activate();
}

void gfcNetworkClientGUI_FLTK::assignRemoteRecent(void * widget)
{
	this->remoteRecent=(Fl_Menu_Button*)widget;
}

void gfcNetworkClientGUI_FLTK::assignStatusWidget(void * widget)
{
	this->status=(Fl_Output*)widget;
}

void gfcNetworkClientGUI_FLTK::setRecent(std::vector<std::string> recents)
{
	this->remoteRecent->clear();
	for (int i=0; i<recents.size();i++)
	{
		remoteRecent->add(recents[i].c_str());
	}
}

void gfcNetworkClientGUI_FLTK::setStatus(std::string value, int pcolor)
{
	int color;
	switch( pcolor ){

		case GFCCOLOR_GREEN:
			color=fl_rgb_color(20,60,20);
		break;
		
		case GFCCOLOR_YELLOW:
			color=fl_rgb_color(60,60,20);
		break;

		case GFCCOLOR_RED:
			color=fl_rgb_color(60,20,20);
		break;
		
		case GFCCOLOR_GRAY:
			color=fl_rgb_color(32,32,32);
		break;
		
	}
	this->status->value(value.c_str());
	this->status->color(color);
	this->status->redraw();
}

void gfcNetworkClientGUI_FLTK::assignPeersInSessionWidget(void * widget)
{
	this->peersInSession=(Fl_Browser*)widget;
}

void gfcNetworkClientGUI_FLTK::setPeersInSession(std::vector< std::string > peers)
{
	peersInSession->clear();
	for(int i=0;i<peers.size();i++)
	{
		peersInSession->add(peers[i].c_str());
	}
	char tmp[60];
	
	sprintf(tmp,"Peers in Session (%i)",peers.size());
	peersInSession->copy_label(tmp);
	peersInSession->redraw();
	
}




#include "gfcnetworkservergui_fltk.h"
#include "gfcNetworkStructures.h"

gfcNetworkServerGUI_FLTK::gfcNetworkServerGUI_FLTK()
{
}


gfcNetworkServerGUI_FLTK::~gfcNetworkServerGUI_FLTK()
{
}


int gfcNetworkServerGUI_FLTK::getPort()
{
    return serverPort->value();
}

std::string gfcNetworkServerGUI_FLTK::getIPAddress()
{
    return serverIP->value();
}

std::string gfcNetworkServerGUI_FLTK::getName()
{
    return serverName->value();
}

std::string gfcNetworkServerGUI_FLTK::getPassword()
{
    return serverPassword->value();
}

void gfcNetworkServerGUI_FLTK::assignIPOutputWidget(void* widget)
{
	this->serverIP=(Fl_Output*)widget;
}

void gfcNetworkServerGUI_FLTK::assignNameInputWidget(void* widget)
{
	this->serverName=(Fl_Input*)widget;
}

void gfcNetworkServerGUI_FLTK::assignPasswordWidget(void* widget)
{
	this->serverPassword=(Fl_Input*)widget;
}

void gfcNetworkServerGUI_FLTK::assignPortInputWidget(void* widget)
{
	this->serverPort=(Fl_Value_Input*)widget;
}

void gfcNetworkServerGUI_FLTK::setIPAddress(std::string address)
{
	serverIP->value(address.c_str());
}

void gfcNetworkServerGUI_FLTK::assignStartStopButtonWidget(void * widget)
{
	startStopButton=(Fl_Button*)widget;
}

void gfcNetworkServerGUI_FLTK::setStartStopButton(std::string label)
{
	startStopButton->copy_label(label.c_str());
}

void gfcNetworkServerGUI_FLTK::assignStatusWidget(void * widget)
{
	this->status=(Fl_Output*)widget;
}

void gfcNetworkServerGUI_FLTK::setStatus(std::string value, int pcolor)
{
int color;
	switch( pcolor ){
		case GFCCOLOR_GREEN:
			color=FL_GREEN;
		break;
		
		case GFCCOLOR_RED:
			color=FL_RED;
		break;
		
		case GFCCOLOR_GRAY:
			color=FL_BLACK;
		break;
		
	}
	this->status->value(value.c_str());
	this->status->color(color);
	this->status->redraw();
}

void gfcNetworkServerGUI_FLTK::enable()
{
	this->serverPort->activate();
	this->serverName->activate();
	this->startStopButton->activate();
	this->serverIP->activate();
	this->serverPassword->activate();
}

void gfcNetworkServerGUI_FLTK::disable()
{
	
	this->serverPort->deactivate();
	this->serverName->deactivate();
	this->startStopButton->deactivate();
	this->serverIP->deactivate();
	this->serverPassword->deactivate();
}



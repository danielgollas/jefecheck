#include "gfcnetworklog.h"
#include "gfcStructures.h"
#include <iostream>
#include <sstream> //for stingstream
#include <string>

#ifdef JEFECHECK_USE_FLTK
#include "remoteWindow.h"
extern RemoteWindow rmw;
#endif

gfcNetworkLog networkLog;

#ifdef JEFECHECK_USE_FLTK
Fl_Text_Display::Style_Table_Entry stable[] = {
    // FONT COLOR      FONT FACE   FONT SIZE
    // --------------- ----------- --------------
    {  FL_WHITE, FL_HELVETICA, 12 }, // A - Normal
    {  FL_DARK_RED, FL_HELVETICA_BOLD, 12 }, // B - Alert
    {  FL_DARK_YELLOW,  FL_HELVETICA_ITALIC, 12 }, // C - Warning
};
#endif

gfcNetworkLog::gfcNetworkLog() {

	lineCount=0;
}


gfcNetworkLog::~gfcNetworkLog() {
}




void gfcNetworkLog::addToLog(std::string message, int type, int noDate) {
#ifdef JEFECHECK_USE_FLTK
    std::string completeMessage;
    if (!noDate) {
        completeMessage=asciiTime(false);
        completeMessage+=": ";
    }
    completeMessage+=message;
    completeMessage+="\n";
    display->insert(completeMessage.c_str());
	//printf("**ADDTOLOG: %s\n",completeMessage.c_str());
	for (int i=completeMessage.size()-1; i>=0;i--)
	{
		if (completeMessage[i]=='\n')
		{
			++lineCount;
		}
	}

	display->scroll(lineCount+1,0);

    std::stringstream ss (std::stringstream::in | std::stringstream::out);

    ss.fill(type);
    ss.width(completeMessage.size());
    ss << "";
    style_buffer.append(ss.str().c_str());
#else
    (void)message; (void)type; (void)noDate;
#endif
}

void gfcNetworkLog::initialize() {
#ifdef JEFECHECK_USE_FLTK
    display=rmw.log;
    display->buffer(buffer);
    //display->wrap_mode(1,80);
    //display->scrollbar_width(10);
    int stable_size = sizeof(stable)/sizeof(stable[0]);
    display->highlight_data(&style_buffer, stable, stable_size, 'A', 0, 0);
    std::string text="Connection Log initialized\n";
    this->addToLog(text,GFCNETLOGTYPE_NORMAL,0);
#endif
}

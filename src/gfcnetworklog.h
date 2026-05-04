#ifndef GFCNETWORKLOG_H
#define GFCNETWORKLOG_H

#include <stdio.h>
#include <string>
#include <vector>

#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/

enum gfcNetworkLogTypes{GFCNETLOGTYPE_NORMAL='A', GFCNETLOGTYPE_WARNING, GFCNETLOGTYPE_ALERT};

class gfcNetworkLog{
public:
    gfcNetworkLog();

    ~gfcNetworkLog();
    
    void initialize();
    void addToLog(std::string, int type=GFCNETLOGTYPE_NORMAL,int NoDate=0);
    void outputToFile(std::string fileName);
    bool writeToConsole;
    Fl_Text_Display* display;
    
private:
std::vector<std::string> log;
Fl_Text_Buffer buffer;
Fl_Text_Buffer style_buffer;
int lineCount; 
};

#endif

#ifndef GFCNETWORKLOG_H
#define GFCNETWORKLOG_H

#include <stdio.h>
#include <string>
#include <vector>


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
    std::vector<std::string> getLog() { return log; }
    bool writeToConsole;

private:
std::vector<std::string> log;
int lineCount;
};

#endif

#include "gfcnetworklog.h"
#include "gfcStructures.h"
#include <iostream>
#include <sstream> //for stingstream
#include <string>


gfcNetworkLog networkLog;


gfcNetworkLog::gfcNetworkLog() {

	lineCount=0;
}


gfcNetworkLog::~gfcNetworkLog() {
}




void gfcNetworkLog::addToLog(std::string message, int type, int noDate) {
    (void)message; (void)type; (void)noDate;
}

void gfcNetworkLog::initialize() {
}

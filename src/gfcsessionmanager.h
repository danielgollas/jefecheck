// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef GFCSESSIONMANAGER_H
#define GFCSESSIONMANAGER_H

#include <string>

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcSessionManager{
public:
    gfcSessionManager();

    ~gfcSessionManager();
    
    void saveSession(std::string filename);
    void loadSession(std::string filename);
    void writeCrashSession();
    bool checkCrashedSession();
    void loadCrashedSession();
    void removeCrashSession();
    const std::string& crashSessionPath() const { return crashSessionName; }
    
private:
    std::string crashSessionName;
    
};

void rebuildRecentSessionsMenu();

#endif

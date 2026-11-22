// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef GFCMEMORYMANAGER_H
#define GFCMEMORYMANAGER_H

#include <thread>
#include <mutex>
#include <cstdint>

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcMemoryManager{
public:
    gfcMemoryManager();

    ~gfcMemoryManager();

         
     void setLimit(const float percentage);
     float getLimit();
     
     float getFreeRAM();
     float getTotalRAM();
     int withinLimits();
     
     // System memory in bytes (lock + refresh). used excludes reclaimable cache.
     uint64_t usedBytes();
     uint64_t totalBytes();

     bool countInactive;
     
private:
	float percentageToUse;
	float freeRAM;
	float totalRAM;
	uint64_t usedB;
	uint64_t totalB;
	void updateData();
	std::mutex updateMutex;
	int m_withinLimits;
};

#endif

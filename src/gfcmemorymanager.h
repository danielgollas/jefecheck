#ifndef GFCMEMORYMANAGER_H
#define GFCMEMORYMANAGER_H

#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>

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
     
     bool countInactive;
     
private:
	float percentageToUse;
	float freeRAM;
	float totalRAM;
	void updateData();
	boost::mutex updateMutex;
	int m_withinLimits;
};

#endif

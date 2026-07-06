#ifndef GFCPOINTERSTORAGE_H
#define GFCPOINTERSTORAGE_H

#include "gfcNetworkStructures.h"
#ifdef WIN32 
#include <map> 
#else 
#include <map> 
#endif //for metadata
#include <deque>
#include <string.h>



/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcPointerStorage{ 
public:
    gfcPointerStorage();

    ~gfcPointerStorage();
    
    bool empty() const;
    void store(gfcNetRemotePointerInfo info);
    void removeFromMap(gfcNetRemotePointerInfo info);
    
    void updateFaders();
    
    int maxPointerStore; //this should be related directly to the preferences pointer trail
    float fadeDelay;
    std::map<std::string, std::deque<gfcNetRemotePointerInfo> > pointerMap;
    std::map<std::string, float> pointerHeadsFader; //this stores a fader for the head that starts deminishing when the size of
    // the corresponding deque in the pointer map goes to one, this will keep the head of the pointer alive for a little while longer 
    //after the trail fades.
    
private:
   float timeSinceLastRedraw; //this is the time since we last told the player to draw, we will use it to only redraw when a certain time has passed. This
   //should lower the cpu usage considerably. 
   float faderCount;
	
	
};

#endif

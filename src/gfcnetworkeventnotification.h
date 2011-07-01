#ifndef GFCNETWORKEVENTNOTIFICATION_H
#define GFCNETWORKEVENTNOTIFICATION_H

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcNetworkEventNotification{
public:
    gfcNetworkEventNotification();

    ~gfcNetworkEventNotification();

    bool readyForSend(float timeStep);
    void processed();
    
    	bool notified;
	float sendCounter;
	float delay;
};

#endif

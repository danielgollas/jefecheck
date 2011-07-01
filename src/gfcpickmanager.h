#ifndef GFCPICKMANAGER_H
#define GFCPICKMANAGER_H

#include <vector>
#include "gfcpicknotifee.h"
#include "gfcpickdrawee.h"
/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/

class gfcPickManager{

private:

	std::vector<gfcPickNotifee*> notifees;
	std::vector<gfcPickDrawee*> drawees;
	
	void callDrawees();
	void callNotifees();

public:
    gfcPickManager();

    ~gfcPickManager();
    
    int doPicking(gfcPickEvent event, unsigned int flags, int x, int y, int dx=0, int dy=0);
    
    void registerNotifee(gfcPickNotifee* notifee);
    void unregisterNotifee(gfcPickNotifee* notifee);
    
    void registerDrawee(gfcPickDrawee* drawee);
    void unregisterDrawee(gfcPickDrawee* drawee);
    
	static gfcPickColor currentColor;
    static gfcPickColor getUniqueColor();
	/*
	static unsigned char uniqueR;
	static unsigned char uniqueG;
	static unsigned char uniqueB;*/


    
    
};


#endif

#include "gfcnetworkeventnotification.h"

gfcNetworkEventNotification::gfcNetworkEventNotification()
{
	notified=false;
	delay=1.0/60.0;
	sendCounter=delay;
}


gfcNetworkEventNotification::~gfcNetworkEventNotification()
{
}

bool gfcNetworkEventNotification::readyForSend(float timeStep)
{
	sendCounter-=timeStep;
	return (notified && sendCounter<=0);
}

void gfcNetworkEventNotification::processed()
{
	notified=false;
	sendCounter=delay;
}



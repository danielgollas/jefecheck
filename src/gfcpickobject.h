#ifndef GFCPICKOBJECT_H
#define GFCPICKOBJECT_H

#include "gfcpickmanager.h"
#include "gfcpickdrawee.h"
#include "gfcpicknotifee.h"

class gfcPickObjectStatus
{
public:
	gfcPickObjectStatus();

	int x;
	int y;
	int dx;
	int dy;
	gfcPickEvent event;
	unsigned int flags;
	gfcPickColor uniqueColor;
	int clicked;
	
	gfcPickObjectStatus& operator=(const gfcPickObjectStatus &status);
	
	
};

class gfcPickObject
{
public:
	gfcPickObject(void);
public:
	~gfcPickObject(void);
	
	void initialize();
	gfcPickObjectStatus getStatus(gfcPickNotifyParameters &params);
	gfcPickObjectStatus getStatus();
	gfcPickColor getPickColor();
	void getPickColor(unsigned char result[3]);
private:
	gfcPickObjectStatus status;
	bool notFirstRun;

};

#endif

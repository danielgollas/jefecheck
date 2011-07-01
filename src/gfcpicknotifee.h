#ifndef GFCPICLNOTIFEE_H
#define GFCPICLNOTIFEE_H


#include <string.h>
#include <vector>

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/

class gfcPickColor{
public:
	gfcPickColor();
	gfcPickColor(unsigned char tmpColor[3]);

	gfcPickColor &operator=(const gfcPickColor &aColor);

	bool operator==(const gfcPickColor &aColor){
		return (colors[0]==aColor.colors[0] && colors[1]==aColor.colors[1] && colors[2]==aColor.colors[2]);
	}

	//std::vector<unsigned char> colors;
	unsigned char colors[3];
};

enum gfcPickEvent{
GFC_PICK_EVENT_CLICK_DOWN=0,
GFC_PICK_EVENT_CLICK_UP,
GFC_PICK_EVENT_DRAG, 
GFC_PICK_EVENT_MOVE, 
GFC_PICK_EVENT_WHEEL_UP, 
GFC_PICK_EVENT_WHEEL_DOWN
};

enum gfcPickModifierFlags{
GFC_PICK_MODIFIER_ALT=1,
GFC_PICK_MODIFIER_CTRL=2,
GFC_PICK_MODIFIER_SHIFT=4,
GFC_PICK_MODIFIER_BUTTON1=8, //left
GFC_PICK_MODIFIER_BUTTON2=16, //middle
GFC_PICK_MODIFIER_BUTTON3=32, //right
};

class gfcPickNotifyParameters{

public:

gfcPickNotifyParameters();
gfcPickNotifyParameters(int px, int py, int pdx, int pdy, gfcPickEvent pevent, gfcPickColor ppickedColor, unsigned int pflags): x(px), y(py), dx(pdx), dy(pdy), event(pevent), pickedColor(ppickedColor), flags(pflags)
{};

~gfcPickNotifyParameters(){};

int x;
int y;

int dx;
int dy;

gfcPickEvent event;
unsigned int flags;

gfcPickColor pickedColor;

};

class gfcPickNotifee{
public:
    gfcPickNotifee(){};

    ~gfcPickNotifee(){};
    
    virtual int pickNotify(gfcPickNotifyParameters &params)=0;

};

#endif

#pragma once
#include "Fl_Button_gfc.h"


class Fl_Button_RGBA_gfc :
	public Fl_Button_gfc
{
	
	int handle(int event);
	int currentValue;

public:

	Fl_Button_RGBA_gfc(int X,int Y,int W,int H,const char* l=0);

	enum Fl_Button_RGBA_Value{VALUE_RGB,VALUE_R,VALUE_G,VALUE_B,VALUE_A};


	int getCurrentValue();
	void setCurrentValue(int value);
	
};

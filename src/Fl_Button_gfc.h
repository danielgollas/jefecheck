#pragma once
#include <FL/Fl_Button.H>

class Fl_Button_gfc :
	public Fl_Button
{
public:
	Fl_Button_gfc(int,int,int,int,const char * = 0);
public:
	void draw();
};

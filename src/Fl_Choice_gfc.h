#pragma once
#include <FL/Fl_Choice.H>

class Fl_Choice_gfc :
	public Fl_Choice
{
protected:
	void draw();
	int handle(int);
public:
	Fl_Choice_gfc(int,int,int,int,const char * = 0);

public:
	
};

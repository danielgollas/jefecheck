#pragma once
#include <FL/Fl_Widget.H>

class gfcFXControlBase :
	public Fl_Widget
{
public:
	gfcFXControlBase(int x, int y, int w, int h, char *L="");
public:
	~gfcFXControlBase(void);
};

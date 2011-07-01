#pragma once
#include <Fl/Fl_Slider.H>
#include "gfcrectang.h"

class Fl_Slider_Timeline_gfc :
	public Fl_Slider
{
	
	void draw_my_bg(int X, int Y, int W, int H);
	float mySlider_size_;
	int inPoint;
	int outPoint;
	
	int eventInsideInPoint();
	int eventInsideOutPoint();
	
	void handlePushForInOutPoints();
	void handleReleaseForInOutPoints();

	gfcRectang inPointBox;
	int inPointClicked;
	gfcRectang outPointBox;
	int outPointClicked;
	
	void handle_event_InOutPoints(); //this check weather to do the callback or not.
	

	//

	int handle(int event, int X, int Y, int W, int H);
protected:
	int handle(int event);
public:
	Fl_Slider_Timeline_gfc(int x,int y,int w,int h, const char *l = 0);
	//~Fl_Slider_Timeline_gfc(void);
	
	void setInPoint(int p);
	void setOutPoint(int p);

	int getInPoint();
	int getOutPoint();

	void draw(int, int, int, int);
	void draw();

};


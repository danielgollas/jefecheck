#include "Fl_Slider_Timeline_gfc.h"

#include <FL/Fl.H>
#include <FL/Fl_Slider.H>
#include <FL/fl_draw.H>
#include <math.h>
#include <string>

Fl_Slider_Timeline_gfc::Fl_Slider_Timeline_gfc(int x,int y,int w,int h, const char *l):Fl_Slider(x,y,w,h,l)
{
	inPoint=1;
	outPoint=100;

	inPointClicked=0;
	outPointClicked=0;

	inPointBox.set(0,0,0,0);
	outPointBox.set(0,0,0,0);
}

/*Fl_Slider_Timeline_gfc::~Fl_Slider_Timeline_gfc(void)
{
}*/

void Fl_Slider_Timeline_gfc::draw_my_bg(int X, int Y, int W, int H) {
//	fl_push_clip(X, Y, W, H);
	draw_box();
//	fl_pop_clip();

	/*Fl_Color black = active_r() ? FL_FOREGROUND_COLOR : FL_INACTIVE_COLOR;
	if (type() == FL_VERT_NICE_SLIDER) {
		draw_box(FL_THIN_DOWN_BOX, X+W/2-2, Y, 4, H, black);
	} else if (type() == FL_HOR_NICE_SLIDER) {
		draw_box(FL_THIN_DOWN_BOX, X, Y+H/2-2, W, 4, black);
	}*/
}

void Fl_Slider_Timeline_gfc::draw(int X, int Y, int W, int H) {
	
	color(fl_rgb_color(60,60,60));
	double val;
	double inVal,outVal;
	if (minimum() == maximum())
		val = inVal=outVal=0.5;
		
	else {
		val = (value()-minimum())/(maximum()-minimum());
		if (val > 1.0) val = 1.0;
		else if (val < 0.0) val = 0.0;
		
		inVal = (inPoint-minimum())/(maximum()-minimum());
		if (inVal > 1.0) inVal = 1.0;
		else if (inVal < 0.0) inVal = 0.0;

		outVal = (outPoint-minimum())/(maximum()-minimum());
		if (outVal > 1.0) outVal = 1.0;
		else if (outVal < 0.0) outVal = 0.0;
	}

	int ww = (horizontal() ? W : H);
	int xx, S;
	int xxIn, xxOut;
	/*if (type()==FL_HOR_FILL_SLIDER || type() == FL_VERT_FILL_SLIDER) {
		S = int(val*ww+.5);
		if (minimum()>maximum()) {S = ww-S; xx = ww-S;}
		else xx = 0;
	} else */{
		S = int(mySlider_size_*ww+.5);
		int T = (horizontal() ? H : W)/2+1;
		if (S < T) S = T;
		xx = int(val*(ww-S)+.5);
		xxIn = int(inVal*(ww-S)+.5);
		xxOut = int(outVal*(ww-S)+.5);

	}
	int xsl, ysl, wsl, hsl;
	int xIn, xOut;
	if (horizontal()) {
		xsl = X+xx;
		xIn = X+xxIn;
		xOut= X+xxOut;
		wsl = S;
		ysl = Y;
		hsl = H;
	} else {
		ysl = Y+xx;
		hsl = S;
		xsl = X;
		wsl = W;
	}
	fl_push_clip(X, Y, W, H);
	
	
	draw_my_bg(X, Y, W, H);
	if (wsl%2!=1)
	{ //make the slider an even number so we can insert the line right in the middle
		wsl++;
	}
	//draw the slider
	Fl_Boxtype box1 = slider();
	if (!box1) {box1 = (Fl_Boxtype)(box()&-2); if (!box1) box1 = FL_FLAT_BOX;}
	if (wsl>0 && hsl>0){
		draw_box(box1, xsl, ysl, wsl, hsl, selection_color());
		Fl_Color prevcolor=fl_color();
		fl_color(fl_rgb_color(42,42,42));
		fl_line(xsl+wsl/2,ysl,xsl+wsl/2,ysl+hsl);
		
		//draw in and out points as triangles the size of the slider
		int inColor=120;
		int outColor=120;
		
		
		//in point

		/*
		
		*3
  
                *2 <---xIn,ysl/2+hsl/2

		* 1


		*/

		//save the bounding box to do click tests later
		inPointBox.set(xIn-wsl, ysl+1, (xIn+wsl/2)-(xIn-wsl),(ysl+hsl-1)-ysl+1);
		//draw_box(FL_FLAT_BOX,inPointBox.x,inPointBox.y,inPointBox.w,inPointBox.h,FL_RED);

		fl_color(fl_rgb_color(inColor,inColor,inColor));
		fl_line(xIn+wsl/2,ysl,xIn+wsl/2,ysl+hsl);
		fl_polygon(xIn-wsl,ysl+1, xIn+wsl/2,ysl+hsl/2,xIn-wsl,ysl+hsl-1);		
		
	
		
		//out point
		/*
		
								3*

	xOut,ysl/2+hsl/2-->	2* 

								1*

		*/

		outPointBox.set(xOut+wsl/2,ysl+1, (xOut+wsl*2)-(xOut+wsl/2), hsl );
		//draw_box(FL_FLAT_BOX,outPointBox.x,outPointBox.y,outPointBox.w,outPointBox.h,FL_RED);


		fl_color(fl_rgb_color(outColor,outColor,outColor));

		fl_line(xOut+wsl/2,ysl,xOut+wsl/2,ysl+hsl);

		fl_polygon(xOut+wsl*2, ysl+1,    xOut+wsl/2,ysl+hsl/2,   xOut+wsl*2, ysl+hsl-1);
		
		fl_color(prevcolor);
	}

	
	

		

	/*Fl_Boxtype box1 = slider();
	

	
	if (!box1) {box1 = (Fl_Boxtype)(box()&-2); if (!box1) box1 = FL_FLAT_BOX;}
	if (type() == FL_HOR_NICE_SLIDER) {
		draw_box(box1, xsl, ysl, wsl, hsl, FL_GRAY);
		int d = (wsl-4)/2;
		draw_box(FL_THIN_DOWN_BOX, xsl+d, ysl+2, wsl-2*d, hsl-4,selection_color());
	} else 
	{
		

		/*if (type()!=FL_HOR_FILL_SLIDER && type() != FL_VERT_FILL_SLIDER &&
			Fl::scheme_ && !strcmp(Fl::scheme_, "gtk+")) {
				if (W>H && wsl>(hsl+8)) {
					// Draw horizontal grippers
					int yy, hh;
					hh = hsl-8;
					xx = xsl+(wsl-hsl-4)/2;
					yy = ysl+3;

					fl_color(fl_darker(selection_color()));
					fl_line(xx, yy+hh, xx+hh, yy);
					fl_line(xx+6, yy+hh, xx+hh+6, yy);
					fl_line(xx+12, yy+hh, xx+hh+12, yy);

					xx++;
					fl_color(fl_lighter(selection_color()));
					fl_line(xx, yy+hh, xx+hh, yy);
					fl_line(xx+6, yy+hh, xx+hh+6, yy);
					fl_line(xx+12, yy+hh, xx+hh+12, yy);
				} else if (H>W && hsl>(wsl+8)) {
					// Draw vertical grippers
					int yy;
					xx = xsl+4;
					ww = wsl-8;
					yy = ysl+(hsl-wsl-4)/2;

					fl_color(fl_darker(selection_color()));
					fl_line(xx, yy+ww, xx+ww, yy);
					fl_line(xx, yy+ww+6, xx+ww, yy+6);
					fl_line(xx, yy+ww+12, xx+ww, yy+12);

					yy++;
					fl_color(fl_lighter(selection_color()));
					fl_line(xx, yy+ww, xx+ww, yy);
					fl_line(xx, yy+ww+6, xx+ww, yy+6);
					fl_line(xx, yy+ww+12, xx+ww, yy+12);
				}
		}
	}
	*/

	draw_label(xsl, ysl, wsl, hsl);
	if (Fl::focus() == this) {
		if (type() == FL_HOR_FILL_SLIDER || type() == FL_VERT_FILL_SLIDER) draw_focus();
		else draw_focus(box1, xsl, ysl, wsl, hsl);
	}

	fl_pop_clip();

}

void Fl_Slider_Timeline_gfc::draw() {
	if (damage()&FL_DAMAGE_ALL) draw_box();
	draw(x()+Fl::box_dx(box()),
		y()+Fl::box_dy(box()),
		w()-Fl::box_dw(box()),
		h()-Fl::box_dh(box()));
}

void Fl_Slider_Timeline_gfc::setInPoint(int p)
{
	inPoint=p;
}

void Fl_Slider_Timeline_gfc::setOutPoint(int p)
{
	outPoint=p;
}

int Fl_Slider_Timeline_gfc::getOutPoint()
{
	return outPoint;
}

int Fl_Slider_Timeline_gfc::getInPoint()
{
	return inPoint;
}

void Fl_Slider_Timeline_gfc::handle_event_InOutPoints(){ //this check weather to do the callback or not.
if (when() & FL_WHEN_CHANGED) do_callback();
}

int Fl_Slider_Timeline_gfc::handle(int event, int X, int Y, int W, int H) {
	switch (event) {
  case FL_PUSH:
	  if (!Fl::event_inside(X, Y, W, H)) return 0;

	  //should check if we hit an in or out point and turn on a flag
	  if (eventInsideInPoint())
	  {
			//printf("event inside in point\n");
			inPointClicked=1;
			outPointClicked=0;
			handle_event_InOutPoints();
				return 1;
	  }
	  else
	  {
		  if (eventInsideOutPoint())
		  {
				//printf("event inside out point\n");
				inPointClicked=0;
				outPointClicked=1;
				handle_event_InOutPoints();
			  return 1;
		  }
	  }
	  		
	  handle_push();

	  
		
  case FL_DRAG: {

	  


	  double val;
	  if (minimum() == maximum())
		  val = 0.5;
	  else {
		  val = (value()-minimum())/(maximum()-minimum());
		  if (val > 1.0) val = 1.0;
		  else if (val < 0.0) val = 0.0;
	  }

	  int ww = (horizontal() ? W : H);
	  int mx = (horizontal() ? Fl::event_x()-X : Fl::event_y()-Y);
	  int S;
	  static int offcenter;

	 {

		  //S = int(slider_size_*ww+.5); if (S >= ww) return 0;
		  S = int(mySlider_size_*ww+.5); if (S >= ww) return 0;
		  int T = (horizontal() ? H : W)/2+1;
		  
		  if (S < T) S = T;
		  if (event == FL_PUSH) {
			  int xx = int(val*(ww-S)+.5);
			  offcenter = mx-xx;
			  if (offcenter < 0) offcenter = 0;
			  else if (offcenter > S) offcenter = S;
			  else return 1;
		  }
	  }

	  int xx = mx-offcenter;
	  double v;
	  char tryAgain = 1;
	  while (tryAgain)
	  {
		  tryAgain = 0;
		  if (xx < 0) {
			  xx = 0;
			  offcenter = mx; if (offcenter < 0) offcenter = 0;
		  } else if (xx > (ww-S)) {
			  xx = ww-S;
			  offcenter = mx-xx; if (offcenter > S) offcenter = S;
		  }
		  v = round(xx*(maximum()-minimum())/(ww-S) + minimum());
		  // make sure a click outside the sliderbar moves it:
		  if (event == FL_PUSH && v == value()) {
			  offcenter = S/2;
			  event = FL_DRAG;
			  tryAgain = 1;
		  }
	  }

	  //if the in our out points have been hit, then drag them, not the slider.
	  
	  if (inPointClicked)
	  {
		  //printf("movingInPoint to: %f\n",v);
		  setInPoint((v));
		  
		  handle_event_InOutPoints(); //this check weather to do the callback or not.
		   redraw();
		  return 1;
	  }
	  else
	  {
		  if (outPointClicked)
		  {
			  //printf("movingOutPoint to: %f\n",v);
			  setOutPoint((v));
			  
			  handle_event_InOutPoints();
			  redraw();
			  return 1;
		  }
	  }

	  handle_drag(clamp(v));
				} return 1;
  case FL_RELEASE:
	  inPointClicked=0;
	  outPointClicked=0;
	  handle_release();
	  return 1;
  case FL_KEYBOARD :
	  switch (Fl::event_key()) {
  case FL_Up:
	  if (horizontal()) return 0;
	  handle_push();
	  handle_drag(clamp(increment(value(),-1)));
	  handle_release();
	  return 1;
  case FL_Down:
	  if (horizontal()) return 0;
	  handle_push();
	  handle_drag(clamp(increment(value(),1)));
	  handle_release();
	  return 1;
  case FL_Left:
	  if (!horizontal()) return 0;
	  handle_push();
	  handle_drag(clamp(increment(value(),-1)));
	  handle_release();
	  return 1;
  case FL_Right:
	  if (!horizontal()) return 0;
	  handle_push();
	  handle_drag(clamp(increment(value(),1)));
	  handle_release();
	  return 1;
  default:
	  return 0;
	  }
	  // break not required because of switch...
  case FL_FOCUS :
  case FL_UNFOCUS :
	  if (Fl::visible_focus()) {
		  redraw();
		  return 1;
	  } else return 0;
  case FL_ENTER :
  case FL_LEAVE :
	  return 1;
  default:
	  return 0;
	}
}

int Fl_Slider_Timeline_gfc::handle(int event) {
	if (event == FL_PUSH && Fl::visible_focus()) {
		Fl::focus(this);
		redraw();
	}

	return handle(event,
		x()+Fl::box_dx(box()),
		y()+Fl::box_dy(box()),
		w()-Fl::box_dw(box()),
		h()-Fl::box_dh(box()));
}

int Fl_Slider_Timeline_gfc::eventInsideInPoint(){

    return Fl::event_inside(inPointBox.x,inPointBox.y,inPointBox.w,inPointBox.h);
}
int Fl_Slider_Timeline_gfc::eventInsideOutPoint(){
	    return Fl::event_inside(outPointBox.x,outPointBox.y,outPointBox.w,outPointBox.h);
}

void Fl_Slider_Timeline_gfc::handlePushForInOutPoints(){

}
void Fl_Slider_Timeline_gfc::handleReleaseForInOutPoints(){

}
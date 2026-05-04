#include "Fl_Button_RGBA_gfc.h"

#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Window.H>
#include <stdio.h>

Fl_Button_RGBA_gfc::Fl_Button_RGBA_gfc(int X,int Y,int W,int H,const char *l):Fl_Button_gfc(X,Y,W,H,l)
{
	currentValue=VALUE_RGB;
}

int Fl_Button_RGBA_gfc::getCurrentValue()
{
	return currentValue;
}

void Fl_Button_RGBA_gfc::setCurrentValue(int value)
{

	currentValue=value;

	switch(currentValue)
	{
	case VALUE_RGB:
		this->copy_label("RGB");
		//printf("Label set to RGB\n");
		break;

	case VALUE_R:
		this->copy_label("R");
		//printf("Label set to R\n");
		break;

	case VALUE_G:
		this->copy_label("G");
		//printf("Label set to G\n");
		break;

	case VALUE_B:
		this->copy_label("B");
		//printf("Label set to B\n");
		break;

	case VALUE_A:
		this->copy_label("A");
		//printf("Label set to A\n");
		break;
	}

}


int Fl_Button_RGBA_gfc::handle(int event) {
	int newval;
	switch (event) {
  
  case FL_RELEASE:
	  
	  {
	  currentValue++;
	  currentValue=currentValue%5; //loop back
	  setCurrentValue(currentValue);
	  }

	
	}

	return Fl_Button_gfc::handle(event);
}

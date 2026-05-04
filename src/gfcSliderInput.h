#ifndef GFCSLIDERINPUT_H
#define GFCSLIDERINPUT_H

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Slider.H>
#include <stdio.h>

//Class provided by Greg Ercolano's cheat sheet.

// sliderinput -- simple example of tying an fltk slider and input widget together
// 1.00 erco 10/17/04

class gfcSliderInput : public Fl_Group {
	Fl_Int_Input *input;
	Fl_Slider    *slider;

	// CALLBACK HANDLERS
	//    These 'attach' the input and slider's values together.
	//
	void Slider_CB2();

	static void Slider_CB(Fl_Widget *w, void *data);
	void Input_CB2();
	static void Input_CB(Fl_Widget *w, void *data);

public:
	// CTOR
	gfcSliderInput(int x, int y, int w, int h, const char *l=0);

	// MINIMAL ACCESSORS --  Add your own as needed
	int value();
	void value(int val);
	void minumum(int val);
	void step(float val);
	void textsize(int val);
	int minumum();
	void maximum(int val);
	int maximum();
	void bounds(int low, int high);
	void range(int low, int high);
};

#endif
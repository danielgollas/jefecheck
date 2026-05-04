#include "gfcSliderInput.h"

// CALLBACK HANDLERS
//    These 'attach' the input and slider's values together.
//

gfcSliderInput::gfcSliderInput(int x, int y, int w, int h, const char *l) : Fl_Group(x,y,w,h,l) {
	int in_w = 55;
	int in_h = h;

	input  = new Fl_Int_Input(x, y, in_w, in_h);
	input->callback(Input_CB, (void*)this);
	input->when(FL_WHEN_ENTER_KEY|FL_WHEN_NOT_CHANGED);
	
	input->color(fl_rgb_color(42,42,42));
	input->textcolor(fl_rgb_color(85,85,85));
	input->box(FL_FLAT_BOX);

	slider = new Fl_Slider(x+in_w, y, w - in_w, in_h);
	slider->type(1);
	slider->callback(Slider_CB, (void*)this);
	slider->box(FL_FLAT_BOX);
	slider->color(fl_rgb_color(50,50,50));
	slider->slider(FL_FLAT_BOX);
	slider->color2(fl_rgb_color(85,85,85));
		
	
	bounds(1, 1);          // some usable default
	value(1);               // some usable default
	end();			// close the group
}

void gfcSliderInput::Slider_CB2() {
	static int recurse = 0;
	if ( recurse ) { 
		return;
	} else {
		recurse = 1;
		char s[80];
		sprintf(s, "%d", (int)(slider->value() + .5));
		// fprintf(stderr, "SPRINTF(%d) -> '%s'\n", (int)slider->value(), s);
		input->value(s);    // pass slider's value to input
		recurse = 0;
	}
}

void gfcSliderInput::Slider_CB(Fl_Widget *w, void *data) {
	((gfcSliderInput*)data)->
		Slider_CB2();
}

void gfcSliderInput::Input_CB2() {
	static int recurse = 0;
	if ( recurse ) {
		return;
	} else {
		recurse = 1;
		int val = 0;
		if ( sscanf(input->value(), "%d", &val) != 1 ) {
			val = 0;
		}
		// fprintf(stderr, "SCANF('%s') -> %d\n", input->value(), val);
		slider->value(val);         // pass input's value to slider
		recurse = 0;
	}
}
void gfcSliderInput::Input_CB(Fl_Widget *w, void *data) {
	((gfcSliderInput*)data)->
		Input_CB2();
}

int gfcSliderInput::value() { return((int)(slider->value() + 0.5)); }
void gfcSliderInput::value(int val) {
	slider->value(val); 
	Slider_CB2(); 
}
void gfcSliderInput::minumum(int val) { slider->minimum(val); }
void gfcSliderInput::step(float val) { slider->step(val);}
void gfcSliderInput::textsize(int val) { input->textsize(val); }
int gfcSliderInput::minumum() { return((int)slider->minimum()); }
void gfcSliderInput::maximum(int val) { slider->maximum(val); }
int gfcSliderInput::maximum() { return((int)slider->maximum()); }
void gfcSliderInput::bounds(int low, int high) { slider->bounds(low, high); }
void gfcSliderInput::range(int low, int high)
{
		bounds(low, high);
}
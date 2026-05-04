#include "Fl_Input_Choice_gfc.h"
#include "UIConstants.h"

void InputMenuButton_gfc:: draw() {
	draw_box(FL_BORDER_FRAME, color());
	fl_color(active_r() ? labelcolor() : fl_inactive(labelcolor()));
	int xc = x()+w()/2, yc=y()+h()/2;
	fl_polygon(xc-5,yc-3,xc+5,yc-3,xc,yc+3);
	if (Fl::focus() == this) draw_focus();
}

Fl_Input_Choice_gfc::Fl_Input_Choice_gfc (int x,int y,int w,int h,const char*l) : Fl_Group(x,y,w,h,l) {
	Fl_Group::box(FL_FLAT_BOX);
	align(FL_ALIGN_LEFT);				// default like Fl_Input
	inp_ = new Fl_Input(inp_x(), inp_y(),
		inp_w(), inp_h());
	inp_->callback(inp_cb, (void*)this);
	inp_->box(FL_FLAT_BOX);		// cosmetic
	inp_->color(fl_rgb_color(42,42,42));

	menu_ = new InputMenuButton_gfc(menu_x(), menu_y(),
		menu_w(), menu_h());
	menu_->callback(menu_cb, (void*)this);
	menu_->box(FL_FLAT_BOX);
	menu_->color(fl_rgb_color(32,32,32));// cosmetic
	end();
}

void Fl_Input_Choice_gfc::draw(){
	
	inp_->textcolor(fl_rgb_color(GFC_WIDGET_LIGHT_TEXT_COLOR,GFC_WIDGET_LIGHT_TEXT_COLOR,GFC_WIDGET_LIGHT_TEXT_COLOR));
	inp_->selection_color(FL_BLACK);
	inp_->color(fl_rgb_color(GFC_WIDGET_COLOR,GFC_WIDGET_COLOR,GFC_WIDGET_COLOR));
	inp_->color(fl_rgb_color(GFC_WIDGET_COLOR,GFC_WIDGET_COLOR,GFC_WIDGET_COLOR));
	inp_->box(FL_FLAT_BOX);

	
	menu_->color(fl_rgb_color(42,42,42));// cosmetic
	menubutton()->textfont(FL_HELVETICA);
	menu_->labelcolor(fl_rgb_color(GFC_WIDGET_DARK_TEXT_COLOR,GFC_WIDGET_DARK_TEXT_COLOR,GFC_WIDGET_DARK_TEXT_COLOR));
	menu_->textcolor(fl_rgb_color(GFC_WIDGET_LIGHT_TEXT_COLOR,GFC_WIDGET_LIGHT_TEXT_COLOR,GFC_WIDGET_LIGHT_TEXT_COLOR));
	menu_->box(FL_FLAT_BOX);
	menu_->down_box(FL_FLAT_BOX);
	menu_->down_color(fl_rgb_color(GFC_WIDGET_COLOR,GFC_WIDGET_COLOR,GFC_WIDGET_COLOR));// cosmetic
	this->labelfont(FL_HELVETICA);
	//this->labelsize(12);

	labelcolor(fl_rgb_color(GFC_WIDGET_DARK_TEXT_COLOR,GFC_WIDGET_DARK_TEXT_COLOR,GFC_WIDGET_DARK_TEXT_COLOR));
	
	//We need to do this workaround to draw the internal widgets because draw is protected
	Fl_Widget *inp2=inp_;
	inp2->draw();
	Fl_Widget *menu2=menu_;
	menu2->draw();
	//inp_->draw();
	//menu_->redraw();
	
}

#ifndef Fl_Input_Choice_gfc_H
#define Fl_Input_Choice_gfc_H

#include <FL/Fl.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/fl_draw.H>

// Private class to handle slightly 'special' behavior of menu button
class InputMenuButton_gfc : public Fl_Menu_Button {
public:
	void draw();
public:
	InputMenuButton_gfc(int x,int y,int w,int h,const char*l=0) : 
	  Fl_Menu_Button(x,y,w,h,l) { box(FL_UP_BOX); }
};

class Fl_Input_Choice_gfc : public Fl_Group {
	
	void draw();
	Fl_Input *inp_;
	InputMenuButton_gfc *menu_;

	static void menu_cb(Fl_Widget*, void *data) { 
		Fl_Input_Choice_gfc *o=(Fl_Input_Choice_gfc *)data;
		const Fl_Menu_Item *item = o->menubutton()->mvalue();
		if ( item && item->flags & (FL_SUBMENU|FL_SUBMENU_POINTER) ) return;	// ignore submenus
		o->inp_->value(o->menu_->text());
		o->inp_->set_changed();
		o->do_callback();
	}

	static void inp_cb(Fl_Widget*, void *data) { 
		Fl_Input_Choice_gfc *o=(Fl_Input_Choice_gfc *)data;
		if (o->inp_->changed()) 
			o->Fl_Widget::set_changed();
		else
			o->Fl_Widget::clear_changed();
		o->do_callback();
		if (o->callback() != default_callback)
			o->Fl_Widget::clear_changed();
	}

	// Custom resize behavior -- input stretches, menu button doesn't
	inline int inp_x() { return(x() + Fl::box_dx(box())); }
	inline int inp_y() { return(y() + Fl::box_dy(box())); }
	inline int inp_w() { return(w() - Fl::box_dw(box()) - 20); }
	inline int inp_h() { return(h() - Fl::box_dh(box())); }

	inline int menu_x() { return(x() + w() - 20 - Fl::box_dx(box())); }
	inline int menu_y() { return(y() + Fl::box_dy(box())); }
	inline int menu_w() { return(20); }
	inline int menu_h() { return(h() - Fl::box_dh(box())); }

public:
	Fl_Input_Choice_gfc (int x,int y,int w,int h,const char*l=0);
	void add(const char *s) {
		menu_->add(s);
	}
	int changed() const { 
		return inp_->changed() | Fl_Widget::changed();
	}
	void clear_changed() { 
		inp_->clear_changed();
		Fl_Widget::clear_changed();
	}
	void set_changed() { 
		inp_->set_changed();
		// no need to call Fl_Widget::set_changed()
	}
	void clear() {
		menu_->clear();
	}
	Fl_Boxtype down_box() const {
		return (menu_->down_box());
	}
	void down_box(Fl_Boxtype b) {
		menu_->down_box(b);
	}
	const Fl_Menu_Item *menu() {
		return (menu_->menu());
	}
	void menu(const Fl_Menu_Item *m) {
		menu_->menu(m);
	}
	void resize(int X, int Y, int W, int H) {
		Fl_Group::resize(X,Y,W,H);
		inp_->resize(inp_x(), inp_y(), inp_w(), inp_h());
		menu_->resize(menu_x(), menu_y(), menu_w(), menu_h());
	}
	Fl_Color textcolor() const {
		return (inp_->textcolor());
	}
	void textcolor(Fl_Color c) {
		inp_->textcolor(c);
	}
	uchar textfont() const {
		return (inp_->textfont());
	}
	void textfont(uchar f) {
		inp_->textfont(f);
	}
	uchar textsize() const {
		return (inp_->textsize());
	}
	void textsize(uchar s) {
		inp_->textsize(s);
	}
	const char* value() const {
		return (inp_->value());
	}
	void value(const char *val) {
		inp_->value(val);
	}
	void value(int val) {
		menu_->value(val);
		inp_->value(menu_->text(val));
	}
	Fl_Menu_Button *menubutton() { return menu_; }
	Fl_Input *input() { return inp_; }
};

#endif // !Fl_Input_Choice_gfc_H



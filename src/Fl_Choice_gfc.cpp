#include "Fl_Choice_gfc.h"

#include <FL/Fl.H>
#include <FL/Fl_Choice.H>
#include <FL/fl_draw.H>
#include <string>
#include "UIConstants.h"
Fl_Choice_gfc::Fl_Choice_gfc(int X, int Y, int W, int H, const char *l) : Fl_Choice(X,Y,W,H,l)
{
	
}

int Fl_Choice_gfc::handle(int e) {
	if (!menu() || !menu()->text) return 0;
	const Fl_Menu_Item* v;
	switch (e) {
  case FL_ENTER:
  case FL_LEAVE:
	  return 1;

  case FL_KEYBOARD:
	  if (Fl::event_key() != ' ' ||
		  (Fl::event_state() & (FL_SHIFT | FL_CTRL | FL_ALT | FL_META))) return 0;
  case FL_PUSH:
	  if (Fl::visible_focus()) Fl::focus(this);
J1:
	  if (Fl::scheme()) {
		  v = menu()->pulldown(x(), y(), w(), h(), mvalue(), this);
	  } else {
		  // In order to preserve the old look-n-feel of "white" menus,
		  // temporarily override the color() of this widget...
		  //Fl_Color c = color();
		  //color(FL_BACKGROUND2_COLOR);
		  v = menu()->pulldown(x(), y(), w(), h(), mvalue(), this);
		  //color(c);
	  }
	  if (!v || v->submenu()) return 1;
	  if (v != mvalue()) redraw();
	  picked(v);
	  return 1;
  case FL_SHORTCUT:
	  if (Fl_Widget::test_shortcut()) goto J1;
	  v = menu()->test_shortcut();
	  if (!v) return 0;
	  if (v != mvalue()) redraw();
	  picked(v);
	  return 1;
  case FL_FOCUS:
  case FL_UNFOCUS:
	  if (Fl::visible_focus()) {
		  redraw();
		  return 1;
	  } else return 0;
  default:
	  return 0;
	}
}

void Fl_Choice_gfc::draw(){

	color(fl_rgb_color(42,42,42));
	down_color(fl_rgb_color(42,42,42));
	this->selection_color(fl_rgb_color(42,42,42));
	this->color2(fl_rgb_color(42,42,42));
	labelcolor(fl_rgb_color(85,85,85));
	textcolor(fl_rgb_color(GFC_WIDGET_LIGHT_TEXT_COLOR,GFC_WIDGET_LIGHT_TEXT_COLOR,GFC_WIDGET_LIGHT_TEXT_COLOR));
	
int dx = Fl::box_dx(FL_FLAT_BOX);
  int dy = Fl::box_dy(FL_FLAT_BOX);
  int H = h() - 2 * dy;
  int W = (H > 20) ? 20 : H;
  int X = x() + w() - W - dx;
  int Y = y() + dy;
  int w1 = (W - 4) / 3; if (w1 < 1) w1 = 1;
  int x1 = X + (W - 2 * w1 - 1) / 2;
  int y1 = Y + (H - w1 - 1) / 2;

  if (Fl::scheme()) {
    draw_box(FL_FLAT_BOX, color());

    fl_color(active_r() ? labelcolor() : fl_inactive(labelcolor()));
    if (!strcmp(Fl::scheme(), "plastic")) {
      // Show larger up/down arrows...
      fl_polygon(x1, y1 + 3, x1 + w1, y1 + w1 + 3, x1 + 2 * w1, y1 + 3);
      fl_polygon(x1, y1 + 1, x1 + w1, y1 - w1 + 1, x1 + 2 * w1, y1 + 1);
    } else {
      // Show smaller up/down arrows with a divider...
      x1 = x() + w() - 13 - dx;
      y1 = y() + h() / 2;
      fl_polygon(x1, y1 - 2, x1 + 3, y1 - 5, x1 + 6, y1 - 2);
      fl_polygon(x1, y1 + 2, x1 + 3, y1 + 5, x1 + 6, y1 + 2);

      fl_color(fl_darker(color()));
      fl_yxline(x1 - 7, y1 - 8, y1 + 8);

      fl_color(fl_lighter(color()));
      fl_yxline(x1 - 6, y1 - 8, y1 + 8);
    }
  } else {
    draw_box(FL_FLAT_BOX, fl_rgb_color(42,42,42));
    draw_box(FL_FLAT_BOX,X,Y,W,H,color());

    fl_color(active_r() ? labelcolor() : fl_inactive(labelcolor()));
    //fl_polygon(x1, y1, x1 + w1, y1 + w1, x1 + 2 * w1, y1);
	// Show smaller up/down arrows with a divider...
	x1 = x() + w() - 13 - dx;
	y1 = y() + h() / 2;
	fl_polygon(x1, y1 - 2, x1 + 3, y1 - 5, x1 + 6, y1 - 2);
	fl_polygon(x1, y1 + 2, x1 + 3, y1 + 5, x1 + 6, y1 + 2);

	/*fl_color(fl_darker(color()));
	fl_yxline(x1 - 7, y1 - 8, y1 + 8);*/

	fl_color(fl_lighter(color()));
	fl_yxline(x1 - 6, y1 - 8, y1 + 8);
  }

  W += 2 * dx;

  if (mvalue()) {
    Fl_Menu_Item m = *mvalue();
    if (active_r()) m.activate(); else m.deactivate();

    // ERCO
    int xx = x() + dx, yy = y() + dy + 1, ww = w() - W, hh = H - 2;

    fl_clip(xx, yy, ww, hh);
	
    /*if ( Fl::scheme()) {
      Fl_Label l;
      l.value = m.text;
      l.image = 0;
      l.deimage = 0;
      l.type = m.labeltype_;
      l.font = m.labelsize_ || m.labelfont_ ? m.labelfont_ : uchar(textfont());
      l.size = m.labelsize_ ? m.labelsize_ : textsize();
      l.color= m.labelcolor_ ? m.labelcolor_ : textcolor();
      if (!m.active()) l.color = fl_inactive((Fl_Color)l.color);
      fl_draw_shortcut = 2; // hack value to make '&' disappear
      l.draw(xx+3, yy, ww>6 ? ww-6 : 0, hh, FL_ALIGN_LEFT);
      fl_draw_shortcut = 0;
      if ( Fl::focus() == this ) draw_focus(box(), xx, yy, ww, hh);
    }
    else */{
      fl_draw_shortcut = 2; // hack value to make '&' disappear
	  
      m.draw(xx, yy, ww, hh, this, Fl::focus() == this);
      fl_draw_shortcut = 0;
    }

    fl_pop_clip();
  }

  draw_label();
}
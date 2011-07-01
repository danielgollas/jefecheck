
// ***************************************************************
//  GlViewport   version:  1.0     date: 03/20/2006
//  -------------------------------------------------------------
/********************************************************************
	created:	2006/03/20
	created:	20:3:2006   14:01
	filename: 	E:\projects\gfcheck\gfcheck\fluid\GlViewport.h
	file path:	E:\projects\gfcheck\gfcheck\fluid
	file base:	GlViewport
	file ext:	h
	author:		Daniel Goll�

	purpose:
*********************************************************************/
//  -------------------------------------------------------------
//  Copyright (C) 2006 - All Rights Reserved
// ***************************************************************
//
// ***************************************************************

//THIS IS THE ARGUMENT FOR THE AUTOMAKE MANAGER 
//$(all_libraries) -dynamic -lHalf -lIex -lIlmImf -lgfl -lgfle -lglut -lpthread -static /usr/lib/libboost_program_options.a /usr/lib/libboost_thread-mt.a `botan-config --libs` `fltk-config --use-gl --use-images --ldstaticflags` `flu-config --ldstaticflags`

//$(all_libraries) $(architectureFlags) $(boostLibPath)/libboost_program_options.a $(boostLibPath)/libboost_thread-mt.a $(glutLibCommand) -lHalf -lIex -lIlmImf -lgfl -lgfle -lpthread  -static `botan-config --libs` `fltk-config --use-gl --use-images --ldstaticflags` `flu-config --ldstaticflags`

//$(all_libraries) $(architectureFlags) $(boostLibPath)/libboost_program_options.a $(boostLibPath)/libboost_thread-mt.a $(glutLibCommand) --ldstaticflags` --libs` --use-gl --use-images -lHalf -lIex -lIlmImf -lgfl -lgfle -lpthread `botan-config `fltk-config `flu-config

#ifndef GlViewport_h
#define GlViewport_h

#  include <Fl/Fl_Gl_Window.h>
//#  include <Fl/gl.h>
#ifdef __APPLE__
#include <OpenGL/glu.h>
#else
#  include <GL/glu.h>
#endif
#include <stdio.h>
#include <Fl/Fl.h>
#include "gfcSequence.h"
#include "UIConstants.h"
#include "gfcPlate.h"
#include <FL/Fl.h>
#include <FL/Fl_Menu_Window.h>

void IdleFunc ( void* pData );

/*class gfcPlate;
class gfcSequence;*/

class PopupWindow : public Fl_Menu_Window
{
		Fl_Box *output;
		Fl_Box *theColorBox;
		char theText[2048];
		// Size window to just fit output's label text
		void SizeToText();

	public:
		void setColor ( int r, int g, int b );
		PopupWindow() : Fl_Menu_Window ( 10,10 )
		{
			output = new Fl_Box ( 0, 15, w(), h() );                 // box will have the text of user's msg
			output->box ( FL_UP_BOX );                              // popup window will have an 'Up Box' border
			output->color ( fl_rgb_color(GFC_BG_COLOR,GFC_BG_COLOR,GFC_BG_COLOR) ); 
			

			theColorBox=new Fl_Box ( 3,3,5,10 );
			theColorBox->box ( FL_FLAT_BOX );


			end();
			hide();
			border ( 0 );                                           // popup will be borderless
			output->align ( FL_ALIGN_LEFT|FL_ALIGN_INSIDE );        // text should be left aligned
			output->label ( "No text defined" );                    // (default msg if none defined)
			output->labelsize(12);
			output->labelfont(FL_HELVETICA);
			output->labelcolor(fl_rgb_color(GFC_WIDGET_DARK_TEXT_COLOR,GFC_WIDGET_DARK_TEXT_COLOR,GFC_WIDGET_DARK_TEXT_COLOR));
			SizeToText();
			set_non_modal();
		}
		// Change text in box
		void text ( const char* s )
		{
			strcpy ( theText,s );
			output->label ( theText );                                    // set message text
			SizeToText();                                           // resize window to size of text
		}
		// Pop up window at current mouse position

		void updatePos()
		{
			position ( Fl::event_x_root() + 10, Fl::event_y_root() + 10 );
		}

		void popup()
		{
			position ( Fl::event_x_root() + 10, Fl::event_y_root() + 10  );  // position window at cursor
			show();
		}
};


class GlViewport : public Fl_Gl_Window
{

	public:

		GlViewport ( int x,int y,int w,int h,const char *l=0 ) : Fl_Gl_Window ( x,y,w,h,l ), ID ( 0 )
				, framingMode ( FRAMINGSINGLE_ID )
		{

			play=false;
			targetFPS=24;
			loopMode=LOOPMODELOOP_ID;
			scale=1;
			transX=transY=prevX=prevY=0;
			//bgColorR=bgColorG=bgColorB=0.0;//0.18;
			mode ( FL_STENCIL | FL_RGB | FL_ALPHA );
			bgColorR[0]=bgColorG[0]=bgColorB[0]=0;
			bgColorR[1]=bgColorG[1]=bgColorB[1]=0.5;
			showChat=true;
			popup = new PopupWindow();
			popup->text ( "This is a test\nSo is this, a much longer line of text." );

			end();
		}

		
		void draw();
		void size ( int X,int Y,int W,int H );
		//void resize ( int X,int Y,int W,int H ); //resize now handled with "valid" in idle func
		int showChat;
		void layout();
		int getQuadFromMousePos(int x, int y);
		int handle ( int );
		void setVsync(int value);
		/*gfcSequence trackA;
		gfcSequence trackB;
		gfcSequence trackC;
		gfcSequence trackD;*/

		int targetFPS;
		int currentFrame;

		gfcFrame tf; //test frame
		RawFrame trf; //test raw frame for preview
		gfcPlate tp; //test plate for preview
		int loopMode;
		bool play;
		int transX;
		int transY;
		int prevX;
		int prevY;
		float scale;
		int startQuad;

		void reLayout ( void );
		std::string updateTimecode ( void );
		int getMaxTrackLenght ( void );
		int ID;
		int framingMode;
		bool resized;
		gfcPlate q1;
		gfcPlate q2;
		gfcPlate q3;
		gfcPlate q4;

		gfcFrame previewFrameA;
		gfcFrame previewFrameB;
		gfcFrame previewFrameC;
		gfcFrame previewFrameD;

		PopupWindow *popup;
		// Fl_Rectangle rec;
		void setBGColor ( float r, float g, float b );
	private:
		float bgColorR[2];
		float bgColorG[2];
		float bgColorB[2];
};
#endif

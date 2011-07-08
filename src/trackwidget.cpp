#include "trackwidget.h"
#include <stdio.h>
#include <iostream>
#include <math.h>
#include <FL/fl_draw.H>
#include "gfcStructures.h"

TrackWidget::TrackWidget(int x, int y, int w, int h)
        : Fl_Group(x,y,w,h)/*,
        bb(x,y,w,h,""),
        bg(x,y,w,h),
        rangeBox(x,y+h/4,(int)(w/2.0),(int)(h/2.0),"")*/
        
        /*,
         currentPos(x+20,(int)y,2,15)*/
{
	
	bb=new Fl_Box(x,y,w,h);
	bg=new Fl_Box(x,y,w,h);
	loadedBox=new Fl_Progress(x,y,w,h);
	labelBox=new Fl_Box(x,y,w,h);
	end(); //end the group widget
	
    bb->box(FL_FLAT_BOX);
    bb->color(FL_BLACK);
    
    Fl_Group::box(FL_FLAT_BOX);
    Fl_Group::color(FL_BLACK);
    
    //bg->box(FL_PLASTIC_UP_BOX);
	bg->box(FL_FLAT_BOX);
    bg->color(fl_rgb_color(85,85,85));
    
    labelBox->box(FL_NO_BOX);
    labelBox->copy_label("this is the label/withaPath/more/last");
    labelBox->labelsize(10);
    labelBox->labelfont(FL_HELVETICA);
    labelBox->labelcolor(FL_WHITE);
	loadedBox->box(FL_FLAT_BOX);
	loadedBox->color(fl_rgb_color(85,85,85)); //progress' color is the background
    //loadedBox->selection_color(FL_DARK_GREEN); //progress' slection color is the fill color of the progress bar
	loadedBox->selection_color(fl_rgb_color(160,160,160)); //progress' slection color is the fill color of the progress bar
    /*currentPos.box(FL_FLAT_BOX);
           currentPos.color(FL_FOREGROUND_COLOR);*/

//     rangeBox.color(FL_FOREGROUND_COLOR);
//     rangeBox.selection_color((Fl_Color)77);
//     rangeBox.labelsize(9);
//     rangeBox.align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);
//     rangeBox.value(60);
//     originalXpos=bg.x();
	
	rangeStart=1;
	rangeEnd=0;
	
	visibleStart=1;
	visibleEnd=1;
	
	totalFramesToLoad=0;
	firstLoadedFrame=0;
	loadedFrames=0;
	
	offset=0;

}


TrackWidget::~TrackWidget()
{}

void TrackWidget::update()
{
	
	if(updateFlag){
		
	updateFlag=false;
	
	//calculate the frame size in pixels according to the visible range.
	frameSize=1;
	//we offset the ends by +1 to facilitate calculating the differences between start and end frames. 1->10 is 10 frames, but 10-1 is 9
	int tmpVisEnd=visibleEnd+1;
	int tmpEnd=rangeEnd+1;
	
	if(visibleEnd>(visibleStart-1))
		frameSize = (bb->w()/(float)(tmpVisEnd-visibleStart));
	
	//if the range is whithin the visible range all is good. The start of the range should be the difference in frames between start and visiblestart.
	
	int startDifference=rangeStart-visibleStart+offset;  ///it seems logical that rangeStart will always be one... mmmh...
	//printf("frameSize (%i/(%i-%i+1)=%f\n",bb->w(),tmpVisEnd,visibleStart,bb->w()/(float)(tmpVisEnd-visibleStart));
	
	bg->position(bb->x()+  frameSize*(startDifference)   ,bg->y());
	bg->size((tmpEnd-rangeStart)*frameSize,bg->h());
	
	labelBox->resize(bg->x(),bg->y(),bg->w(),bg->h());
	
	//loadedBox->align(FL_ALIGN_INSIDE || FL_ALIGN_CENTER);
	
	loadedBox->position( bg->x()+(firstLoadedFrame-1)*frameSize, bg->y() );
	loadedBox->size(totalFramesToLoad*frameSize,loadedBox->h());
	loadedBox->minimum(0);
	loadedBox->maximum(totalFramesToLoad);
	loadedBox->value(loadedFrames);
	
	labelBox->copy_label(label.c_str());
	bb->tooltip(labelBox->label());
	
//	Fl_Group::redraw();
	
	redraw();
	}
}



void TrackWidget::setOffset(int x)
{
     offset=x;
     updateFlag=true;
     //update();
}


 ///what part of the timeline is visible
 void TrackWidget::setVisibleRange(int start, int end){
 	visibleStart=start;
 	visibleEnd=end;
 	updateFlag=true;
 	//update();
 }

void TrackWidget::setRange(int startFrame, int endFrame)
{
   rangeStart=startFrame;
   rangeEnd=endFrame;
   updateFlag=true;
   //update();
}

/*void TrackWidget::setFrameSize(float fsize)
{
	frameSize=fsize;
}*/

void TrackWidget::resize(int px, int py, int pw, int ph)
{
	
	Fl_Group::resize(px,py,pw,ph);
	updateFlag=true;
	//update();
	//printf("Rezised widget %i %i %i %i\n",bb->x(),bb->y(),bb->w(),bb->h());
	
	//bb->size(w(),h());
	//bg->position(bb->x()+10,bg->y());
	//bb->redraw();
}

void TrackWidget::draw()
{
	fl_push_clip(bb->x(),bb->y(),bb->w(),bb->h());
	Fl_Group::draw();
	
	//TODO: draw the current Frame mark, use a 1 pixel box? probably best
	
	
	fl_pop_clip();
	
}

void TrackWidget::setLoadedFrames(int numOfLoadedFrames)
{
	loadedFrames=numOfLoadedFrames;
	updateFlag=true;
	//update();
}

void TrackWidget::setCurrentPos(int frame)
{
	currentPos=frame;
	updateFlag=true;
	//redraw();
}

void TrackWidget::setTotalFramesToLoad(int firstFrame, int howMany)
{
	this->firstLoadedFrame=firstFrame;
	totalFramesToLoad=howMany;
	updateFlag=true;
	//update();
}

void TrackWidget::setLabel(std::string plabel)
{
	label=plabel;
	updateFlag=true;
}

int TrackWidget::handle(int e)
{
	switch(e)
	{
		case FL_PUSH:
		{
			do_callback();
			return 1;
		}
		break;
		case FL_RELEASE:
		{
			do_callback();
			return 1;
		}
		break;
		
		case FL_DRAG:
		{
			do_callback();
		}
		
		case FL_DND_ENTER:          // return(1) for these events to 'accept' dnd
            	case FL_DND_DRAG:
            	case FL_DND_RELEASE:
                return(1);
            	break;
            	
            	case FL_PASTE:              // handle actual drop (paste) operation
            	std::string pastedText=Fl::event_text();
				std::cout<<"pasted text: "<<GetFilenameNoFilePrefix(RemoveNewLine(pastedText))<<std::endl<<"nextLine"<<std::endl;
            	//printf("\nDropped %s (%s) into track\n",pastedText.c_str(),RemoveNewLine(pastedText).c_str());
                do_callback();
                return(1);
		
		break;
	}
	 
	 return 0;
}

int TrackWidget::getClickedFrame()
{
	int result=((Fl::event_x()-x())/frameSize)+1-visibleStart-offset; //+1 accounts for the visibleStart that is offset by 1.
	return result>=0?result:0; 
}



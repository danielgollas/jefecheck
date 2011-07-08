#ifndef TRACKWIDGET_H
#define TRACKWIDGET_H
#include "glew.h"
#include <FL/Fl_Box.H>
#include <FL/Fl.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Group.H>
#include <string>

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
*/
class TrackWidget : public Fl_Group
{
public:
    TrackWidget(int x, int y, int w, int h);
    ~TrackWidget();
     void draw();
     void size(int x, int y);
     void resize(int x, int y, int w, int h);
     void update(); ///sets sets all the subwidgets properties calculating them from the range, percentage, visible range and size of the bb
     void setLoadedFrames(int numOfLoadedFrames); ///set how many frames from the range are loaded
     void setRange(int startFrame, int endFrame); ///how many frames the sequence has and from where to where those frames go.
     void setCurrentPos(int frame); ///todo will eventually set where the playhead for this frame is.
     void setVisibleRange(int start, int end); ///what part of the timeline is visible,
     void setOffset(int x); ///how much this sequence is offsetted. modifies the bgbox
     void setTotalFramesToLoad(int firstLoadedFrame, int howMany);
     void setLabel(std::string label);
     int getClickedFrame();
     int handle(int e);
     //void setFrameSize(float fsize);
private:
	Fl_Box *bb; ///bounding box, this changes size when the widget is resized, it remains the same size as the timeline, eliminating the need to access it. All other components resize in relation to this. 
	Fl_Box *bg;
	Fl_Progress *loadedBox;
	Fl_Box *labelBox;
	int originalXpos;
	int totalFramesToLoad;
	float frameSize;
	
	int firstLoadedFrame;
	int loadedFrames;
	
	int rangeStart;
	int rangeEnd;
	
	int visibleStart;
	int visibleEnd;
	
	int offset;
	int currentPos;
	
	std::string label;
	
	bool updateFlag; //we use a flag to mark when the widget requires updating since we should not call fltk from non-main threads.
 	
};

#endif

#include "gfcsequencegui_fltk.h"
#include "UIConstants.h"
#include "gfcStructures.h"
gfcSequenceGUI_FLTK::gfcSequenceGUI_FLTK()
 : gfcSequenceGUI()
{
}


gfcSequenceGUI_FLTK::~gfcSequenceGUI_FLTK()
{
}


float gfcSequenceGUI_FLTK::getGamma()
{
	//Fl::lock();
	return gammaSlider->value();
	//Fl::unlock();Fl::awake((void*)NULL);
	
}

float gfcSequenceGUI_FLTK::getScale()
{
	//Fl::lock();
	return atof(scaleChooser->value());
	//Fl::unlock();Fl::awake((void*)NULL);
}



int gfcSequenceGUI_FLTK::getCompression()
{
	switch(compression->value())
	{
		
		case 0:
			return GFC_8BPC;
		break;
		
		case 1:
			return GFC_16BPC;
		break;
		
		case 2: 
			return GFC_16HALF;
		break;
			
		case 3:
			return GFC_4BPC;
		break;
		
		case 4:
			return GFC_S3TCDX1;
		break;
		
		default:
			return GFC_8BPC;
		break;
	}
}

int gfcSequenceGUI_FLTK::getFilter()
{
//Fl::lock();
   return filterChooser->value();
  // Fl::unlock();Fl::awake((void*)NULL);
}

int gfcSequenceGUI_FLTK::getFrom()
{
//Fl::lock();
    return loadFromSpinner->value();
  //  Fl::unlock();Fl::awake((void*)NULL);
}

int gfcSequenceGUI_FLTK::getStream()
{
    return streamButton->value();
}

int gfcSequenceGUI_FLTK::getTo()
{
//Fl::lock();
   return loadToSpinner->value();
  // Fl::unlock();Fl::awake((void*)NULL);
}

int gfcSequenceGUI_FLTK::getAppendOption()
{
	//TODO: THIS HAS TO BE CHANGED TO ACTUALLY DO SOMETHING USEFULL
	switch(this->appendModeChooser->value())
	{
		case 0:
//			printf("GFC_SEQREPLACE\n");
			return GFC_SEQREPLACE;
		break;
		
		case 1:
//			printf("GFC_SEQLEAVE\n");
			return GFC_SEQLEAVE;
		break;
		
		case 2:
//			printf("GFC_SEQSTREAM\n");
			return GFC_SEQSTREAM;
		break;
	}
	
}

std::vector< int > gfcSequenceGUI_FLTK::getLutList()
{
	std::vector<int> returnValue;
	return returnValue;
}

std::string gfcSequenceGUI_FLTK::getFilename()
{
   return fileNameInput->value();
}

void gfcSequenceGUI_FLTK::activateAbortButton()
{
	//Fl::lock();
	abortButton->activate();
	startButton->deactivate();
	//abortButton->redraw();
	//Fl::unlock();
}

void gfcSequenceGUI_FLTK::deactivateAbortButton()
{
	//Fl::lock();
  	abortButton->deactivate();
	startButton->activate();
	//abortButton->redraw();   
	//Fl::unlock();
}

char testChar[30]="global char";
void gfcSequenceGUI_FLTK::setRecentlyLoaded(std::vector<std::string> &filenames)
{
	
	std::vector<std::string>::reverse_iterator it=filenames.rbegin(), end=filenames.rend();
	recent->clear();
	std::string tmp;
	int i,j=0;
	Fl_Menu_Item *m;
	for(it;it!=end;it++)
	{
		/*i=recent->add("x");
		m=(Fl_Menu_Item*)recent->menu();
		m[i].label(it->c_str());*/
		
		tmp=*it;
		AddMenuSlash(tmp); //prevents from turning / separated paths into menu hierarchies in windows
		i=recent->add(tmp.c_str(),0,0,0,0);
	}
}




void gfcSequenceGUI_FLTK::setOffset(int offset)
{
	slider->setOffset(offset);
}

void gfcSequenceGUI_FLTK::setPlayHead(int timelineValue)
{
  
}



void gfcSequenceGUI_FLTK::assignFilenameWidget(void * widget)
{
	this->fileNameInput=(Fl_Input*)widget;
	containingWidgets.insert(widget);
}

void gfcSequenceGUI_FLTK::assignFromWidget(void * widget)
{
	this->loadFromSpinner=(gfcSliderInput*)widget;
	containingWidgets.insert(widget);
}

void gfcSequenceGUI_FLTK::assignToWidget(void * widget)
{
	this->loadToSpinner=(gfcSliderInput*)widget;
	containingWidgets.insert(widget);
}

void gfcSequenceGUI_FLTK::assignScaleWidget(void * widget)
{
	this->scaleChooser=(Fl_Input_Choice*)widget;
	containingWidgets.insert(widget);
}

void gfcSequenceGUI_FLTK::assignGammaWidget(void * widget)
{
	this->gammaSlider=(Fl_Value_Input*)widget;
	containingWidgets.insert(widget);
}

void gfcSequenceGUI_FLTK::assignAOIWidget(void * widget)
{
	this->aoiButton=(Fl_Check_Button*)widget;
	containingWidgets.insert(widget);
}

void gfcSequenceGUI_FLTK::assignStreamWidget(void * widget)
{
	this->appendModeChooser=(Fl_Choice*)widget;
	containingWidgets.insert(widget);
}

void gfcSequenceGUI_FLTK::assignFilterWidget(void * widget)
{
	this->filterChooser=(Fl_Choice*)widget;
	containingWidgets.insert(widget);
}

void gfcSequenceGUI_FLTK::assignAbortWidget(void * widget)
{
	this->abortButton=(Fl_Button*)widget;
	containingWidgets.insert(widget);
}

void gfcSequenceGUI_FLTK::assignCompressionWidget(void * widget)
{
	this->compression=(Fl_Choice*)widget;
	containingWidgets.insert(widget);
}

void gfcSequenceGUI_FLTK::assignSliderWidget(void * widget)
{
	this->slider=(TrackWidget*)widget;
	containingWidgets.insert(widget);
}


void gfcSequenceGUI_FLTK::assignBrowseWidget(void * widget)
{
	browseButton=(Fl_Button*)widget;
	containingWidgets.insert(widget);
}

void gfcSequenceGUI_FLTK::assignWindowWidget(void * widget)
{
	window=(Fl_Double_Window*)widget;
	containingWidgets.insert(widget);
}

void gfcSequenceGUI_FLTK::setFromToBounds(int Min, int Max, bool setToMinAndMax)
{
	this->loadFromSpinner->range(Min, Max);
	this->loadToSpinner->range(Min, Max);
	
	if(setToMinAndMax)
	{
		loadFromSpinner->value(Min);
		loadToSpinner->value(Max);
	}

}



void gfcSequenceGUI_FLTK::setTrackOffset(int offset)
{
	//TODO: Implement Me
	slider->setOffset(offset);
}

void gfcSequenceGUI_FLTK::setTrackVisibleRange(int from, int to)
{
	this->slider->setVisibleRange(from, to);
}

void gfcSequenceGUI_FLTK::setLoadedRange(int numberOfLoadedFrames)
{
	this->slider->setLoadedFrames(numberOfLoadedFrames);
}

void gfcSequenceGUI_FLTK::setTrackRange(int start, int end)
{
	slider->setRange(start, end);
}

void gfcSequenceGUI_FLTK::setTotalFramesToLoad(int firstFrame, int howMany)
{
	slider->setTotalFramesToLoad(firstFrame, howMany);
}

void gfcSequenceGUI_FLTK::setTrackLabel(std::string label)
{
	slider->setLabel(label);
}

/**
 * Tells if a widget belongs to this sequence GUI
 * @param theWidget 
 * @return true if the widget belong to this widget, false if not.
 */
int gfcSequenceGUI_FLTK::widgetBelongsToMe(void * theWidget)
{
	return (containingWidgets.find(theWidget)!=containingWidgets.end());
}

void gfcSequenceGUI_FLTK::setFilename(std::string filename)
{
	this->fileNameInput->value(filename.c_str());
	this->fileNameInput->tooltip(this->fileNameInput->value());
}

int gfcSequenceGUI_FLTK::getCrop()
{
	return this->aoiButton->value();
}


int gfcSequenceGUI_FLTK::getWindowVisible()
{
	return window->visible();
}

void gfcSequenceGUI_FLTK::assignEstimatesWidget(void * widget)
{
	estimates=(Fl_Output*)widget;
	containingWidgets.insert(widget);
}

void gfcSequenceGUI_FLTK::setEstimates(std::string pestimates)
{
	estimates->value(pestimates.c_str());
}



void gfcSequenceGUI_FLTK::assignRecentButton(void * widget)
{
	recent=(Fl_Menu_Button*)widget;
	containingWidgets.insert(widget);
}


void gfcSequenceGUI_FLTK::assignUnloadAndClearButton(void * widget)
{
	unloadAndClear=(Fl_Button*)widget;
	containingWidgets.insert(widget);
}

void gfcSequenceGUI_FLTK::clearAllValues()
{
	loadFromSpinner->value(0);
	loadFromSpinner->range(0,0);
	loadToSpinner->value(0);
	loadToSpinner->range(0,0);
	
	filterChooser->value(0);
	appendModeChooser->value(0);
	
	scaleChooser->value(0);
	aoiButton->value(0);
	
	compression->value(0);
	estimates->value("");
	
	fileNameInput->value("");
	fileNameInput->tooltip(fileNameInput->value());
}

void gfcSequenceGUI_FLTK::assignMoreOptionsButton(void * widget)
{
	moreOptions=(Fl_Button*)widget;
	containingWidgets.insert(widget);

}

void gfcSequenceGUI_FLTK::setScale(std::string scale)
{
	scaleChooser->value(scale.c_str());

}

void gfcSequenceGUI_FLTK::setFromFrame(int value)
{
	this->loadFromSpinner->value(value);
}

void gfcSequenceGUI_FLTK::setToFrame(int value)
{
	this->loadToSpinner->value(value);

}
void gfcSequenceGUI_FLTK::setAppendOption(int option)
{
	this->appendModeChooser->value(option);
}

void gfcSequenceGUI_FLTK::setFilter(int filter)
{
	this->filterChooser->value(filter);
}

void gfcSequenceGUI_FLTK::setCrop(int crop)
{
	this->aoiButton->value(crop);
}

void gfcSequenceGUI_FLTK::setCompression(int compression)
{
	this->compression->value(compression);
}

void gfcSequenceGUI_FLTK::assignChannelOptionsWidget(void * widget)
{
	channels=(Fl_Choice*)widget;
	containingWidgets.insert(widget);
}

void gfcSequenceGUI_FLTK::assignStartButtonWidget(void * widget)
{
	startButton=(Fl_Button*)widget;
	containingWidgets.insert(widget);
}

void gfcSequenceGUI_FLTK::setChannelOptions(std::vector< std::string > options)
{
	channels->clear();
	std::vector<std::string>::iterator it=options.begin(), end=options.end(); 
	for(it;it!=end;it++)
		channels->add(it->c_str());
		
	channels->value(0);
}

void gfcSequenceGUI_FLTK::setChannel(int value)
{
	if(value>channels->size())
		channels->value(0);
	else
		channels->value(value);
}

void gfcSequenceGUI_FLTK::setChannel(std::string name)
{
	int size=channels->size();
	for(int i=0;i<size;i++)
	{
		if(name==channels->text(i)){
			channels->value(i);
			break;
		}
	}
	
}

int gfcSequenceGUI_FLTK::getChannel()
{
	return channels->value();
}

std::string gfcSequenceGUI_FLTK::getChannelName()
{
	if (channels)
	{
		
		if (channels->text()!=(void*)NULL)
		{
			return channels->text();
		}
		else
		{
			return "";
		}
		
	}
	else
	{
		
		return "";
	}

	
	

}
void gfcSequenceGUI_FLTK::updateTrackWidget()
{
	slider->update();
}
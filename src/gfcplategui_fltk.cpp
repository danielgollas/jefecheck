#include "gfcplategui_fltk.h"
#include <stdlib.h>

gfcPlateGUI_FLTK::gfcPlateGUI_FLTK()
 : gfcPlateGUI()
{

	this->rgbaButton=NULL;

	this->channelRButton=NULL;
	this->channelGButton=NULL;
	this->channelBButton=NULL;
	this->channelAButton=NULL;

	this->aspectChoice=NULL;
	this->cropButton=NULL;
	this->cropPosButton=NULL;

	this->flipButton=NULL;
	this->flopButton=NULL;

	this->rzInput=NULL;

	this->trackChoice=NULL;
	this->txInput=NULL;
	this->tyInput=NULL;

	this->lutChoice=NULL;
	this->gammaInput=NULL;
	this->exposureInput=NULL;
	this->brightnessInput=NULL;
	this->contrastInput=NULL;
	this->saturationInput=NULL;

	
}


gfcPlateGUI_FLTK::~gfcPlateGUI_FLTK()
{
}


float gfcPlateGUI_FLTK::getRZ()
{
	if (rzInput)
	{
		return this->rzInput->value();
	}
	else
		return 0.0;
}

gfcRectang gfcPlateGUI_FLTK::getAOI()
{
	gfcRectang returnvalue;
	return returnvalue;
}

int gfcPlateGUI_FLTK::getActive(){

	if (activeButton)
	{
		return activeButton->value();
	}
	else
	{
		return false;
	}
	

}

int gfcPlateGUI_FLTK::getCrop()
{
	if (cropButton)
	{
		return this->cropButton->value();
	}
	else
		return 0;
}

int gfcPlateGUI_FLTK::getFlip()
{
	if (flipButton)
	{
	return this->flipButton->value();
	}
	else
	{
		return false;
	}
}

int gfcPlateGUI_FLTK::getFlop()
{
	if (flopButton)
	{
		return this->flopButton->value();
	}
	else
	{
		return false;
	}
	
	
}

int gfcPlateGUI_FLTK::getOffset()
{
	return 0;
}

int gfcPlateGUI_FLTK::getSequenceID()
{
	if(trackChoice){
	return this->trackChoice->value();
	}
	else
	{
		return 0;
	}
}

int gfcPlateGUI_FLTK::getTX()
{
	if (txInput)
	{
		return this->txInput->value();
	}
	else
		return 0;
}

int gfcPlateGUI_FLTK::getTY()
{
	if (tyInput)
	{
	return this->tyInput->value();
	}
	else
		return 0;
}


float gfcPlateGUI_FLTK::getScale()
{
	if (scaleInput)
	{
		return this->scaleInput->value();
	}
	else
		return 0;
}

std::string gfcPlateGUI_FLTK::getAspectString()
{
	if (aspectChoice)
	{
		return this->aspectChoice->value();
	}
	else
		return "";
}



void gfcPlateGUI_FLTK::assignTXWidget(void * widget)
{
	this->txInput=(Fl_Value_Input*)widget;
}

void gfcPlateGUI_FLTK::assignTYWidget(void * widget)
{
	this->tyInput=(Fl_Value_Input*)widget;
}

void gfcPlateGUI_FLTK::assignRZWidget(void * widget)
{
	this->rzInput=(Fl_Value_Input*)widget;
}

void gfcPlateGUI_FLTK::assignZoom(void * widget)
{
	this->scaleInput=(Fl_Value_Input*)widget;
}

void gfcPlateGUI_FLTK::assignFlipWidget(void * widget)
{
	this->flipButton=(Fl_Button*)widget;
}

void gfcPlateGUI_FLTK::assignFlopWidget(void * widget)
{
	this->flopButton=(Fl_Button*)widget;
}

void gfcPlateGUI_FLTK::assignCropWidget(void * widget)
{
	this->cropButton=(Fl_Button*)widget;
}

void gfcPlateGUI_FLTK::assignChannelRWidget(void * widget)
{
	this->channelRButton=(Fl_Button*)widget;
}

void gfcPlateGUI_FLTK::assignChannelGWidget(void * widget)
{
	this->channelGButton=(Fl_Button*)widget;
}

void gfcPlateGUI_FLTK::assignChannelBWidget(void * widget)
{
	this->channelBButton=(Fl_Button*)widget;
}

void gfcPlateGUI_FLTK::assignChannelAWidget(void * widget)
{
	this->channelAButton=(Fl_Button*)widget;
}


void gfcPlateGUI_FLTK::setTX(float ptx)
{
	if (txInput)
	{
	txInput->value(ptx);
	}
}

void gfcPlateGUI_FLTK::setTY(float pty)
{
	if (tyInput)
	{
		tyInput->value(pty);
	}
}

void gfcPlateGUI_FLTK::setRZ(float prz)
{
	if (rzInput)
	{
		this->rzInput->value(prz);
	}
}

void gfcPlateGUI_FLTK::setFlip(int pflip)
{
	if (flipButton)
	{
		flipButton->value(pflip);
	}
}

void gfcPlateGUI_FLTK::setFlop(int pflop)
{
	if (flopButton)
	{
		flopButton->value(pflop);
	}
}

void gfcPlateGUI_FLTK::setCrop(int pcrop)
{
	if (cropButton)
	{
	cropButton->value(pcrop);
	}
}

void gfcPlateGUI_FLTK::setOffset(int poffset)
{
	if (offsetInput)
	{
		this->offsetInput->value(poffset);
	}
}

void gfcPlateGUI_FLTK::setScale(float pscale)
{
	if (scaleInput)
	{
		scaleInput->value(pscale);
	}
}

void gfcPlateGUI_FLTK::assignTrackChoiceWidget(void * widget)
{
	this->trackChoice=(Fl_Choice*)widget;
}

void gfcPlateGUI_FLTK::assignAspectChoiceWidget(void * widget)
{
	this->aspectChoice=(Fl_Input_Choice*)widget;
}

void gfcPlateGUI_FLTK::setTrackChoice(int pchoice)
{
	this->trackChoice->value(pchoice);
}

void gfcPlateGUI_FLTK::setAspectChoice(std::string paspect)
{
	this->aspectChoice->value(paspect.c_str());
}

void gfcPlateGUI_FLTK::assignOffsetWidget(void * widget)
{
	this->offsetInput=(Fl_Value_Input*)widget;
}


void gfcPlateGUI_FLTK::assignGroupWidget(void * widget)
{
	group=(Fl_Group*)widget;
}

void gfcPlateGUI_FLTK::assignRGBAWidget(void *widget)
{
	rgbaButton=(Fl_Button_RGBA_gfc*)widget;
}

void gfcPlateGUI_FLTK::assignActiveWidget(void * widget)
{
	activeButton=(Fl_Round_Button*)widget;
}


 void gfcPlateGUI_FLTK::assignLUTWidget(void *widget){
	lutChoice=(Fl_Choice_gfc*)widget;
}
 void gfcPlateGUI_FLTK::assignGammaWidget(void *widget){
	gammaInput=(Fl_Value_Input*)widget;
}
 void gfcPlateGUI_FLTK::assignExposureWidget(void *widget){
	exposureInput=(Fl_Value_Input*)widget;
}
 void gfcPlateGUI_FLTK::assignBrightnessWidget(void *widget){
	brightnessInput=(Fl_Value_Input*)widget;
}
 void gfcPlateGUI_FLTK::assignContrastWidget(void *widget){
	contrastInput=(Fl_Value_Input*)widget;
}
 void gfcPlateGUI_FLTK::assignSaturationWidget(void *widget){
	saturationInput=(Fl_Value_Input*)widget;
}


 void gfcPlateGUI_FLTK::setLUT(int value)
{
	lutChoice->value(value);
}

 void gfcPlateGUI_FLTK::clearLUTs()
 {
	 if (lutChoice)
	 {
		 lutChoice->clear();
	 }
	 
		
 }

 void gfcPlateGUI_FLTK::addLUTOption(std::string name){
	 if (lutChoice)
	 {
		 lutChoice->add(name.c_str());
	 }
	 
	
 }

 void gfcPlateGUI_FLTK::setGamma(float value)
{
	gammaInput->value(value);
}
 void gfcPlateGUI_FLTK::setExposure(float value)
{
	exposureInput->value(value);
}
 void gfcPlateGUI_FLTK::setBrightness(float value)
{
	brightnessInput->value(value);
}
 void gfcPlateGUI_FLTK::setContrast(float value)
{
	contrastInput->value(value);
}
 void gfcPlateGUI_FLTK::setSaturation(float value)
{
	saturationInput->value(value);
}

 int gfcPlateGUI_FLTK::getLUT()
{
	return lutChoice->value();
}

 std::string gfcPlateGUI_FLTK::getLUTName()
{
	return lutChoice->text();
}

 float gfcPlateGUI_FLTK::getGamma()
{
	return gammaInput->value();
}
 float gfcPlateGUI_FLTK::getExposure()
{
	return exposureInput->value();
}
 float gfcPlateGUI_FLTK::getBrightness()
{
	return brightnessInput->value();
}
 float gfcPlateGUI_FLTK::getContrast()
{
	return contrastInput->value();
}
 float gfcPlateGUI_FLTK::getSaturation()
{
	return saturationInput->value();
}

void gfcPlateGUI_FLTK::setGroupPosition(int x, int y)
{
	group->position(x,y);
}

void gfcPlateGUI_FLTK::setActive(int value)
{
	if (activeButton)
	{
		activeButton->value(value);
	}
	
}

void gfcPlateGUI_FLTK::setGroupVisible(int mode)
{
	switch(mode)
	{
		case 0:
			group->hide();
			break;
		
		case 1:
			group->set_visible();
			break;
		
		case 2:
			group->deactivate();
			break;
		
		case 3:
			group->activate();
			break;
	}
}
void gfcPlateGUI_FLTK::setActiveVisible(int mode)
{
	switch(mode)
	{
	case 0:
		activeButton->hide();
		break;

	case 1:
		activeButton->set_visible();
		break;

	case 2:
		activeButton->deactivate();
		break;

	case 3:
		activeButton->activate();
		break;
	}
}

float gfcPlateGUI_FLTK::getAspect()
{
	char tempAspectString[20];
        strcpy ( tempAspectString,this->aspectChoice->value() );

        float aspect=-1; //an aspect of -1 means use original aspect;

        if ( strstr ( tempAspectString,":" ) && strrchr ( tempAspectString,':' ) !=tempAspectString+strlen ( tempAspectString )-1 ) {

            char * pch=strtok ( tempAspectString,":" );
            aspect= ( atof ( strtok ( NULL,":" ) ) /atof ( pch ) );

            //calculate percentage and create new size for y
        } else {
            if ( !strstr ( tempAspectString,":" ) && !strstr ( tempAspectString,"original" ) )
               aspect=atof ( tempAspectString );
        }
        	
        	return aspect;
}

int gfcPlateGUI_FLTK::getRGBA()
{
	if (this->rgbaButton)
	{
		return this->rgbaButton->getCurrentValue();
	}
	else
	{
		return Fl_Button_RGBA_gfc::VALUE_RGB;
	}
	
}

bool gfcPlateGUI_FLTK::getChannelR()
{
	/*if (this->channelRButton)
	{
		return this->channelRButton->value();
	}
	else
		return false;*/

	if (this->rgbaButton)
	{
		switch(rgbaButton->getCurrentValue())
		{
		case Fl_Button_RGBA_gfc::VALUE_RGB:
		case Fl_Button_RGBA_gfc::VALUE_R:
			return 1;
			break;
		}
	}

	return 0;
	

}

bool gfcPlateGUI_FLTK::getChannelG()
{
	/*if (this->channelGButton)
	{
		return this->channelGButton->value();
	}
	else
		return true;*/

	if (this->rgbaButton)
	{
		switch(rgbaButton->getCurrentValue())
		{
		case Fl_Button_RGBA_gfc::VALUE_RGB:
		case Fl_Button_RGBA_gfc::VALUE_G:
			return 1;
			break;
		}
	}

	return 0;
}

bool gfcPlateGUI_FLTK::getChannelB()
{
	/*if (this->channelBButton)
	{
		return this->channelBButton->value();
	}
	else
		return true;*/

	if (this->rgbaButton)
	{
		switch(rgbaButton->getCurrentValue())
		{
		case Fl_Button_RGBA_gfc::VALUE_RGB:
		case Fl_Button_RGBA_gfc::VALUE_B:
			return 1;
			break;
		}
	}

	return 0;
	
	
}

bool gfcPlateGUI_FLTK::getChannelA()
{
	/*if (this->channelAButton)
	{
		return this->channelAButton->value();
	}
	else
		return true;*/

	if (this->rgbaButton)
	{
		switch(rgbaButton->getCurrentValue())
		{
		case Fl_Button_RGBA_gfc::VALUE_A:
			return 1;
			break;
		}
	}

	return 0;
	
}

bool gfcPlateGUI_FLTK::getShowPreview()
{
	return this->loadWindow->visible();
}

void gfcPlateGUI_FLTK::assignShowPreviewWidget(void * widget)
{
	this->loadWindow=(Fl_Double_Window*)widget;
}

void gfcPlateGUI_FLTK::setRGBA(int value)
{
	rgbaButton->setCurrentValue(value);
}



void gfcPlateGUI_FLTK::setChannelR(bool value)
{
	if (this->channelRButton){
			this->channelRButton->value(value);
	}

	if (rgbaButton)
	{
		if (value)
		{
			rgbaButton->setCurrentValue(Fl_Button_RGBA_gfc::VALUE_R);
		}
		/*else
		{
			rgbaButton->setCurrentValue(Fl_Button_RGBA_gfc::VALUE_RGB);
		}*/
	}
	

}



void gfcPlateGUI_FLTK::setChannelG(bool value)
{
	if (this->channelGButton)
	this->channelGButton->value(value);

	if (rgbaButton)
	{
		if (value)
		{
			rgbaButton->setCurrentValue(Fl_Button_RGBA_gfc::VALUE_G);
		}
		/*else
		{
			rgbaButton->setCurrentValue(Fl_Button_RGBA_gfc::VALUE_RGB);
		}*/
	}
}

void gfcPlateGUI_FLTK::setChannelB(bool value)
{
	if (this->channelBButton)
	this->channelBButton->value(value);

	if (rgbaButton)
	{
		if (value)
		{
			rgbaButton->setCurrentValue(Fl_Button_RGBA_gfc::VALUE_B);
		}
		/*else
		{
			rgbaButton->setCurrentValue(Fl_Button_RGBA_gfc::VALUE_RGB);
		}*/
	}
}

void gfcPlateGUI_FLTK::setChannelA(bool value)
{
	if (this->channelAButton)
	this->channelAButton->value(value);

	if (rgbaButton)
	{
		if (value)
		{
			rgbaButton->setCurrentValue(Fl_Button_RGBA_gfc::VALUE_A);
		}
		/*else
		{
			rgbaButton->setCurrentValue(Fl_Button_RGBA_gfc::VALUE_RGB);
		}*/
	}
}
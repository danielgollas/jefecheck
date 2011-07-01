#include "gfcpickobject.h"
#include <stdlib.h>
#include "gfcpickmanager.h"
extern gfcPickManager pickManager;

gfcPickColor gfcPickManager::currentColor;

gfcPickObjectStatus& gfcPickObjectStatus::operator=(const gfcPickObjectStatus &a)
{
	uniqueColor=a.uniqueColor;
	x=a.x;
	y=a.y;
	dx=a.dx;
	dy=a.dy;
	clicked=a.clicked;
	flags=a.flags;
	
	return *this;
}

gfcPickObjectStatus::gfcPickObjectStatus()
{
	x=0;
	y=0;
	dx=0;
	dy=0;
	clicked=0;
	this->flags=0;
	/*uniqueColor.colors[0]=0;
	uniqueColor.colors[1]=0;
	uniqueColor.colors[2]=0;*/
}

gfcPickObject::gfcPickObject()
{
	
	//status.uniqueColor=pickManager.getUniqueColor();
	notFirstRun=true;
	//status.uniqueColor=gfcPickManager::getUniqueColor();
}

gfcPickObject::~gfcPickObject(void)
{

}

void gfcPickObject::initialize()
{
	//status.uniqueColor=pickManager.getUniqueColor();
	//printf("Initializing Pick Object: MyUniqueColor=%i %i %i\n",status.uniqueColor.colors[0],status.uniqueColor.colors[1],status.uniqueColor.colors[2]);
}

gfcPickColor gfcPickObject::getPickColor()
{
	if (notFirstRun)
	{
		status.uniqueColor=pickManager.getUniqueColor();
		notFirstRun=false;
	}
	return status.uniqueColor;
}

void gfcPickObject::getPickColor(unsigned char result[3])
{
	if (notFirstRun)
	{
		status.uniqueColor=pickManager.getUniqueColor();
		/*status.uniqueColor.colors[0]=rand()%255;
		status.uniqueColor.colors[1]=rand()%255;
		status.uniqueColor.colors[2]=rand()%255;*/
		notFirstRun=false;
	}
	
	//printf("Returning Pick Object: MyUniqueColor=%i %i %i\n",status.uniqueColor.colors[0],status.uniqueColor.colors[1],status.uniqueColor.colors[2]);
	result[0]=status.uniqueColor.colors[0];
	result[1]=status.uniqueColor.colors[1];
	result[2]=status.uniqueColor.colors[2];
	//printf("Returning Pick Object: MyUniqueColor=%i %i %i\n",status.uniqueColor.colors[0],status.uniqueColor.colors[1],status.uniqueColor.colors[2]);
}

gfcPickObjectStatus gfcPickObject::getStatus(gfcPickNotifyParameters &params)
{
	//This method should be called by whoever is interested in this pickObject's status.
	//It will process whatever pickNotifyParameters it was passed and adjust it's values accordingly.
	//i.e. update if it is selected or not, and with that know if it was dragged or not etc.
	
	status.flags=params.flags;
	
	status.dx=0;
	status.dy=0;

	

	/*printf("status.uniqueColor=%i %i %i\nparams.pickedColor=%i %i %i\n\n",status.uniqueColor.colors[0],status.uniqueColor.colors[1],status.uniqueColor.colors[2],
		params.pickedColor.colors[0],params.pickedColor.colors[1],params.pickedColor.colors[2]);*/
	switch ( params.event ) {
	case GFC_PICK_EVENT_CLICK_DOWN:
		//Did they click on me?
		
		if (params.pickedColor==status.uniqueColor)
		{
			status.clicked=1;
		}
		else
		{
			status.clicked=0;
		}
		break;

	case GFC_PICK_EVENT_CLICK_UP:
		//on up click we are not clicked no matter if we where picked or not.
		status.clicked=0;
		break;

	case GFC_PICK_EVENT_DRAG:
		//if we are clicked then we where dragged, otherwise we where not
		if (status.clicked) {
			status.dx=params.dx;
			status.dy=params.dy;
		}
		break;
	}

	return status;

}

gfcPickObjectStatus gfcPickObject::getStatus()
{
	return status;
}
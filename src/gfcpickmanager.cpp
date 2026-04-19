#include "gfcpickmanager.h"
#include <algorithm>
#include <glad/glad.h>
#include <stdio.h>
gfcPickManager::gfcPickManager()
{
}


gfcPickManager::~gfcPickManager()
{
}


void gfcPickManager::registerDrawee(gfcPickDrawee * drawee)
{
	drawees.push_back(drawee);
}

void gfcPickManager::registerNotifee(gfcPickNotifee * notifee)
{
	notifees.push_back(notifee);
}

void gfcPickManager::unregisterDrawee(gfcPickDrawee * drawee)
{
	std::vector<gfcPickDrawee*>::iterator index=std::find(drawees.begin(),drawees.end(), drawee);
	if(index!=drawees.end())
		drawees.erase(index);
		
}

void gfcPickManager::unregisterNotifee(gfcPickNotifee * notifee)
{
	std::vector<gfcPickNotifee*>::iterator index=std::find(notifees.begin(),notifees.end(), notifee);
	if(index!=notifees.end())
		notifees.erase(index);
}

int gfcPickManager::doPicking(gfcPickEvent event, unsigned int flags, int x, int y, int dx, int dy)
{
	int somethingPicked=0;
	//1. Reset the unique colors
	//currentColor.colors[0]=currentColor.colors[1]=currentColor.colors[2]=0;
	
	//clear background to black
	//clear buffer 
	glClearColor ( 1,1,1,1 );
    	glPushAttrib ( GL_COLOR_BUFFER_BIT );
    	glColorMask ( true,true,true,true ); //make sure we clear all the colors, but restore before drawing
    	glClear ( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    	glPopAttrib();
	
	//2. Call each registered drawees drawForPick method
	std::vector<gfcPickDrawee*>::iterator dit=drawees.begin(), dend=drawees.end();
	for( dit; dit!=dend;dit++ ){
		if(*dit!=NULL)
		{
			(*dit)->drawForPicking();
		}
	}
	
	//3. do a readback from the back buffer for the pixel under the mouse.
	 unsigned char tmpPixel[3];
	 glReadPixels (x,y,1,1,
                                   GL_RGB,GL_UNSIGNED_BYTE, ( void * ) tmpPixel);
     
	 gfcPickColor readPixel(tmpPixel);

	//4. send the picked color to the notifees, along with the flags and params.
	std::vector<gfcPickNotifee*>::iterator nit=notifees.begin(), nend=notifees.end();
	gfcPickNotifyParameters params(x,y,dx,dy,event, readPixel, flags);
	for( nit; nit!=nend;nit++ ){
		if(*nit!=NULL)
		{
			somethingPicked |= (*nit)->pickNotify(params);
		}
	}
	return somethingPicked;
}

gfcPickColor gfcPickManager::getUniqueColor()
{	
	
	//the method below yields 256^3 unique colors. 
	//change the current color
	/*if (currentColor.colors.size()<=0)
	{
		currentColor.colors.push_back(0);
		currentColor.colors.push_back(0);
		currentColor.colors.push_back(0);
	}*/
	
	gfcPickColor localCurrentColor=gfcPickManager::currentColor;

	localCurrentColor.colors[0]++;
	if(localCurrentColor.colors[0]==255)
	{
		localCurrentColor.colors[0]=0;
		localCurrentColor.colors[1]++;
		if(localCurrentColor.colors[1]==255)
		{
			localCurrentColor.colors[1]=0;
			localCurrentColor.colors[2]++;
			if(localCurrentColor.colors[2]==255)
			{
				printf("Too many objects\n");		
			}	
		}	
	
	}
	gfcPickManager::currentColor=localCurrentColor;
	
	//The method below yields 256*3 different colors. 
	/*if(localCurrentColor.colors[0]<256){
		localCurrentColor.colors[0]++;
	}else
	{
		if(localCurrentColor.colors[1]<256){
		localCurrentColor.colors[1]++;
		}else
		{
			if(localCurrentColor.colors[0]<256){
			localCurrentColor.colors[2]++;
			}
			else
			{
				printf("Too many objects\n");	
			}
		}	
	}*/
	
	//printf("Unique Color(%i): %i %i %i\n", localCurrentColor.colors.size()*/, localCurrentColor.colors[0],localCurrentColor.colors[1],localCurrentColor.colors[2]);
	//printf("Unique Color: %i %i %i\n" , localCurrentColor.colors[0],localCurrentColor.colors[1],localCurrentColor.colors[2]);
	
	return localCurrentColor;
}


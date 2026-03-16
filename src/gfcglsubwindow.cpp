#include "gfcglsubwindow.h"
#include <glad/glad.h>

#include "gfcpickmanager.h"
extern gfcPickManager pickManager;

gfcGLSubWindow::gfcGLSubWindow()
{

    	borderWidth=10;
    	windowColor[0]=windowColor[1]=windowColor[2]=40;
//    	windowColorHighlight[0]=windowColorHighlight[1]=windowColorHighlight[2]=windowColor[0]*1.1;
    	resizeColor[0]=resizeColor[1]=resizeColor[2]=128;
    	resizeColor[3]=windowColor[3]=128;
    	scaleX=320.0;
    	scaleY=240.0;
    	posX=0;
    	posY=0;
    	windowSize=1;
    	drawForPicking=0;
		pickWindow.initialize();
		pickCornerResize.initialize();
    
}

void gfcGLSubWindow::initialize()
{
	pickWindow.initialize();
	pickCornerResize.initialize();
}

gfcGLSubWindow::~ gfcGLSubWindow()
{
}


void gfcGLSubWindow::scale(int dx, int dy)
{
	scaleX-=dx;
	if (scaleX<100)
		scaleX=100;

	scaleY-=dy;
	if (scaleY<100)
		scaleY=100;

}

void gfcGLSubWindow::move(int x, int y)
{

	posX-=x;
        posY+=y;
        
        //check boundries
        if(posX+scaleX<-viewport.w/2)
        	posX=-viewport.w/2-scaleX;
        	
        if(posX>viewport.w/2)
        	posX=viewport.w/2;
	
	if(posY<-viewport.h/2)
        	posY=-viewport.h/2;
        	
        if(posY-scaleY>viewport.h/2)
        	posY=viewport.h/2+scaleY;
}

void gfcGLSubWindow::drawBorder()
{
	
	glPushAttrib(GL_ALL_ATTRIB_BITS);
        
    if (drawForPicking==1)
    {
    	glDisable(GL_BLEND);
    }
    else
    {
    	glEnable(GL_BLEND);
        glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    /*glTranslatef(-hWidth/2.0, h,0.0);
    glScalef(scaleX,scaleY,1);
    glTranslatef(hWidth/2.0, -h,0.0);
    glPointSize(15);
    glBegin(GL_POINTS);
    	glVertex3f(0,0,0);
    	glVertex3f(-hWidth,0,0);
    glEnd();*/
    
    //glTranslatef(posX, posY,0.0);
    /*
    glBegin(GL_QUADS);
    glVertex3f(-hWidth/2.0, 0.0, 0.0);
    glVertex3f(-hWidth/2.0, h, 0.0);
    glVertex3f( hWidth/2.0, h, 0.0);
    glVertex3f( hWidth/2.0, 0.0, 0.0);
    glEnd();*/
    
    //draw border, compensating for scale 
        
    float borderWX=borderWidth/scaleX;
    float borderWY=borderWidth/scaleY;
    
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    //
    
    unsigned char tmpWindowColor[4];
    unsigned char tmpResizeColor[4];
    memcpy(tmpWindowColor,windowColor,4);
    memcpy(tmpResizeColor,resizeColor,4);
    
    for(int i=0;i<2;i++){
	if (drawForPicking==0){
        glColor4ubv(windowColor);
	}
    else{
    	//glColor3ub(128,128,128);
		unsigned char tmpColor[3];
		pickWindow.getPickColor(tmpColor);
		glColor3ubv((GLubyte*)tmpColor);
//        glColor3ubv((GLubyte*)pickWindow.getPickColor().colors);
    }
    
    
    
	//Draw the frame
    glBegin(GL_QUADS);
    //left frame
    glVertex3f(-borderWX, 0.0, 0.0);
    glVertex3f(-borderWX, windowSize+borderWY, 0.0);
    glVertex3f(0.0, windowSize+borderWY, 0.0);
    glVertex3f(0.0, 0.0, 0.0);
    
    //top frame
    glVertex3f(0.0, windowSize, 0.0);
    glVertex3f(0.0, windowSize+borderWY, 0.0);
    glVertex3f(windowSize+borderWX, windowSize+borderWY, 0.0);
    glVertex3f(windowSize+borderWX, windowSize, 0.0);
    
    //right frame
    glVertex3f(windowSize, windowSize, 0.0);
    glVertex3f(windowSize+borderWX, windowSize, 0.0);
    glVertex3f(windowSize+borderWX, -borderWY, 0.0);
    glVertex3f(windowSize, -borderWY, 0.0);
    
    //bottom frame
    glVertex3f(windowSize, 0.0, 0.0);
    glVertex3f(windowSize, -borderWY, 0.0);
    glVertex3f(-borderWX, -borderWY, 0.0);
    glVertex3f(-borderWX, 0.0, 0.0);
    
    glEnd();
    
    //DRAW THE CORNER DRAGGER 
    if (drawForPicking==0)
        glColor4ubv(tmpResizeColor);
    else{
    	//glColor3ub(128,128,128);
		unsigned char tmpColor[3];
		pickCornerResize.getPickColor(tmpColor);
        glColor3ubv((GLubyte*)tmpColor);
    }

    glBegin(GL_TRIANGLES);
    glVertex3f(windowSize+borderWX, -borderWY,0.0);
    glVertex3f(windowSize+borderWX, borderWY,0.0);
    glVertex3f(windowSize-borderWX, -borderWY,0.0);
    glEnd();
    
    borderWY*=0.5;
    borderWX*=0.5;
    
    tmpWindowColor[0]=tmpWindowColor[1]=tmpWindowColor[2]=tmpWindowColor[0]*1.6;
    tmpResizeColor[0]=tmpResizeColor[1]=tmpResizeColor[2]=tmpResizeColor[0]*1.6;
    
    }

	//Draw a thin inverse line to make sure we see something, only if not picker.
	if (drawForPicking==0){
		
	tmpWindowColor[0]=tmpWindowColor[1]=tmpWindowColor[2]=255-tmpWindowColor[0];
		
		glColor3ubv((GLubyte*)tmpWindowColor);
	glBegin(GL_LINE_LOOP);
	//left frame
	glVertex3f(-borderWX, -borderWY, 0.0);
	glVertex3f(-borderWX, windowSize+borderWY, 0.0);
	glVertex3f(windowSize+borderWX, windowSize+borderWY, 0.0);
	glVertex3f(windowSize+borderWX, -borderWY, 0.0);
	glEnd();
	}
    
    glPopAttrib();
}

void gfcGLSubWindow::beginTransform()
{
	glPushMatrix();
	
	glTranslatef(posX, posY,0.0);
	glTranslatef(0.0, 1,0.0);
	glScalef(scaleX,scaleY,1.0);
	glTranslatef(0.0, -1,0.0);
}

void gfcGLSubWindow::endTransform()
{
	glPopMatrix();
}

int gfcGLSubWindow::draw()
{
	if(isVisible){
	beginTransform();
	drawBorder();
	drawContent();
	endTransform();
	}
	return 0;
}

int gfcGLSubWindow::pickNotify(gfcPickNotifyParameters & params)
{

	gfcPickObjectStatus windowStatus=pickWindow.getStatus(params);
	move(windowStatus.dx, windowStatus.dy);

	
	gfcPickObjectStatus cornerStatus=pickCornerResize.getStatus(params);
	scale(cornerStatus.dx, cornerStatus.dy);

	gfcPickObjectStatus closeStatus=pickClose.getStatus(params);
	
	picked = windowStatus.clicked || cornerStatus.clicked || closeStatus.clicked;
	
	return picked;
	

	/*  switch ( params.event ) {
    case GFC_PICK_EVENT_CLICK_DOWN:
        //Did they click on the window border?
        if (params.pickedColor==windowPickColor)
            windowClicked=1;
        else
            windowClicked=0;

        //did they click on the window's drag corner?
        if (params.pickedColor==resizePickColor)
            resizeClicked=1;
        else
            resizeClicked=0;
        break;

    case GFC_PICK_EVENT_CLICK_UP:
        resizeClicked=0;
        windowClicked=0;
        break;

    case GFC_PICK_EVENT_DRAG:
        //drag the histogram
        if (windowClicked) {
            	    move(params.dx, params.dy);
        }

        if (resizeClicked) {
            //printf("HistogramPos %i %i\n",histogramPosX,histogramPosY);
			
			

          


        break;
    }*/
}

bool gfcGLSubWindow::visible()
{
	return isVisible;
}

void gfcGLSubWindow::visible(bool value)
{
	isVisible=value;
}






#include "gfchistogram.h"
#include <FL/gl.h>

#include "gfcpickmanager.h"
extern gfcPickManager pickManager;

gfcHistogram::~gfcHistogram() {
    /*for ( int i=0; i<3; i++ ) {
        delete [] valuesA[i];
    }
    delete [] valuesA;*/
}

gfcHistogram::gfcHistogram() {
    /*std::vector<int> sampleChannel;
    sampleChannel.resize(width,0);
    values.resize(4,sampleChannel);*/

    /*valuesA = new float*[3];
    for ( int i=0; i<3; i++ ) {
        valuesA[i]=new float[width];
        for ( int j=0;j<width ;j++ ) {
            valuesA[i][j]=0;
        }
    }*/


    hWidth=256;
    scaleX=3;
    scaleY=3;
    posX=0;
    posY=0;
    quality=16;
    drawForPicking=0;
    memset(valuesA,0,sizeof(float)*3*hWidth);

}

gfcHistogram& gfcHistogram::operator =(const gfcHistogram &theHist) {

    hWidth=theHist.hWidth;
    scaleX=theHist.scaleX;
    scaleY=theHist.scaleY;
    posX=theHist.posX;
    posY=theHist.posY;
    drawForPicking=theHist.drawForPicking;
    quality=theHist.quality;
    memcpy(valuesA,theHist.valuesA,sizeof(float)*3*theHist.hWidth);
	return *this;
}

void gfcHistogram::processPixels(int width, int height, unsigned char * pixels) {
    int skip=4*quality; //4channels times subsampling
    for ( int i=width*height*4-1; i>=0;) {
        //(valuesA[3])[(pixels[i])]++;
        //i--;
        (valuesA[2])[(pixels[i-1])]++;
        //i--;
        (valuesA[1])[(pixels[i-2])]++;
        //i--;
        (valuesA[0])[(pixels[i-3])]++;
        //i--;
        i-=skip;
    }

    normalize();

}

void gfcHistogram::processPixels(int width, int height, unsigned short * pixels) {
    int skip=4*quality; //4channels times subsampling
    for ( int i=width*height*4-1; i>=0;) {
        //(valuesA[3])[(pixels[i])]++;
        //i--;
        (valuesA[2])[(int)(pixels[i-1]*hWidth)]++;
        //i--;
        (valuesA[1])[(int)(pixels[i-2]*hWidth)]++;
        //i--;
        (valuesA[0])[(int)(pixels[i-3]*hWidth)]++;
        //i--;
        i-=skip;
    }

    normalize();

}

void gfcHistogram::draw(int caching) {

    //draw background of hWidth x hWidth*/2.35 (make it wider than taller 1:2.35 is nice)
    float h=hWidth;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    if (drawForPicking==0) {
        
        glEnable(GL_BLEND);
        glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    else
    {
    	 glDisable(GL_BLEND);
    }

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    
    
    
    
    
    //BACKGROUND
    if (drawForPicking==0)
        glColor4f(0.065,0.065,0.065,0.4);
    else{
    	//glColor3ub(128,128,128);
        //glColor3ubv((GLubyte*)pickWindow.getPickColor.colors);
    	//printf("Histogram.myUniqueColor: %i %i %i\n",myUniqueColor.colors[0],myUniqueColor.colors[1],myUniqueColor.colors[2]);
    }
    
    glBegin(GL_QUADS);
    	glVertex3f(0.0,0.0,0.0);
    	glVertex3f(1,0.0,0.0);
    	glVertex3f(1,1.0,0.0);
    	glVertex3f(0.0,1.0,0.0);
    glEnd();
    
    
    //END OF BACKGROUND
    

        //glPushMatrix();
        //glTranslatef(posX, posY,0.0);
    
    //move the scale pivot and scale
    

    //DRAW THE ACTUAL HISTOGRAM BARS FOR EACH COLOR
    if (drawForPicking==0) {
        float xPos=0;
        glColor4f(1.0,0.0,0.0,1.0);
        float colorIntensity=0.7;
        float colors[12]={colorIntensity,0.0,0.0,0.3,   0.0,colorIntensity,0.0,0.3,   0.0,0.0,colorIntensity,0.4};
        float entryColor=0.0;

        glBlendFunc (GL_SRC_COLOR,GL_ONE_MINUS_SRC_COLOR); //additive blending
        //glBlendFunc (GL_ONE_MINUS_SRC_COLOR,GL_SRC_COLOR); //substractive blending?
		
        for ( int i=0;i<3 ;i++ ) {
            glColor4fv(&colors[i*4]);
            int r,g,b;
            r= (i==0);
            g= (i==1);
            b= (i==2);

            glBegin(GL_QUADS);
            for ( int j=0; j<hWidth;j++ ) {

                /*entryColor=(float)j*1.5/(float)hWidth;
                glColor4f(entryColor*r,entryColor*g,entryColor*b,0.3);*/

                //xPos=(float)j;

                glVertex3f((float)(j)/(float)hWidth,0.0,0);
                glVertex3f((float)(j)/(float)hWidth,valuesA[i][j],0);
                glVertex3f((float)(j+1.0)/(float)hWidth,valuesA[i][j],0);
                glVertex3f((float)(j+1.0)/(float)hWidth,0.0,0);
            }
            glEnd();

            //draw the last bottom point to close the triangle strip
            //glVertex3f(hWidth/2.0,0.0,0.0);

        }

        if (caching==1) {
            glPushAttrib(GL_ALL_ATTRIB_BITS);
            glEnable(GL_BLEND);
            glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            gl_font(FL_TIMES + FL_BOLD,10);
            gl_font(FL_HELVETICA + FL_BOLD,10);
            // gl_font(FL_COURIER + FL_BOLD,12);

            glColor4f(1.0,1.0,1.0,0.8);



//-hWidth*scale/2.0
//scale*h/2.0-16
            /*gl_draw("Caching Histogram",
                    -hWidth/2.0,h100,20,

            Fl_Align(FL_ALIGN_LEFT| FL_ALIGN_TOP | FL_ALIGN_WRA/));*/
	    
            gl_draw("Caching Histogram",(float)0.0,(float)0.0);

            glPopAttrib();
        }//END OF DRAWING CACHING
        
    }
    //glPopMatrix();

    glPopAttrib();







    //draw each channel as a series of lines





}

void gfcHistogram::normalize() {
//1. Normalize values.
    //1.1 Find maximum
    maxValue=0;
    for ( int i=0; i<hWidth ;i++ ) {
        for ( int j=0; j<3;j++ ) {
            if (valuesA[j][i]>maxValue) {
                maxValue=valuesA[j][i];
                //printf("maxValue: %f\n",maxValue);
            }
        }
    }

    //2.2 Normalize
    float divider=1.0/maxValue;
    for ( int i=0; i<hWidth ;i++ ) {
        for ( int j=0; j<3;j++ ) {
            valuesA[j][i]*=divider;
        }
    }
}





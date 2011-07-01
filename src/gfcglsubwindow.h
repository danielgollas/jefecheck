#ifndef GFCGLSUBWINDOW_H
#define GFCGLSUBWINDOW_H

#include "gfcpickdrawee.h"
#include "gfcpicknotifee.h"
#include "gfcrectang.h"
#include "gfcpickobject.h"

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcGLSubWindow{
public:
    gfcGLSubWindow();
    

    ~gfcGLSubWindow();
     
    virtual int drawContent()=0;
    int draw();
    
    void beginTransform();
    void endTransform();
    
    void scale(int x, int y);
    void move(int x, int y);
    
	void initialize();

    unsigned char windowColor[4];
    unsigned char resizeColor[4];
    
    int borderWidth;
    int windowSize;
    
    gfcRectang viewport; //needs to be set correctly so that the plate knows how and where to draw itself.
    
    int drawForPicking;
    void drawBorder();
    
    int pickNotify(gfcPickNotifyParameters & params);
    
    int picked; //1 when some part of the window has been clicked, 0 otherwise.
    
    bool visible();
    void visible(bool value);
    
private:
    int posX;
    int posY;
    
    float scaleX;
    float scaleY;
    
	gfcPickObject pickWindow;
	gfcPickObject pickCornerResize;
	gfcPickObject pickClose;

    /*gfcPickColor windowPickColor;
    gfcPickColor resizePickColor;
    gfcPickColor closePickColor;*/
    
    bool isVisible;


};

#endif

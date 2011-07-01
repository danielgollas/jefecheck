#ifndef GFCHISTOGRAMGLWINDOW_H
#define GFCHISTOGRAMGLWINDOW_H

#include "gfcglsubwindow.h"
#include "gfchistogram.h"

/**
	@author Daniel Gollas Gilman <gollas@jefecorp.com>
*/
class gfcHistogramGLWindow : public gfcGLSubWindow
{
public:
    gfcHistogramGLWindow();

    ~gfcHistogramGLWindow();
    
    //int draw();
    virtual int drawContent();
    int draw(gfcHistogram *theHist, int pcaching);
    
    gfcHistogram *currentHistogram;

private:
int caching;

};

#endif

#include "gfchistogramglwindow.h"


int gfcHistogramGLWindow::drawContent()
{
	//TODO: Draw the histogram
	if(currentHistogram==NULL)
		return 0;
		
	currentHistogram->drawForPicking=this->drawForPicking;
	currentHistogram->draw(caching);
	
}

int gfcHistogramGLWindow::draw(gfcHistogram *theHist, int pcaching)
{
	currentHistogram=theHist;
	caching=pcaching;
	return gfcGLSubWindow::draw();
	
}

gfcHistogramGLWindow::gfcHistogramGLWindow()
{
}

gfcHistogramGLWindow::~ gfcHistogramGLWindow()
{
}



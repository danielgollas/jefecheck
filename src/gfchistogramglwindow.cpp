// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfchistogramglwindow.h"


int gfcHistogramGLWindow::drawContent()
{
	//TODO: Draw the histogram
	if(currentHistogram==NULL)
		return 0;
		
	currentHistogram->drawForPicking=this->drawForPicking;
	currentHistogram->draw(caching);
	return 1;
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



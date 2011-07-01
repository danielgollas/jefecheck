#include "gfcsequencegui.h"

gfcSequenceGUI::gfcSequenceGUI()
{
}


gfcSequenceGUI::~gfcSequenceGUI()
{
}

gfcRectang gfcSequenceGUI::getAOI()
{
	return aoi;
}

void gfcSequenceGUI::setAOI(int x, int y, int w, int h)
{
	setAOI(gfcRectang(x,y,w,h));
}

void gfcSequenceGUI::setAOI(gfcRectang rectang)
{
	aoi=rectang;
}

void gfcSequenceGUI::setAOI(void)
{
	setAOI(gfcRectang(-1,-1,-1,-1));
}



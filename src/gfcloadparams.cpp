#include "gfcloadparams.h"

gfcLoadParams::gfcLoadParams()
{
	//aoi.set(-1,-1,-1,-1);
	append=0;
	channel=0;
	compressed=0;
	//defog=exposition=kneeH=kneeL=0;
	fileName="";
	loadFromTimeline=0;
	scale=100;
	stream=false;
	fromFrame=toFrame=-1;
	gamma=1;
	filterType=0;
	channelName="";
	crop=false;
	//gammaLUT=NULL;
}


gfcLoadParams::~gfcLoadParams()
{
}

void gfcLoadParams::fixWindowsPath()
{
	while (fileName.find('\\') != std::string::npos) {
		fileName.replace(fileName.find('\\'), 1, "/");
	}
}

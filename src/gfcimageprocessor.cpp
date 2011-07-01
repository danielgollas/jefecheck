#include "gfcimageprocessor.h"

gfcImageProcessor::gfcImageProcessor(GFLC_BITMAP* theBitmap)
{
	bitmap=theBitmap;
}


gfcImageProcessor::~gfcImageProcessor()
{
}

void gfcImageProcessor::crop(gfcRectang rectang)
{
	if(rectang.x!=-1 && rectang.y!=-1 && rectang.w!=-1 && rectang.h!=-1)
	{
		GFL_RECT cropRect;
		cropRect.x=rectang.x;
		cropRect.y=bitmap->getHeight()-rectang.y-rectang.h; //gfl uses top right image origin, so convert Y origin from bottom right
		cropRect.w=rectang.w;
		cropRect.h=rectang.h;
		bitmap->crop ( cropRect );
	}
}

void gfcImageProcessor::scale(float scale, int filterType)
{
	if(bitmap && scale!=100)
	{
		bitmap->resize ( scale/100.0*bitmap->getWidth(),scale/100.0*bitmap->getHeight(),filterType );
	}
}

void gfcImageProcessor::gamma(float* gammaLUT)
{
	if(gammaLUT)
	{
		//do the gamma
		
	}
}

void gfcImageProcessor::applyLuts(std::vector<int> lutList)
{
	//apply luts
}

void gfcImageProcessor::convertToNBits(int nBits)
{
	//convert To N Bits
}

void gfcImageProcessor::process(gfcLoadParams params)
{
	/*if(params.scale!=100)
	{
		scale(params.scale, params.filterType);
	}
	if(params.gamma!=1)
	{
		gamma(params.gammaLUT);
	}*/
	
	
}


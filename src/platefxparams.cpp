#include "platefxparams.h"
#include <stdlib.h>
#include <string.h>
PlateFXParams::PlateFXParams()
{
	numberOfTextures=0;
	for(int i=0;i<4;i++)
	{
		texCoords[i].r=0;
		texCoords[i].s=0;
		texCoords[i].t=0;
		texCoords[i].x=0;
		texCoords[i].y=0;
		texCoords[i].z=0;
	}
}


PlateFXParams::~PlateFXParams()
{
}

const PlateFXParams & PlateFXParams::operator =(const PlateFXParams & params)
{
	sizeX=params.sizeX;
	sizeY=params.sizeY;
	pass=params.pass;
	numberOfTextures=params.numberOfTextures;
	memcpy(texCoords,params.texCoords,sizeof(FXTexCoords)*4);
	currentFrame=params.currentFrame;
	FBOTextureID=params.FBOTextureID;
	return params;
}



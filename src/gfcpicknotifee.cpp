#include "gfcpicknotifee.h"

gfcPickColor::gfcPickColor()
{
	colors[0]=colors[1]=colors[2]=0;
	/*colors.push_back(0);
	colors.push_back(0);
	colors.push_back(0);*/

}

gfcPickColor::gfcPickColor(unsigned char tmpColor[3])
{	/*colors.push_back(tmpColor[0]);
	colors.push_back(tmpColor[1]);
	colors.push_back(tmpColor[2]);*/

	colors[0]=tmpColor[0];
	colors[1]=tmpColor[1];
	colors[2]=tmpColor[2];
}

gfcPickColor& gfcPickColor::operator=(const gfcPickColor &aColor){
	//memcpy(this->colors,aColor.colors,3);
	colors[0]=aColor.colors[0];
	colors[1]=aColor.colors[1];
	colors[2]=aColor.colors[2];
	return *this;
}






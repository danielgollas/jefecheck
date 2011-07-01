#include "gfcrectang.h"
#include <stdio.h>

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

gfcRectang::~gfcRectang()
{
}

void 	gfcRectang::set(int X, int Y, int W, int H)
		{
			x=X;
			y=Y;
			w=W;
			h=H;
		}


bool gfcRectang::intersects(const gfcRectang &b)
{
	int r1L, r1R, r1B, r1T;
	int r2L, r2R, r2B, r2T;

	r1L=x;
	r1R=x+w-1;
	r1B=y;
	r1T=y+h-1;
	
	r2L=b.x;
	r2R=b.x+b.w-1;
	r2B=b.y;
	r2T=b.y+b.h-1;
	printf("r1: %i %i %i %i\nr2: %i %i %i %i",r1L, r1B, r1T, r1R,r2L, r2B, r2T, r2R);
	
	return ! ( r2L > r1R
		|| r2R< r1L
		|| r2T > r1B
		|| r2B < r1T
		);
}

gfcRectang gfcRectang::intersection(const gfcRectang &b)
{
	gfcRectang result(-1,-1,-1,-1);
	
	int r1L, r1R, r1B, r1T;
	int r2L, r2R, r2B, r2T;
	int resL, resR, resB, resT;

	r1L=x;
	r1R=x+w-1;
	r1B=y;
	r1T=y+h-1;

	r2L=b.x;
	r2R=b.x+b.w-1;
	r2B=b.y;
	r2T=b.y+b.h-1;


	//printf("r1: %i %i %i %i\nr2: %i %i %i %i\n",r1L, r1B, r1T, r1R,r2L, r2B, r2T, r2R);

	resL=max(r1L, r2L);
	resT=min(r1T, r2T);
	resB=max(r1B, r2B);
	resR=min(r1R, r2R);
	
	//printf("result: L%i B%i R%i T%i\n", resL, resB, resR, resT);
	//check for intersection
	if(resR > resL && resT > resB)
	{	//the intersection is not empty
		
		result.x=resL;
		result.y=resB;
		result.h=resT-resB;
		result.w=resR-resL;
	}
	

	return result;

	/*//find if they intersect, if they do find intersection
	if (intersects(b))
	{
		
	}
	
	return result;

	//if they do intersect, find the intersection*/
}

void gfcRectangf::set(float X, float Y, float W, float H)
{
			x=X;
			y=Y;
			w=W;
			h=H;
}

gfcRectangf::~ gfcRectangf()
{
}
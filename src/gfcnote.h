#ifndef GFCNOTE_H
#define GFCNOTE_H

#include <string>

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
	@brief Base class for all other note types.
*/



class gfcNote
{
	public:
		gfcNote();

		~gfcNote();
		
		virtual void draw()=0;
		virtual void fillSpecificPane()=0;
		
		unsigned char type; //text, drawing, aniline etc.
		std::string name;
		float posX;
		float posY;
		int quadID;
		int size;
		int from;
		int to;
		bool always;
		float colorR;
		float colorG;
		float colorB;
		
};

#endif

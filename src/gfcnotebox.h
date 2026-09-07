#ifndef GFCNOTEBOX_H
#define GFCNOTEBOX_H

#include "gfcnote.h"

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
	@brief A box note: two opposite corners, in normalised image space.
*/
class gfcNoteBox : public gfcNote
{
	public:
		gfcNoteBox();
		~gfcNoteBox() override;

		gfcNoteType noteType() const override;
		std::vector<gfcNotePoint> points() const override;

		gfcNotePoint a;
		gfcNotePoint b;
};

#endif

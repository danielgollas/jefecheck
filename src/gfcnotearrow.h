#ifndef GFCNOTEARROW_H
#define GFCNOTEARROW_H

#include "gfcnote.h"

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
	@brief An arrow note: tail and head, in normalised image space.
*/
class gfcNoteArrow : public gfcNote
{
	public:
		gfcNoteArrow();
		~gfcNoteArrow() override;

		gfcNoteType noteType() const override;
		std::vector<gfcNotePoint> points() const override;

		gfcNotePoint tail;
		gfcNotePoint head;
};

#endif

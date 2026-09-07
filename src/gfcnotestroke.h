#ifndef GFCNOTESTROKE_H
#define GFCNOTESTROKE_H

#include "gfcnote.h"

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
	@brief A freehand note: an ordered list of points, in normalised image space.
*/
class gfcNoteStroke : public gfcNote
{
	public:
		gfcNoteStroke();
		~gfcNoteStroke() override;

		gfcNoteType noteType() const override;
		std::vector<gfcNotePoint> points() const override;

		std::vector<gfcNotePoint> pts;
};

#endif

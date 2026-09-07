#ifndef GFCNOTETEXT_H
#define GFCNOTETEXT_H

#include "gfcnote.h"

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
	@brief A text note: an anchor point plus a string, in normalised image space.
*/
class gfcNoteText : public gfcNote
{
	public:
		gfcNoteText();
		~gfcNoteText() override;

		gfcNoteType noteType() const override;
		std::vector<gfcNotePoint> points() const override;

		gfcNotePoint anchor;
		std::string text;
};

#endif

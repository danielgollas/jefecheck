#include "gfcnotetext.h"

gfcNoteText::gfcNoteText()
{
}

gfcNoteText::~gfcNoteText()
{
}

gfcNoteType gfcNoteText::noteType() const
{
	return GFCNOTE_TEXT;
}

std::vector<gfcNotePoint> gfcNoteText::points() const
{
	return { anchor };
}

#include "gfcnotearrow.h"

gfcNoteArrow::gfcNoteArrow()
{
}

gfcNoteArrow::~gfcNoteArrow()
{
}

gfcNoteType gfcNoteArrow::noteType() const
{
	return GFCNOTE_ARROW;
}

std::vector<gfcNotePoint> gfcNoteArrow::points() const
{
	return { tail, head };
}

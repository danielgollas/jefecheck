// SPDX-License-Identifier: GPL-2.0-or-later
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

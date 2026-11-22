// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfcnotestroke.h"

gfcNoteStroke::gfcNoteStroke()
{
}

gfcNoteStroke::~gfcNoteStroke()
{
}

gfcNoteType gfcNoteStroke::noteType() const
{
	return GFCNOTE_STROKE;
}

std::vector<gfcNotePoint> gfcNoteStroke::points() const
{
	return pts;
}

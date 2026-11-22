// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfcnotebox.h"

gfcNoteBox::gfcNoteBox()
{
}

gfcNoteBox::~gfcNoteBox()
{
}

gfcNoteType gfcNoteBox::noteType() const
{
	return GFCNOTE_BOX;
}

std::vector<gfcNotePoint> gfcNoteBox::points() const
{
	return { a, b };
}

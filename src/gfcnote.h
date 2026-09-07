#ifndef GFCNOTE_H
#define GFCNOTE_H

#include <string>
#include <vector>
#include "gfcNoteGeometry.h"

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
	@brief Base class for all note types (freehand, arrow, box, text).

	Geometry lives in the concrete subclasses (gfcNoteStroke, gfcNoteArrow,
	gfcNoteBox, gfcNoteText); points() exposes it uniformly, always in
	normalised image space. See
	docs/superpowers/specs/2026-09-07-annotations-design.md.
*/

/** One value per concrete gfcNote subclass. Mirrors noteType(). */
enum gfcNoteType
{
	GFCNOTE_STROKE = 0,
	GFCNOTE_ARROW,
	GFCNOTE_BOX,
	GFCNOTE_TEXT
};

class gfcNote
{
	public:
		gfcNote();

		virtual ~gfcNote();

		/** Which concrete subclass this is. */
		virtual gfcNoteType noteType() const = 0;

		/** Every point this note is made of, in normalised image space, in draw order. */
		virtual std::vector<gfcNotePoint> points() const = 0;

		/** True when this note should be drawn on the given frame. */
		bool visibleOnFrame(int frame) const;

		std::string id;         // UUID, assigned at construction
		std::string author;
		std::string name;
		int   quadID = 0;
		int   from = 0, to = 0;
		bool  always = false;
		float colorR = 1.0f, colorG = 0.2f, colorB = 0.2f;
		int   size = 3;
};

/** Runs the note/revision model self-test; prints NOTE-MODEL: pass=N fail=N
    and returns non-zero if any check failed. Wired to --notes-test in a
    later task. */
int noteModelSelfTest();

#endif

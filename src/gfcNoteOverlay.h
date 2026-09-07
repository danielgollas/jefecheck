#ifndef GFCNOTEOVERLAY_H
#define GFCNOTEOVERLAY_H

#include <vector>

#include "gfcnote.h"
#include "gfcNoteGeometry.h"

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
	@brief Draws notes with immediate-mode GL, for both the on-screen plate
	       overlay and the export composite.

	Notes are stored in normalised image space (0..1). Everything here does
	one job: map that onto a rectangle expressed in whatever GL coordinate
	system the caller has current, and stroke it.

	This must never run through the super-shader — if markup went through
	colour correction, a note's red would shift when exposure is pulled and
	the markup would start lying about itself. The caller places the call
	after FXPASS_LAST; draw() additionally clears the active shader program
	for the duration, the way GfcTextRenderer does before its own quads.
	See docs/superpowers/specs/2026-09-07-annotations-design.md
	("Rendering — on screen").

	This header deliberately pulls in no GL headers, so it is safe to include
	from any translation unit (developer_notes.md §1).
*/
namespace gfcNoteOverlay
{
	/** Where normalised 0..1 lands, in the caller's current GL coordinates.
	    x/y is the origin (the 0,0 corner) and w/h the extent. Either extent
	    may be negative, which is how a caller expresses a flip. */
	struct Rect
	{
		float x = 0.0f;
		float y = 0.0f;
		float w = 0.0f;
		float h = 0.0f;
	};

	/**
		Draws every note in @a notes that is visible on @a frame and whose
		quadID matches @a quadID, mapping normalised coordinates onto
		@a target.

		Saves and restores the active shader program and every piece of GL
		state it touches — the caller is mid-render and goes on drawing
		afterwards. Null entries in @a notes are skipped.

		Requires a current GL context.
	*/
	void draw(const std::vector<const gfcNote*>& notes,
	          int frame, int quadID, const Rect& target);

	/** Maps one normalised point onto @a target. Pure arithmetic, exposed so
	    the geometry can be tested without a GL context. */
	gfcNotePoint mapPoint(const gfcNotePoint& p, const Rect& target);
}

/** Runs the overlay geometry self-test; prints NOTE-OVERLAY: pass=N fail=N
    and returns non-zero if any check failed. Geometry only — there is no
    headless GL context here, so pixel proof lives in the render test. */
int noteOverlaySelfTest();

#endif

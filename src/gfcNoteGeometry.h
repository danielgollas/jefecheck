#ifndef GFCNOTEGEOMETRY_H
#define GFCNOTEGEOMETRY_H

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
	@brief A single point of note geometry.

	Always normalised image space: x and y each range 0.0..1.0 across the
	source image, never screen pixels and never source-resolution pixels.
	This is what lets a note stick to the image through pan, zoom, flip,
	flop, aspect changes, a different display resolution, and export at a
	different size. See
	docs/superpowers/specs/2026-09-07-annotations-design.md ("Geometry").
*/
struct gfcNotePoint
{
	float x = 0.0f;
	float y = 0.0f;
};

#endif

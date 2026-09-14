#ifndef GFCNOTESTAMP_H
#define GFCNOTESTAMP_H

#include <string>
#include <vector>

class gfcReview;

/**
 * Attach annotations to an EXR that already exists.
 *
 * Deliberately a STAMP, not a render. Notes arrive after the render -- you
 * publish dailies, then people mark them up -- so producing the annotated
 * frame by re-rendering would re-run the FX stack and the super-shader, and
 * the beauty pixels could come back subtly different from the ones everybody
 * reviewed. Here the source pixels are copied through byte for byte and only
 * the header and an extra layer are added.
 *
 * Two things are written, for two different readers:
 *
 *   jefecheck:notes   a JSON header attribute carrying the geometry, so the
 *                     notes travel with the frame and stay editable. EXR
 *                     string attributes are standard and OIIO passes them
 *                     through, but be aware most comp tools DROP unknown
 *                     attributes on re-render -- this is durable for archive
 *                     and handoff, not for a round trip through a comp.
 *
 *   notes.R/G/B/A     the markup rasterised into its own layer, so anyone
 *                     with any EXR viewer can toggle it on and off without
 *                     knowing anything about JefeCheck. Non-destructive: the
 *                     beauty channels are untouched.
 *
 * Rasterising the layer needs a GL context, which this module does not have
 * and does not want -- so the caller renders the markup and hands the pixels
 * in. That keeps everything here pure CPU and testable headlessly.
 */
namespace gfcNoteStamp
{
	struct Options
	{
		/** Write the JSON geometry into the EXR header. */
		bool writeHeader = true;
		/**
		 * Premultiplied RGBA for the notes layer, row-major, top row first,
		 * exactly layerWidth * layerHeight * 4 bytes. Empty means header only.
		 */
		std::vector<unsigned char> layerRGBA;
		int layerWidth = 0;
		int layerHeight = 0;
	};

	/**
	 * Copies `inExr` to `outExr`, adding whatever `opt` asks for. Returns
	 * false and fills `err` on any failure; never partially writes over an
	 * existing output.
	 */
	bool stamp(const std::string& inExr, const std::string& outExr,
	           const gfcReview& review, const Options& opt, std::string* err);

	/** Reads jefecheck:notes back out of an EXR. Empty when absent. */
	std::string readEmbeddedJson(const std::string& exrPath);

	/** Pixel dimensions of an image, read from its header without its pixels. */
	bool imageSize(const std::string& path, int* width, int* height);
}

/** Self-test: stamps a generated EXR and reads the attribute back. */
int noteStampSelfTest();

#endif

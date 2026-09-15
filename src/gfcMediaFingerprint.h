#ifndef GFCMEDIAFINGERPRINT_H
#define GFCMEDIAFINGERPRINT_H

#include <map>
#include <string>
#include <vector>

/**
	@brief Content identity for a media sequence, independent of its file names
	and location, so a review package can find moved media again. "fp1:" +
	SHA-1 over the tag "jefecheck-fp1", the first frame's width, height and
	channel names, the frame count, and every 16th row and column (channels
	0-2, as float) of the first, middle and last frames.
	See docs/superpowers/specs/2026-09-14-review-package-design.md.
*/
namespace gfcMediaFingerprint
{
	struct Probe
	{
		int width = 0;
		int height = 0;
		std::vector<std::string> channels;
	};

	/** Reads a frame's header only, no pixels. */
	bool probe(const std::string& framePath, Probe& out, std::string* err);

	/** The fingerprint of the sequence whose frames, in frame order, are
	    `framePaths`. Empty (with `err` set) on failure. */
	std::string compute(const std::vector<std::string>& framePaths, std::string* err);

	/** Every file sequence in `dir` (and below when `recursive`), keyed by
	    gfcNoteStore::normalisePath, frames sorted by path. Skips JefeCheck's own
	    .jnotes, .jcs, .jcreview and .partial files. */
	std::map<std::string, std::vector<std::string>> sequencesIn(const std::string& dir, bool recursive);
}

/** Prints NOTE-FINGERPRINT: pass=N fail=N; returns the fail count. */
int mediaFingerprintSelfTest();

#endif

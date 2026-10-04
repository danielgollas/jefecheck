#ifndef GFCREVIEW_H
#define GFCREVIEW_H

#include "gfcrevision.h"
#include <vector>
#include <string>
/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
	@brief One per piece of footage: the sequence it's about, plus every
	round of notes drawn on it.
*/
class gfcReview{
public:
    gfcReview();

    ~gfcReview();

    std::string mediaPath;    // normalised sequence pattern -- the identity
    std::string fingerprint;  // hash of sampled frames, for re-link when the path misses
    std::vector<gfcRevision> revisions;

    /** The last unlocked revision, or nullptr if every revision is locked
        (or there are none). */
    gfcRevision* openRevision();

    /** Appends a new revision authored by `author`, stamps its created/modified
        time, and returns a reference to it. */
    gfcRevision& beginRevision(const std::string& author);
};

#endif

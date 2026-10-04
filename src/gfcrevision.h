#ifndef GFCREVISION_H
#define GFCREVISION_H

#include <vector>
#include <string>
#include <memory>
#include <time.h>
#include "gfcnote.h"
/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
	@brief One round of notes, in time. Locking finalises the round: a
	locked revision refuses add/remove but its notes stay visible.
*/
class gfcRevision{
public:
    gfcRevision();

    std::string id, author;
    time_t created = 0, modified = 0;
    bool locked = false;
    std::vector<std::unique_ptr<gfcNote>> notes;

    /** Returns false and changes nothing when locked, or when n is null. */
    bool addNote(std::unique_ptr<gfcNote> n);
    /** Returns false and changes nothing when locked, or when no note has this id. */
    bool removeNote(const std::string& noteId);
};

#endif

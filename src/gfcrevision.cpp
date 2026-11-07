#include "gfcrevision.h"

#include <algorithm>
#include <random>

namespace
{
	// Same shape as gfcNote's id generator (see gfcnote.cpp), duplicated locally
	// for a ten-line helper.
	std::string generateRevisionId()
	{
		static const char hexDigits[] = "0123456789abcdef";
		std::random_device rd;
		std::mt19937_64 gen(rd());
		std::uniform_int_distribution<int> dist(0, 15);

		std::string out;
		out.reserve(32);
		for (int i = 0; i < 32; ++i)
		{
			out.push_back(hexDigits[dist(gen)]);
		}
		return out;
	}
}

gfcRevision::gfcRevision()
	: id(generateRevisionId())
{
}

// No user-declared destructor: the notes vector cleans itself up, and gfcRevision
// keeps implicit move (it lives by value in gfcReview::revisions) with copy deleted.

bool gfcRevision::addNote(std::unique_ptr<gfcNote> n)
{
	if (locked || !n)
	{
		return false;
	}
	notes.push_back(std::move(n));
	return true;
}

bool gfcRevision::removeNote(const std::string& noteId)
{
	if (locked)
	{
		return false;
	}

	auto it = std::find_if(notes.begin(), notes.end(),
		[&](const std::unique_ptr<gfcNote>& n) { return n->id == noteId; });

	if (it == notes.end())
	{
		return false;
	}

	notes.erase(it);
	return true;
}

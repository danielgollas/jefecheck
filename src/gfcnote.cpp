#include "gfcnote.h"

#include <random>

namespace
{
	// A 32-hex-character random id. Not a cryptographic UUID, just enough
	// entropy that two notes drawn anywhere, ever, don't collide, and
	// idempotent for the add/remove sync messages that will reference it.
	std::string generateNoteId()
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

gfcNote::gfcNote()
	: id(generateNoteId())
{
}

gfcNote::~gfcNote()
{
}

bool gfcNote::visibleOnFrame(int frame) const
{
	if (always)
	{
		return true;
	}
	return frame >= from && frame <= to;
}

// ---------------------------------------------------------------------------
// Self-test
//
// Exercises visibleOnFrame(), note id assignment, and gfcRevision's
// addNote()/removeNote() lock semantics. No GL and no file I/O — that is
// Task 2 (store) and Task 3 (overlay)'s job. Wired to the --notes-test CLI
// flag by Task 7; until then, prove it compiles and run it via a scratch
// harness outside the repo.
// ---------------------------------------------------------------------------

#include "gfcnotestroke.h"
#include "gfcrevision.h"

#include <cstdio>
#include <memory>

int noteModelSelfTest()
{
	int pass = 0;
	int fail = 0;
	auto check = [&](bool cond, const char* msg)
	{
		if (cond)
		{
			++pass;
		}
		else
		{
			++fail;
			std::fprintf(stderr, "NOTE-MODEL FAIL: %s\n", msg);
		}
	};

	gfcNoteStroke s;
	check(!s.visibleOnFrame(5), "a note outside its range is not visible");
	s.from = 1; s.to = 10;
	check(s.visibleOnFrame(5), "a note inside its range is visible");
	check(!s.visibleOnFrame(11), "the range is inclusive at the top");
	s.always = true;
	check(s.visibleOnFrame(999), "an always note ignores the range");

	gfcRevision r;
	auto n = std::make_unique<gfcNoteStroke>();
	const std::string nid = n->id;
	check(!nid.empty(), "a note gets an id at construction");
	check(r.addNote(std::move(n)), "an open revision accepts a note");
	r.locked = true;
	check(!r.addNote(std::make_unique<gfcNoteStroke>()), "a locked revision refuses a note");
	check(!r.removeNote(nid), "a locked revision refuses a removal");

	std::printf("NOTE-MODEL: pass=%d fail=%d\n", pass, fail);
	return fail == 0 ? 0 : 1;
}

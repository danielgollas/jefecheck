#include "gfcNoteMerge.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gfcreview.h"
#include "gfcrevision.h"
#include "gfcnote.h"
#include "gfcnotestroke.h"

gfcNoteMerge::Result gfcNoteMerge::mergeInto(gfcReview& local, gfcReview&& incoming)
{
	Result result;
	if (local.fingerprint.empty())
	{
		local.fingerprint = incoming.fingerprint;
	}
	for (gfcRevision& inRev : incoming.revisions)
	{
		const auto existing = std::find_if(local.revisions.begin(), local.revisions.end(),
										   [&inRev](const gfcRevision& r) { return r.id == inRev.id; });
		if (existing == local.revisions.end())
		{
			for (const auto& n : inRev.notes)
			{
				if (n) ++result.notesAdded;
			}
			local.revisions.push_back(std::move(inRev));
			++result.revisionsAdded;
			continue;
		}
		for (auto& n : inRev.notes)
		{
			if (!n) continue;
			const bool present = std::any_of(existing->notes.begin(), existing->notes.end(),
											 [&n](const std::unique_ptr<gfcNote>& mine) { return mine && mine->id == n->id; });
			if (present) continue;
			// Direct push rather than addNote(): addNote refuses a locked round, and
			// this is the same round arriving from another copy, not an edit.
			existing->notes.push_back(std::move(n));
			++result.notesAdded;
		}
	}
	incoming.revisions.clear();
	return result;
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------
int noteMergeSelfTest()
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
			std::fprintf(stderr, "NOTE-MERGE FAIL: %s\n", msg);
		}
	};

	auto note = [](const char* id, const char* author)
	{
		auto n = std::make_unique<gfcNoteStroke>();
		n->id = id;
		n->author = author;
		n->pts = { gfcNotePoint{0.1f, 0.1f} };
		return n;
	};
	// Appends a revision and returns it; use it before appending another,
	// since the vector may reallocate.
	auto revision = [](gfcReview& review, const char* id, bool locked) -> gfcRevision&
	{
		review.revisions.emplace_back();
		gfcRevision& rev = review.revisions.back();
		rev.id = id;
		rev.author = "tester";
		rev.locked = locked;
		return rev;
	};
	auto ids = [](const gfcRevision& rev)
	{
		std::vector<std::string> out;
		for (const auto& n : rev.notes) out.push_back(n->id);
		return out;
	};
	auto makeIncoming = [&](gfcReview& in)
	{
		in.mediaPath = "/elsewhere/a.exr";
		in.fingerprint = "fp1:abc";
		{
			gfcRevision& r = revision(in, "r1", false);
			r.notes.push_back(note("n2", "B"));
			r.notes.push_back(note("n3", "B"));
		}
		{
			gfcRevision& r = revision(in, "r3", false);
			r.notes.push_back(note("n4", "B"));
		}
		{
			gfcRevision& r = revision(in, "r2", false);
			r.notes.push_back(note("n6", "B"));
		}
	};

	gfcReview local;
	local.mediaPath = "/m/a.exr";
	{
		gfcRevision& r = revision(local, "r1", false);
		r.notes.push_back(note("n1", "A"));
		r.notes.push_back(note("n2", "A"));
	}
	{
		gfcRevision& r = revision(local, "r2", true);
		r.notes.push_back(note("n5", "A"));
	}

	gfcReview incoming;
	makeIncoming(incoming);
	const gfcNoteMerge::Result first = gfcNoteMerge::mergeInto(local, std::move(incoming));
	check(first.revisionsAdded == 1 && first.notesAdded == 3, "one revision and three notes added");
	const bool shaped = local.revisions.size() == 3;
	check(shaped && local.revisions[0].id == "r1" && local.revisions[1].id == "r2" && local.revisions[2].id == "r3",
		  "local revisions first, the new one appended");
	if (shaped)
	{
		check(ids(local.revisions[0]) == std::vector<std::string>{"n1", "n2", "n3"},
			  "a shared revision gains only the missing note");
		check(local.revisions[0].notes[1]->author == "A", "an existing note is kept as it is");
		check(ids(local.revisions[1]) == std::vector<std::string>{"n5", "n6"} && local.revisions[1].locked,
			  "a locked local revision keeps its lock and still gains the note");
		check(ids(local.revisions[2]) == std::vector<std::string>{"n4"}, "a new revision keeps its notes");
	}
	check(local.mediaPath == "/m/a.exr" && local.fingerprint == "fp1:abc",
		  "local identity kept, empty fingerprint adopted");

	gfcReview again;
	makeIncoming(again);
	const gfcNoteMerge::Result second = gfcNoteMerge::mergeInto(local, std::move(again));
	check(second.revisionsAdded == 0 && second.notesAdded == 0 && local.revisions.size() == 3 &&
		  local.revisions[0].notes.size() == 3 && local.revisions[1].notes.size() == 2,
		  "merging the same package again changes nothing");

	gfcReview empty;
	const gfcNoteMerge::Result third = gfcNoteMerge::mergeInto(local, std::move(empty));
	check(third.revisionsAdded == 0 && third.notesAdded == 0 && local.fingerprint == "fp1:abc",
		  "merging an empty review changes nothing");

	std::printf("NOTE-MERGE: pass=%d fail=%d\n", pass, fail);
	return fail;
}

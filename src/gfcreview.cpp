#include "gfcreview.h"

#include <ctime>

gfcReview::gfcReview()
{
}


gfcReview::~gfcReview()
{
}

gfcRevision* gfcReview::openRevision()
{
	for (auto it = revisions.rbegin(); it != revisions.rend(); ++it)
	{
		if (!it->locked)
		{
			return &(*it);
		}
	}
	return nullptr;
}

gfcRevision& gfcReview::beginRevision(const std::string& author)
{
	revisions.emplace_back();
	gfcRevision& rev = revisions.back();
	rev.author = author;
	rev.created = time(nullptr);
	rev.modified = rev.created;
	return rev;
}

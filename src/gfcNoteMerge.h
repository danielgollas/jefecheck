#ifndef GFCNOTEMERGE_H
#define GFCNOTEMERGE_H

class gfcReview;

/**
	@brief Union of two copies of one media's review, by id: what opening a
	review package does when the media already has notes. Nothing local is
	removed or changed; the same idempotent-by-id rule as live note sync.
	See docs/superpowers/specs/2026-09-14-review-package-design.md.
*/
namespace gfcNoteMerge
{
	struct Result
	{
		int revisionsAdded = 0;
		int notesAdded = 0;
	};

	/**
	 * Moves into `local` every revision of `incoming` whose id `local` lacks
	 * (appended, in incoming order), and every note of a shared revision whose id
	 * that revision lacks (appended, even into a locked revision -- both copies
	 * are the same round). An existing revision or note with the same id is kept
	 * as it is. `local` adopts `incoming`'s fingerprint only if it has none.
	 * `incoming` is consumed.
	 */
	Result mergeInto(gfcReview& local, gfcReview&& incoming);
}

/** Prints NOTE-MERGE: pass=N fail=N; returns the fail count. */
int noteMergeSelfTest();

#endif

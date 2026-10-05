#ifndef GFCREVIEWSUMMARY_H
#define GFCREVIEWSUMMARY_H

#include <ctime>
#include <string>
#include <vector>

class gfcReview;

/**
	@brief A review session's latest state, flattened for reading: media ->
	round -> frame -> notes. Built from gfcReview objects; written as text or
	CSV here and as PDF by ReviewSummaryPdf_qt. Pure data -- no Qt, no GL.
	See docs/superpowers/specs/2026-09-14-review-summary-export-design.md.
*/
namespace gfcReviewSummary
{
	/** Frame key for notes that show on every frame (gfcNote::always). */
	constexpr int kAllFrames = -1;

	struct Note
	{
		std::string id;
		std::string type;     // "stroke", "arrow", "box" or "text"
		std::string author;
		int from = 0;
		int to = 0;
		bool always = false;
		float r = 1.0f, g = 0.2f, b = 0.2f;
		int size = 3;
		std::string text;     // text notes only
	};

	struct Frame
	{
		int frame = kAllFrames;
		std::vector<Note> notes;      // stored order
		std::string thumbnailPath;    // PDF only; empty when unavailable
	};

	struct Round
	{
		std::string id;
		std::string author;
		time_t created = 0;
		time_t modified = 0;
		bool locked = false;
		std::vector<Frame> frames;    // kAllFrames first, then ascending frame
	};

	struct Media
	{
		std::string mediaPath;        // normalised pattern, as gfcReview::mediaPath
		std::string displayName;      // file-name part of mediaPath
		bool notesReadable = true;    // false when a sidecar exists but did not parse
		std::vector<Round> rounds;
	};

	struct Doc
	{
		std::string title;
		time_t exportedAt = 0;
		std::string appVersion;
		std::vector<Media> media;
	};

	/** One media's entry: rounds in stored order; within a round, one frame
	    entry per `always ? kAllFrames : from`. */
	Media fromReview(const gfcReview& review);

	int roundCount(const Doc& doc);
	int noteCount(const Doc& doc);

	/** ISO-8601 UTC, e.g. 2026-09-14T05:44:23Z. */
	std::string isoUtc(time_t t);
	/** "#rrggbb" from 0..1 components, clamped. */
	std::string colorHex(float r, float g, float b);
	/** "All frames" or "Frame <n>". */
	std::string frameLabel(const Frame& frame);

	std::string toText(const Doc& doc);
	/** UTF-8, RFC 4180 quoting, CRLF records, header row, one row per note. */
	std::string toCsv(const Doc& doc);

	/** Writes `contents` to `<path>.partial`, then renames it over `path`. */
	bool writeFileAtomically(const std::string& path, const std::string& contents, std::string* err);
}

/** Model, grouping and writer self-test; prints NOTE-SUMMARY: pass=N fail=N and
    returns the number of failed checks. Run by --notes-test. */
int reviewSummarySelfTest();

#endif

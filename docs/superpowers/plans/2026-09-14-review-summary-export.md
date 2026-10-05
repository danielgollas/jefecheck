# Review Summary Export Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** File → Export Review Summary… writes a PDF (thumbnails rendered through the plate pipeline with that round's notes burned in), a TXT or a CSV describing every round of notes on every media in the session.

**Architecture:** A pure C++ document model (`gfcReviewSummary`) flattens `gfcReview` objects into media → round → frame → notes and writes TXT/CSV. A QtGui-only writer (`ReviewSummaryPdf_qt`) lays that model out on A4 pages. The bridge enumerates the session's media and points a plate at one round's notes; `MainWindow_Qt` orchestrates thumbnails (save session → render per frame entry → restore session) and exposes the menu action and headless test.

**Tech Stack:** C++20, Qt 6 (Widgets/Gui: `QPdfWriter`, `QPainter`, `QImage`), vendored note model (`gfcReview`/`gfcRevision`/`gfcNote*`), existing render path (`jefe::qt::triggerSyncRender`).

**Spec:** `docs/superpowers/specs/2026-09-14-review-summary-export-design.md`

## Global Constraints

- Work in `/Users/dgollas/projects/jefecheck2` on branch `feature/review-export`. Run the app from the repo root (it finds `FX/` and `fonts/` there).
- Only `src/qt/SequenceLoadBridge_qt.cpp` may include the rendering-chain managers (`gfcplatemanager.h`, `gfctrackmanager.h`, …). Other `src/qt/*` files call `jefe::qt::*` functions declared in `src/qt/SequenceLoadBridge_qt.h`. Qt files include core headers as `"../gfcX.h"`.
- No new dependencies and no new Qt modules (`QPdfWriter` is in QtGui, already linked).
- There is no unit-test framework. Pure tests are functions that print `NAME: pass=N fail=N`, print `NAME FAIL: <message>` to stderr per failed check, return the fail count, and are called from the `--notes-test` battery in `src/main_qt.cpp` (exit 0 when every self-test returns 0, else 2). GL tests are flags handled after `window.show()` with `QTimer::singleShot(5000, …)`, `fflush(stdout)`, then `std::_Exit(code)` with 0 = pass, 2 = fail.
- New `.cpp` files are picked up by `file(GLOB)` only after reconfiguring: `cmake -S . -B build_qt > /dev/null` before `cmake --build build_qt -j8`.
- Binary: `./build_qt/jefecheck.app/Contents/MacOS/jefecheck`.
- GL end-to-end runs need a settings directory that does not open the Load Sequence Manager at startup. Qt's INI format stores the `General` group as `[%General]`:
  `T=$(mktemp -d) && mkdir -p "$T/JefeCheck" && printf '[%%General]\nopenLoadWindowAtStartup=0\n\n[Session]\ncleanExit=true\n' > "$T/JefeCheck/JefeCheck.ini"` then pass `--config-dir "$T"`.
- Test image: `/Users/dgollas/projects/openexr-images/ScanLines/Blobbies.exr` (1000×1000 EXR, one frame).
- Match the surrounding file's style: core `src/gfc*.cpp` note files use tabs and Allman braces; `src/qt/*` uses 4 spaces and K&R braces. Doc comments are `/** … */`.
- Non-ASCII output characters are written as escapes: em dash `\xE2\x80\x94`, en dash `\xE2\x80\x93`, middle dot `\xC2\xB7`. A hex escape swallows following hex digits, so split the literal (`"\xE2\x80\x93" "18"`).
- Times: ISO-8601 UTC, `%Y-%m-%dT%H:%M:%SZ`.
- Output files are written to `<path>.partial` and renamed on success; nothing partial is left on failure.
- Bash commands must be single-line (no embedded newlines; use `&&`/`;`).
- Commits: stage the task's files explicitly (the repo has many untracked files — never `git add -A` or `git add .`). Write the message to a file and `git commit -F <file>`. Subject `JEF-39: <plain-English summary>`, a short body saying why, and these exact trailer lines last:
  `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_0139ixvqjdzvAuwuFUVcBz37`
- Do not push.

## File Structure

| File | Status | Responsibility |
|------|--------|----------------|
| `src/gfcReviewSummary.h` | create | Summary document model, grouping, TXT/CSV writers, atomic file write, self-test declaration. |
| `src/gfcReviewSummary.cpp` | create | Implementation + `reviewSummarySelfTest()`. |
| `src/qt/ReviewSummaryPdf_qt.h` | create | `jefe::qt::writeReviewSummaryPdf`, `reviewSummaryPdfSelfTest`. |
| `src/qt/ReviewSummaryPdf_qt.cpp` | create | Two-pass A4 layout with QPdfWriter; self-test. |
| `src/qt/SequenceLoadBridge_qt.h` | modify | `SessionMedia`, `getSessionMediaSet`, `buildReviewSummary`, `plateShowingTrack`, `setPlateNotesToRound`, `prepareTrackForRender`. |
| `src/qt/SequenceLoadBridge_qt.cpp` | modify | Implementations; thumbnail note clones released by `syncPlateNotesImpl`. |
| `src/qt/MainWindow_qt.h` | modify | `ReviewSummaryStats`, `exportReviewSummary`, `runHeadlessSummaryTest`, `renderSummaryThumbnails`. |
| `src/qt/MainWindow_qt.cpp` | modify | Export orchestration, File menu action, headless test. |
| `src/main_qt.cpp` | modify | Self-tests in `--notes-test`; `--summary-test <image>`; `--export-summary <out>`. |

---

### Task 1: Summary document model, text and CSV writers

**Files:**
- Create: `src/gfcReviewSummary.h`, `src/gfcReviewSummary.cpp`
- Modify: `src/main_qt.cpp` (the `--notes-test` block, currently around lines 300–316)

**Interfaces:**
- Consumes: `gfcReview` (`src/gfcreview.h`: `mediaPath`, `revisions`, `beginRevision(author)`), `gfcRevision` (`src/gfcrevision.h`: `id`, `author`, `created`, `modified`, `locked`, `notes`, `addNote`), `gfcNote` (`src/gfcnote.h`: `noteType()`, `id`, `author`, `from`, `to`, `always`, `colorR/G/B`, `size`), `gfcNoteText::text`, enum `gfcNoteType { GFCNOTE_STROKE, GFCNOTE_ARROW, GFCNOTE_BOX, GFCNOTE_TEXT }`.
- Produces (namespace `gfcReviewSummary`, used by Tasks 2–4):
  - `constexpr int kAllFrames = -1;`
  - `struct Note { std::string id, type, author; int from, to; bool always; float r, g, b; int size; std::string text; };`
  - `struct Frame { int frame; std::vector<Note> notes; std::string thumbnailPath; };`
  - `struct Round { std::string id, author; time_t created, modified; bool locked; std::vector<Frame> frames; };`
  - `struct Media { std::string mediaPath, displayName; bool notesReadable; std::vector<Round> rounds; };`
  - `struct Doc { std::string title; time_t exportedAt; std::string appVersion; std::vector<Media> media; };`
  - `Media fromReview(const gfcReview&)`, `int roundCount(const Doc&)`, `int noteCount(const Doc&)`, `std::string isoUtc(time_t)`, `std::string colorHex(float, float, float)`, `std::string frameLabel(const Frame&)`, `std::string toText(const Doc&)`, `std::string toCsv(const Doc&)`, `bool writeFileAtomically(const std::string& path, const std::string& contents, std::string* err)`
  - global `int reviewSummarySelfTest();`

- [ ] **Step 1: Write the header**

Create `src/gfcReviewSummary.h`:

```cpp
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
```

- [ ] **Step 2: Write the self-test with stub implementations**

Create `src/gfcReviewSummary.cpp` with stubs that compile but do nothing, plus the complete self-test:

```cpp
#include "gfcReviewSummary.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <system_error>

#include "gfcreview.h"
#include "gfcrevision.h"
#include "gfcnote.h"
#include "gfcnotestroke.h"
#include "gfcnotearrow.h"
#include "gfcnotebox.h"
#include "gfcnotetext.h"

gfcReviewSummary::Media gfcReviewSummary::fromReview(const gfcReview&) { return {}; }
int gfcReviewSummary::roundCount(const Doc&) { return 0; }
int gfcReviewSummary::noteCount(const Doc&) { return 0; }
std::string gfcReviewSummary::isoUtc(time_t) { return {}; }
std::string gfcReviewSummary::colorHex(float, float, float) { return {}; }
std::string gfcReviewSummary::frameLabel(const Frame&) { return {}; }
std::string gfcReviewSummary::toText(const Doc&) { return {}; }
std::string gfcReviewSummary::toCsv(const Doc&) { return {}; }
bool gfcReviewSummary::writeFileAtomically(const std::string&, const std::string&, std::string*) { return false; }

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------
int reviewSummarySelfTest()
{
	using namespace gfcReviewSummary;
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
			std::fprintf(stderr, "NOTE-SUMMARY FAIL: %s\n", msg);
		}
	};

	// One locked round (three notes, one on every frame) and one open, empty round.
	gfcReview review;
	review.mediaPath = "/shots/Blobbies.exr";
	{
		gfcRevision& r1 = review.beginRevision("Supervisor");
		r1.id = "r1";

		auto stroke = std::make_unique<gfcNoteStroke>();
		stroke->id = "n1";
		stroke->author = "Supervisor";
		stroke->from = 12;
		stroke->to = 18;
		stroke->colorR = 1.0f; stroke->colorG = 0.25f; stroke->colorB = 0.2f;
		stroke->size = 4;
		stroke->pts = { gfcNotePoint{0.1f, 0.1f}, gfcNotePoint{0.2f, 0.2f} };
		r1.addNote(std::move(stroke));

		auto text = std::make_unique<gfcNoteText>();
		text->id = "n2";
		text->author = "Supervisor";
		text->from = 12;
		text->to = 12;
		text->colorR = 0.35f; text->colorG = 0.85f; text->colorB = 1.0f;
		text->size = 3;
		text->text = "too warm, \"really\"\nsee grade";
		r1.addNote(std::move(text));

		auto box = std::make_unique<gfcNoteBox>();
		box->id = "n3";
		box->author = "Artist";
		box->always = true;
		box->from = 1;
		box->to = 1;
		box->colorR = 0.35f; box->colorG = 0.85f; box->colorB = 1.0f;
		box->size = 3;
		r1.addNote(std::move(box));

		// addNote/beginRevision stamp the clock; pin the times after them.
		r1.created = 1789364663;
		r1.modified = 1789364663;
		r1.locked = true;
	}
	{
		gfcRevision& r2 = review.beginRevision("Artist");
		r2.id = "r2";
		r2.created = 1789400000;
		r2.modified = 1789400000;
	}

	const Media m = fromReview(review);
	check(m.displayName == "Blobbies.exr", "display name is the file name");
	check(m.rounds.size() == 2, "one entry per round");
	const bool shaped = m.rounds.size() == 2 && m.rounds[0].frames.size() == 2;
	check(shaped, "round 1 groups into two frame entries");
	if (shaped)
	{
		const Frame& all = m.rounds[0].frames[0];
		const Frame& f12 = m.rounds[0].frames[1];
		check(all.frame == kAllFrames && all.notes.size() == 1 && all.notes[0].id == "n3",
			  "the all-frames entry comes first");
		check(f12.frame == 12 && f12.notes.size() == 2 && f12.notes[0].id == "n1" && f12.notes[1].id == "n2",
			  "frame 12 keeps the stored note order");
		check(f12.notes.size() == 2 && f12.notes[1].type == "text" && f12.notes[1].text == "too warm, \"really\"\nsee grade",
			  "text notes keep type and text");
		check(m.rounds[1].frames.empty(), "an empty round is kept");
		check(m.rounds[0].locked && !m.rounds[1].locked, "lock state carries over");
	}

	check(isoUtc(0) == "1970-01-01T00:00:00Z", "epoch formats as ISO UTC");
	check(isoUtc(1789364663) == "2026-09-14T05:44:23Z", "a 2026 time formats as ISO UTC");
	check(colorHex(1.0f, 0.25f, 0.2f) == "#ff4033", "colour hex rounds to bytes");
	check(colorHex(2.0f, -1.0f, 0.5f) == "#ff0080", "colour hex clamps");
	check(frameLabel(Frame{}) == "All frames", "all-frames label");

	Doc doc;
	doc.title = "Blobbies review";
	doc.exportedAt = 1789400000;
	doc.appVersion = "1.7.0";
	doc.media.push_back(m);
	check(roundCount(doc) == 2 && noteCount(doc) == 3, "totals count rounds and notes");

	const std::string expectedText =
		"Review summary \xE2\x80\x94 Blobbies review\n"
		"Exported 2026-09-14T15:33:20Z by JefeCheck 1.7.0\n"
		"1 media \xC2\xB7 2 rounds \xC2\xB7 3 notes\n"
		"\n"
		"== Blobbies.exr ==\n"
		"Round 1 \xE2\x80\x94 Supervisor \xE2\x80\x94 created 2026-09-14T05:44:23Z \xE2\x80\x94 locked\n"
		"  All frames\n"
		"    - box     Artist  frames all  #59d9ff\n"
		"  Frame 12\n"
		"    - stroke  Supervisor  frames 12\xE2\x80\x93" "18  #ff4033\n"
		"    - text    Supervisor  frames 12\xE2\x80\x93" "12  #59d9ff  \"too warm, \"really\" see grade\"\n"
		"Round 2 \xE2\x80\x94 Artist \xE2\x80\x94 created 2026-09-14T15:33:20Z \xE2\x80\x94 open\n"
		"  No notes\n";
	const std::string gotText = toText(doc);
	check(gotText == expectedText, "text output matches the golden text");
	if (gotText != expectedText)
	{
		std::fprintf(stderr, "---- got text ----\n%s---- end ----\n", gotText.c_str());
	}

	const std::string header =
		"media,round_id,round_author,round_created,round_modified,round_locked,note_id,type,author,from,to,all_frames,color,size,text\r\n";
	const std::string expectedCsv = header +
		"/shots/Blobbies.exr,r1,Supervisor,2026-09-14T05:44:23Z,2026-09-14T05:44:23Z,true,n3,box,Artist,1,1,true,#59d9ff,3,\r\n"
		"/shots/Blobbies.exr,r1,Supervisor,2026-09-14T05:44:23Z,2026-09-14T05:44:23Z,true,n1,stroke,Supervisor,12,18,false,#ff4033,4,\r\n"
		"/shots/Blobbies.exr,r1,Supervisor,2026-09-14T05:44:23Z,2026-09-14T05:44:23Z,true,n2,text,Supervisor,12,12,false,#59d9ff,3,\"too warm, \"\"really\"\"\nsee grade\"\r\n";
	const std::string gotCsv = toCsv(doc);
	check(gotCsv == expectedCsv, "CSV output matches the golden CSV");
	if (gotCsv != expectedCsv)
	{
		std::fprintf(stderr, "---- got csv ----\n%s---- end ----\n", gotCsv.c_str());
	}

	// Media with no rounds, and media whose sidecar did not parse.
	Doc sparse = doc;
	sparse.media.clear();
	Media none;
	none.mediaPath = "/shots/a.exr";
	none.displayName = "a.exr";
	sparse.media.push_back(none);
	Media unreadable = none;
	unreadable.mediaPath = "/shots/b.exr";
	unreadable.displayName = "b.exr";
	unreadable.notesReadable = false;
	sparse.media.push_back(unreadable);
	const std::string sparseText = toText(sparse);
	check(sparseText.find("== a.exr ==\n  No notes\n") != std::string::npos, "media without rounds says No notes");
	check(sparseText.find("== b.exr ==\n  Notes unreadable\n") != std::string::npos, "unreadable notes are reported");
	check(sparseText.find("2 media \xC2\xB7 0 rounds \xC2\xB7 0 notes\n") != std::string::npos, "totals pluralise zero");
	check(toCsv(sparse) == header, "media without notes adds no CSV rows");

	// Atomic write: the content lands, no partial file remains.
	const std::string path = (std::filesystem::temp_directory_path() /
		("jefe_summary_test_" + std::to_string(static_cast<long long>(time(nullptr))) + ".txt")).string();
	std::string err;
	check(writeFileAtomically(path, "hello\n", &err), "atomic write succeeds");
	std::ifstream in(path, std::ios::binary);
	const std::string back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	in.close();
	check(back == "hello\n", "atomic write stores the contents");
	std::error_code ec;
	check(!std::filesystem::exists(path + ".partial", ec), "atomic write leaves no partial file");
	std::filesystem::remove(path, ec);
	check(!writeFileAtomically("/nonexistent_dir_jefe/x.txt", "x", &err) && !err.empty(),
		  "an unwritable path reports an error");

	std::printf("NOTE-SUMMARY: pass=%d fail=%d\n", pass, fail);
	return fail;
}
```

- [ ] **Step 3: Wire the self-test into `--notes-test`**

In `src/main_qt.cpp`, add `#include "gfcReviewSummary.h"` beside `#include "gfcNoteStamp.h"`, and change the `--notes-test` block to:

```cpp
    if (hasNotesTest(argc, argv)) {
        const int modelFail   = noteModelSelfTest();
        const int storeFail   = noteStoreSelfTest();
        const int overlayFail = noteOverlaySelfTest();
        const int stampFail   = noteStampSelfTest();
        const int summaryFail = reviewSummarySelfTest();
        // The self-tests print via std::printf but do not flush; std::_Exit
        // skips stdio's normal flush-on-exit, so an unflushed buffer (e.g.
        // stdout not a tty) would silently drop all three lines.
        std::fflush(stdout);
        std::_Exit((modelFail == 0 && storeFail == 0 && overlayFail == 0 &&
                    stampFail == 0 && summaryFail == 0) ? 0 : 2);
    }
```

- [ ] **Step 4: Build and run — expect failures**

Run: `cmake -S . -B build_qt > /dev/null && cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-|FAIL' | head -30`
Expected: builds; `NOTE-SUMMARY: pass=<small> fail=<many>` with `NOTE-SUMMARY FAIL:` lines; the four existing lines still `fail=0`.

- [ ] **Step 5: Replace the stubs with the implementation**

Replace the nine stub lines in `src/gfcReviewSummary.cpp` with:

```cpp
namespace
{
	const char* const kEmDash    = "\xE2\x80\x94";
	const char* const kEnDash    = "\xE2\x80\x93";
	const char* const kMiddleDot = "\xC2\xB7";

	const char* typeName(gfcNoteType t)
	{
		switch (t)
		{
			case GFCNOTE_STROKE: return "stroke";
			case GFCNOTE_ARROW:  return "arrow";
			case GFCNOTE_BOX:    return "box";
			case GFCNOTE_TEXT:   return "text";
		}
		return "stroke";
	}

	gfcReviewSummary::Note toNote(const gfcNote& n)
	{
		gfcReviewSummary::Note out;
		out.id = n.id;
		out.type = typeName(n.noteType());
		out.author = n.author;
		out.from = n.from;
		out.to = n.to;
		out.always = n.always;
		out.r = n.colorR;
		out.g = n.colorG;
		out.b = n.colorB;
		out.size = n.size;
		if (const auto* t = dynamic_cast<const gfcNoteText*>(&n))
		{
			out.text = t->text;
		}
		return out;
	}

	std::string counted(int n, const char* one, const char* many)
	{
		return std::to_string(n) + " " + (n == 1 ? one : many);
	}

	// Text output keeps one note per line: line breaks inside a note become spaces.
	std::string oneLine(const std::string& s)
	{
		std::string out = s;
		for (char& c : out)
		{
			if (c == '\n' || c == '\r') c = ' ';
		}
		return out;
	}

	std::string csvField(const std::string& s)
	{
		if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
		std::string out = "\"";
		for (char c : s)
		{
			if (c == '"') out += "\"\"";
			else out += c;
		}
		out += "\"";
		return out;
	}
}

gfcReviewSummary::Media gfcReviewSummary::fromReview(const gfcReview& review)
{
	Media m;
	m.mediaPath = review.mediaPath;
	const size_t slash = review.mediaPath.find_last_of("/\\");
	m.displayName = (slash == std::string::npos) ? review.mediaPath : review.mediaPath.substr(slash + 1);

	for (const gfcRevision& rev : review.revisions)
	{
		Round entry;
		entry.id = rev.id;
		entry.author = rev.author;
		entry.created = rev.created;
		entry.modified = rev.modified;
		entry.locked = rev.locked;
		for (const auto& np : rev.notes)
		{
			if (!np) continue;
			Note n = toNote(*np);
			const int key = n.always ? kAllFrames : n.from;
			auto it = std::find_if(entry.frames.begin(), entry.frames.end(),
								   [key](const Frame& f) { return f.frame == key; });
			if (it == entry.frames.end())
			{
				Frame f;
				f.frame = key;
				entry.frames.push_back(f);
				it = entry.frames.end() - 1;
			}
			it->notes.push_back(n);
		}
		// kAllFrames is -1, so it sorts ahead of every real frame.
		std::stable_sort(entry.frames.begin(), entry.frames.end(),
						 [](const Frame& a, const Frame& b) { return a.frame < b.frame; });
		m.rounds.push_back(std::move(entry));
	}
	return m;
}

int gfcReviewSummary::roundCount(const Doc& doc)
{
	int n = 0;
	for (const Media& m : doc.media) n += static_cast<int>(m.rounds.size());
	return n;
}

int gfcReviewSummary::noteCount(const Doc& doc)
{
	int n = 0;
	for (const Media& m : doc.media)
		for (const Round& r : m.rounds)
			for (const Frame& f : r.frames)
				n += static_cast<int>(f.notes.size());
	return n;
}

std::string gfcReviewSummary::isoUtc(time_t t)
{
	std::tm tm {};
#ifdef _WIN32
	gmtime_s(&tm, &t);
#else
	gmtime_r(&t, &tm);
#endif
	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
	return buf;
}

std::string gfcReviewSummary::colorHex(float r, float g, float b)
{
	auto byte = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
	char buf[16];
	std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", byte(r), byte(g), byte(b));
	return buf;
}

std::string gfcReviewSummary::frameLabel(const Frame& frame)
{
	return frame.frame == kAllFrames ? std::string("All frames") : "Frame " + std::to_string(frame.frame);
}

std::string gfcReviewSummary::toText(const Doc& doc)
{
	std::ostringstream out;
	out << "Review summary " << kEmDash << " " << doc.title << "\n";
	out << "Exported " << isoUtc(doc.exportedAt) << " by JefeCheck " << doc.appVersion << "\n";
	out << counted(static_cast<int>(doc.media.size()), "media", "media") << " " << kMiddleDot << " "
		<< counted(roundCount(doc), "round", "rounds") << " " << kMiddleDot << " "
		<< counted(noteCount(doc), "note", "notes") << "\n";

	for (const Media& m : doc.media)
	{
		out << "\n== " << m.displayName << " ==\n";
		if (!m.notesReadable)
		{
			out << "  Notes unreadable\n";
			continue;
		}
		if (m.rounds.empty())
		{
			out << "  No notes\n";
			continue;
		}
		for (size_t ri = 0; ri < m.rounds.size(); ++ri)
		{
			const Round& r = m.rounds[ri];
			out << "Round " << (ri + 1) << " " << kEmDash << " " << r.author << " " << kEmDash
				<< " created " << isoUtc(r.created) << " " << kEmDash << " " << (r.locked ? "locked" : "open") << "\n";
			if (r.frames.empty())
			{
				out << "  No notes\n";
				continue;
			}
			for (const Frame& f : r.frames)
			{
				out << "  " << frameLabel(f) << "\n";
				for (const Note& n : f.notes)
				{
					std::string type = n.type;
					if (type.size() < 6) type.resize(6, ' ');
					out << "    - " << type << "  " << n.author << "  frames ";
					if (n.always) out << "all";
					else out << n.from << kEnDash << n.to;
					out << "  " << colorHex(n.r, n.g, n.b);
					if (n.type == "text") out << "  \"" << oneLine(n.text) << "\"";
					out << "\n";
				}
			}
		}
	}
	return out.str();
}

std::string gfcReviewSummary::toCsv(const Doc& doc)
{
	std::string out =
		"media,round_id,round_author,round_created,round_modified,round_locked,note_id,type,author,from,to,all_frames,color,size,text\r\n";
	for (const Media& m : doc.media)
		for (const Round& r : m.rounds)
			for (const Frame& f : r.frames)
				for (const Note& n : f.notes)
				{
					const std::string fields[] = {
						m.mediaPath, r.id, r.author, isoUtc(r.created), isoUtc(r.modified),
						r.locked ? "true" : "false", n.id, n.type, n.author,
						std::to_string(n.from), std::to_string(n.to), n.always ? "true" : "false",
						colorHex(n.r, n.g, n.b), std::to_string(n.size), n.text };
					for (size_t i = 0; i < std::size(fields); ++i)
					{
						if (i) out += ",";
						out += csvField(fields[i]);
					}
					out += "\r\n";
				}
	return out;
}

bool gfcReviewSummary::writeFileAtomically(const std::string& path, const std::string& contents, std::string* err)
{
	const std::string partial = path + ".partial";
	std::error_code ec;
	{
		std::ofstream f(partial, std::ios::binary | std::ios::trunc);
		if (!f)
		{
			if (err) *err = "cannot write " + partial;
			return false;
		}
		f.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		if (!f)
		{
			if (err) *err = "write failed: " + partial;
			f.close();
			std::filesystem::remove(partial, ec);
			return false;
		}
	}
	std::filesystem::rename(partial, path, ec);
	if (ec)
	{
		if (err) *err = "cannot rename " + partial + ": " + ec.message();
		std::filesystem::remove(partial, ec);
		return false;
	}
	return true;
}
```

- [ ] **Step 6: Build and run — expect all pass**

Run: `cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-|FAIL'; echo "exit=$?"`
Expected: `NOTE-MODEL`, `NOTE-STORE`, `NOTE-OVERLAY`, `NOTE-STAMP` unchanged with `fail=0`, and `NOTE-SUMMARY: pass=24 fail=0`; no `FAIL` lines. (Use `./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test > /dev/null 2>&1; echo $?` to confirm exit status 0.)

- [ ] **Step 7: Commit**

`git add src/gfcReviewSummary.h src/gfcReviewSummary.cpp src/main_qt.cpp` and commit with subject `JEF-39: flatten a review into media, rounds and frames, and write it as text or CSV`.

---

### Task 2: PDF writer

**Files:**
- Create: `src/qt/ReviewSummaryPdf_qt.h`, `src/qt/ReviewSummaryPdf_qt.cpp`
- Modify: `src/main_qt.cpp` (the `--notes-test` block and includes)

**Interfaces:**
- Consumes: everything in `gfcReviewSummary` from Task 1 (`Doc`, `Media`, `Round`, `Frame`, `Note`, `kAllFrames`, `isoUtc`, `frameLabel`, `roundCount`, `noteCount`).
- Produces:
  - `namespace jefe::qt { bool writeReviewSummaryPdf(const gfcReviewSummary::Doc& doc, const QString& path, int* pagesOut, QString* err); int reviewSummaryPdfSelfTest(); }`

- [ ] **Step 1: Write the header**

Create `src/qt/ReviewSummaryPdf_qt.h`:

```cpp
// Review summary PDF (docs/superpowers/specs/2026-09-14-review-summary-export-design.md).
// QtGui only — no glad, no managers (developer_notes.md §1).
#ifndef JEFECHECK_QT_REVIEW_SUMMARY_PDF_H
#define JEFECHECK_QT_REVIEW_SUMMARY_PDF_H

#include <QString>

#include "../gfcReviewSummary.h"

namespace jefe::qt {

/**
 * Lays @a doc out as an A4 portrait PDF at @a path: a header block, one section
 * per media (each after the first on a new page), a header line per round, and
 * one entry per frame — the thumbnail from Frame::thumbnailPath on the left (a
 * "thumbnail unavailable" box when empty or unreadable), the notes on the right.
 * Writes <path>.partial and renames it. Returns false with @a err filled on
 * failure; @a pagesOut receives the page count.
 */
bool writeReviewSummaryPdf(const gfcReviewSummary::Doc& doc, const QString& path,
                           int* pagesOut, QString* err);

/** Prints NOTE-SUMMARY-PDF: pass=N fail=N; returns the failed-check count. */
int reviewSummaryPdfSelfTest();

}  // namespace jefe::qt

#endif
```

- [ ] **Step 2: Write the self-test with a stub writer**

Create `src/qt/ReviewSummaryPdf_qt.cpp`:

```cpp
#include "ReviewSummaryPdf_qt.h"

#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPen>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace jefe::qt {

bool writeReviewSummaryPdf(const gfcReviewSummary::Doc&, const QString&, int*, QString*) {
    return false;
}

int reviewSummaryPdfSelfTest() {
    int pass = 0;
    int fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) {
            ++pass;
        } else {
            ++fail;
            std::fprintf(stderr, "NOTE-SUMMARY-PDF FAIL: %s\n", msg);
        }
    };

    const QString dir = QDir::tempPath() + "/jefe_summary_pdf_test_" +
                        QString::number(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(dir);
    QImage thumb(640, 360, QImage::Format_RGB32);
    thumb.fill(QColor(40, 120, 60));
    const QString thumbPath = dir + "/thumb.png";
    check(thumb.save(thumbPath), "fixture thumbnail saved");

    gfcReviewSummary::Note note;
    note.id = "n1";
    note.type = "text";
    note.author = "Supervisor";
    note.from = 12;
    note.to = 12;
    note.text = "too warm here";

    gfcReviewSummary::Frame withThumb;
    withThumb.frame = 12;
    withThumb.notes.push_back(note);
    withThumb.thumbnailPath = thumbPath.toStdString();
    gfcReviewSummary::Frame noThumb = withThumb;
    noThumb.thumbnailPath.clear();

    gfcReviewSummary::Round locked;
    locked.id = "r1";
    locked.author = "Supervisor";
    locked.created = 1789364663;
    locked.locked = true;
    locked.frames.push_back(withThumb);
    gfcReviewSummary::Round empty;
    empty.id = "r2";
    empty.author = "Artist";
    empty.created = 1789400000;
    gfcReviewSummary::Round missing = locked;
    missing.frames.clear();
    missing.frames.push_back(noThumb);

    gfcReviewSummary::Media a;
    a.mediaPath = "/shots/a.exr";
    a.displayName = "a.exr";
    a.rounds.push_back(locked);
    gfcReviewSummary::Media b;
    b.mediaPath = "/shots/b.exr";
    b.displayName = "b.exr";
    b.rounds.push_back(missing);
    b.rounds.push_back(empty);

    gfcReviewSummary::Doc doc;
    doc.title = "PDF self-test";
    doc.exportedAt = 1789400000;
    doc.appVersion = "1.7.0";
    doc.media = {a, b};

    const QString out = dir + "/summary.pdf";
    int pages = 0;
    QString err;
    check(writeReviewSummaryPdf(doc, out, &pages, &err), "writer reports success");
    check(pages == 2, "the second media starts page 2");

    QFile f(out);
    const QByteArray bytes = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    check(bytes.startsWith("%PDF-"), "file starts with %PDF-");
    check(bytes.trimmed().endsWith("%%EOF"), "file ends with %%EOF");
    // Page dictionaries are written uncompressed: count "/Type /Page" but not "/Type /Pages".
    int pageDicts = 0;
    for (qsizetype at = bytes.indexOf("/Type /Page"); at >= 0; at = bytes.indexOf("/Type /Page", at + 1)) {
        if (at + 11 >= bytes.size() || bytes.at(at + 11) != 's') ++pageDicts;
    }
    check(pages > 0 && pageDicts == pages, "page dictionaries match the reported page count");
    check(!QFile::exists(out + ".partial"), "no partial file left");

    QString err2;
    int pages2 = 0;
    check(!writeReviewSummaryPdf(doc, "/nonexistent_dir_jefe/x.pdf", &pages2, &err2) && !err2.isEmpty(),
          "an unwritable path reports an error");

    std::printf("NOTE-SUMMARY-PDF: pass=%d fail=%d\n", pass, fail);
    return fail;
}

}  // namespace jefe::qt
```

- [ ] **Step 3: Wire the self-test into `--notes-test`**

In `src/main_qt.cpp`, add `#include "qt/ReviewSummaryPdf_qt.h"` after `#include "qt/SequenceLoadBridge_qt.h"`, and extend the block from Task 1:

```cpp
        const int summaryFail = reviewSummarySelfTest();
        const int pdfFail     = jefe::qt::reviewSummaryPdfSelfTest();
```

and the exit condition to `... && summaryFail == 0 && pdfFail == 0) ? 0 : 2);`.

- [ ] **Step 4: Build and run — expect failures**

Run: `cmake -S . -B build_qt > /dev/null && cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-SUMMARY-PDF'`
Expected: `NOTE-SUMMARY-PDF FAIL:` lines and `NOTE-SUMMARY-PDF: pass=2 fail=6` (only the fixture save and "no partial file" pass).

- [ ] **Step 5: Implement the writer**

Replace the stub `writeReviewSummaryPdf` in `src/qt/ReviewSummaryPdf_qt.cpp` with:

```cpp
namespace {

constexpr int kDpi = 300;

int mm(double v) { return int(v * kDpi / 25.4 + 0.5); }

QString qs(const std::string& s) { return QString::fromStdString(s); }

const QString& emDash() { static const QString s = QString::fromUtf8("\xE2\x80\x94"); return s; }
const QString& enDash() { static const QString s = QString::fromUtf8("\xE2\x80\x93"); return s; }
const QString& middleDot() { static const QString s = QString::fromUtf8("\xC2\xB7"); return s; }

struct Fonts {
    QFont title, heading, round, frame, body, footer;
};

Fonts makeFonts() {
    Fonts f;
    f.title.setPointSizeF(16);   f.title.setBold(true);
    f.heading.setPointSizeF(13); f.heading.setBold(true);
    f.round.setPointSizeF(10.5); f.round.setBold(true);
    f.frame.setPointSizeF(9.5);  f.frame.setBold(true);
    f.body.setPointSizeF(9);
    f.footer.setPointSizeF(8);
    return f;
}

QString counted(int n, const char* one, const char* many) {
    return QString::number(n) + " " + (n == 1 ? one : many);
}

QString noteLine(const gfcReviewSummary::Note& n) {
    QString s = qs(n.type) + "  " + qs(n.author) + "  frames ";
    s += n.always ? QStringLiteral("all") : QString::number(n.from) + enDash() + QString::number(n.to);
    if (n.type == "text") s += "  \"" + qs(n.text) + "\"";
    return s;
}

// Walks the whole document. With painter == nullptr it only measures and
// returns the page count; with a painter it draws, using totalPages in the
// footers. Both passes make identical break decisions because both measure
// with the same fonts against the same device.
int layoutSummary(QPdfWriter& pdf, QPainter* painter, const gfcReviewSummary::Doc& doc, int totalPages) {
    const Fonts fonts = makeFonts();
    const QRect area = pdf.pageLayout().paintRectPixels(kDpi);
    const int W = area.width();
    const int H = area.height();
    const int footerH = mm(8);
    const int bottom = H - footerH;
    int page = 1;
    int y = 0;

    auto measure = [&](const QFont& font, const QString& text, int width) {
        const QFontMetricsF fm(font, &pdf);
        return int(fm.boundingRect(QRectF(0, 0, width, 1e7), Qt::TextWordWrap, text).height() + 0.5);
    };
    auto drawText = [&](const QFont& font, const QString& text, int x, int top, int width, int height) {
        if (!painter) return;
        painter->setFont(font);
        painter->setPen(Qt::black);
        painter->drawText(QRect(x, top, width, height), Qt::TextWordWrap, text);
    };
    auto drawFooter = [&]() {
        if (!painter) return;
        painter->setFont(fonts.footer);
        painter->setPen(Qt::darkGray);
        painter->drawText(QRect(0, H - footerH, W, footerH), Qt::AlignHCenter | Qt::AlignBottom,
                          qs(doc.title) + " " + emDash() + " page " + QString::number(page) +
                          " of " + QString::number(totalPages));
        painter->setPen(Qt::black);
    };
    auto newPage = [&]() {
        drawFooter();
        if (painter) pdf.newPage();
        ++page;
        y = 0;
    };
    // Entries are never split: move to a new page unless already at its top.
    auto ensureRoom = [&](int height) {
        if (y > 0 && y + height > bottom) newPage();
    };
    auto paragraph = [&](const QFont& font, const QString& text, int gapAfter) {
        const int h = measure(font, text, W);
        ensureRoom(h);
        drawText(font, text, 0, y, W, h);
        y += h + gapAfter;
    };

    // Header block.
    paragraph(fonts.title, "Review summary " + emDash() + " " + qs(doc.title), mm(1));
    paragraph(fonts.body, "Exported " + qs(gfcReviewSummary::isoUtc(doc.exportedAt)) +
                          " by JefeCheck " + qs(doc.appVersion), 0);
    paragraph(fonts.body, counted(int(doc.media.size()), "media", "media") + " " + middleDot() + " " +
                          counted(gfcReviewSummary::roundCount(doc), "round", "rounds") + " " + middleDot() + " " +
                          counted(gfcReviewSummary::noteCount(doc), "note", "notes"), mm(6));

    for (size_t mi = 0; mi < doc.media.size(); ++mi) {
        const gfcReviewSummary::Media& m = doc.media[mi];
        if (mi > 0) newPage();
        paragraph(fonts.heading, qs(m.displayName), mm(3));
        if (!m.notesReadable || m.rounds.empty()) {
            paragraph(fonts.body, m.notesReadable ? QStringLiteral("No notes") : QStringLiteral("Notes unreadable"), mm(4));
            continue;
        }
        for (size_t ri = 0; ri < m.rounds.size(); ++ri) {
            const gfcReviewSummary::Round& r = m.rounds[ri];
            const QString header = "Round " + QString::number(ri + 1) + " " + emDash() + " " + qs(r.author) +
                                   " " + emDash() + " created " + qs(gfcReviewSummary::isoUtc(r.created)) +
                                   " " + emDash() + " " + (r.locked ? "locked" : "open");
            const int headerH = measure(fonts.round, header, W);
            ensureRoom(headerH + mm(20));   // keep a round header with the start of its first entry
            drawText(fonts.round, header, 0, y, W, headerH);
            y += headerH + mm(2);
            if (r.frames.empty()) {
                paragraph(fonts.body, QStringLiteral("No notes"), mm(4));
                continue;
            }
            for (const gfcReviewSummary::Frame& f : r.frames) {
                const int thumbW = mm(80);
                QImage img;
                if (!f.thumbnailPath.empty()) img.load(qs(f.thumbnailPath));
                const int thumbH = (img.isNull() || img.width() <= 0)
                                   ? mm(45)
                                   : std::max(1, int(double(thumbW) * img.height() / img.width() + 0.5));
                const int textX = thumbW + mm(5);
                const int textW = W - textX;
                const int noteX = textX + mm(5);
                const int noteW = textW - mm(5);

                const QString label = qs(gfcReviewSummary::frameLabel(f));
                const int labelH = measure(fonts.frame, label, textW);
                std::vector<int> noteHeights;
                int textH = labelH + mm(1);
                for (const gfcReviewSummary::Note& n : f.notes) {
                    const int nh = std::max(measure(fonts.body, noteLine(n), noteW), mm(3.5));
                    noteHeights.push_back(nh);
                    textH += nh + mm(1);
                }
                const int entryH = std::max(thumbH, textH);
                ensureRoom(entryH);

                if (painter) {
                    const QRect thumbRect(0, y, thumbW, thumbH);
                    if (img.isNull()) {
                        painter->setPen(QPen(Qt::gray, mm(0.3)));
                        painter->setBrush(Qt::NoBrush);
                        painter->drawRect(thumbRect);
                        painter->setFont(fonts.body);
                        painter->drawText(thumbRect, Qt::AlignCenter | Qt::TextWordWrap,
                                          QStringLiteral("thumbnail unavailable"));
                        painter->setPen(Qt::black);
                    } else {
                        painter->drawImage(thumbRect, img);
                    }
                    int ty = y;
                    drawText(fonts.frame, label, textX, ty, textW, labelH);
                    ty += labelH + mm(1);
                    for (size_t ni = 0; ni < f.notes.size(); ++ni) {
                        const gfcReviewSummary::Note& n = f.notes[ni];
                        const QColor swatch = QColor::fromRgbF(std::clamp(n.r, 0.0f, 1.0f),
                                                               std::clamp(n.g, 0.0f, 1.0f),
                                                               std::clamp(n.b, 0.0f, 1.0f));
                        painter->fillRect(QRect(textX, ty + mm(0.6), mm(3), mm(3)), swatch);
                        drawText(fonts.body, noteLine(n), noteX, ty, noteW, noteHeights[ni]);
                        ty += noteHeights[ni] + mm(1);
                    }
                }
                y += entryH + mm(5);
            }
        }
    }
    drawFooter();
    return page;
}

}  // namespace

bool writeReviewSummaryPdf(const gfcReviewSummary::Doc& doc, const QString& path,
                           int* pagesOut, QString* err) {
    const QString partial = path + ".partial";
    QFile::remove(partial);
    int pages = 0;
    {
        QPdfWriter pdf(partial);
        pdf.setPageSize(QPageSize(QPageSize::A4));
        pdf.setResolution(kDpi);
        pdf.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
        pdf.setTitle(qs(doc.title));
        pdf.setCreator("JefeCheck " + qs(doc.appVersion));

        pages = layoutSummary(pdf, nullptr, doc, 0);
        QPainter painter;
        if (!painter.begin(&pdf)) {
            if (err) *err = QStringLiteral("Cannot write %1").arg(path);
            QFile::remove(partial);
            return false;
        }
        layoutSummary(pdf, &painter, doc, pages);
        painter.end();
    }
    QFile::remove(path);
    if (!QFile::rename(partial, path)) {
        if (err) *err = QStringLiteral("Cannot rename %1").arg(partial);
        QFile::remove(partial);
        return false;
    }
    if (pagesOut) *pagesOut = pages;
    return true;
}
```

- [ ] **Step 6: Build and run — expect all pass**

Run: `cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --notes-test 2>&1 | grep -E 'NOTE-|FAIL'`
Expected: all six lines `fail=0`, including `NOTE-SUMMARY-PDF: pass=8 fail=0`.
If only "page dictionaries match" fails, print the bytes around the first `/Type` in the file to see the exact token QPdfWriter writes, and fix the counted token — keep the assertion that the count equals `pages`.

- [ ] **Step 7: Look at the PDF**

The self-test leaves its output in a directory named `jefe_summary_pdf_test_<ms>` under `$TMPDIR`. Render page 1 to PNG and read it:
`P=$(ls -dt ${TMPDIR:-/tmp}/jefe_summary_pdf_test_* | head -1) && sips -s format png "$P/summary.pdf" --out "$P/page1.png" > /dev/null && echo "$P/page1.png"`
Open the PNG with the Read tool. Expected: header block, `a.exr` heading, `Round 1 — Supervisor — created 2026-09-14T05:44:23Z — locked`, a green 80 mm thumbnail with `Frame 12` and a swatch + `text  Supervisor  frames 12–12  "too warm here"` beside it, and a footer `PDF self-test — page 1 of 2`.

- [ ] **Step 8: Commit**

`git add src/qt/ReviewSummaryPdf_qt.h src/qt/ReviewSummaryPdf_qt.cpp src/main_qt.cpp` and commit with subject `JEF-39: lay a review summary out as a PDF`.

---

### Task 3: Session media set, text and CSV export end to end

**Files:**
- Modify: `src/qt/SequenceLoadBridge_qt.h` (after the declaration `void syncPlateNotes();`, around line 1048)
- Modify: `src/qt/SequenceLoadBridge_qt.cpp` (globals near `std::vector<gfcNotePoint> g_drawPoints;`, end of `syncPlateNotesImpl()`, and new definitions after the definition of `void syncPlateNotes()`)
- Modify: `src/qt/MainWindow_qt.h`, `src/qt/MainWindow_qt.cpp`
- Modify: `src/main_qt.cpp` (new flags after the `--stamp-notes` block)

**Interfaces:**
- Consumes: Task 1 (`gfcReviewSummary::Doc`, `fromReview`, `toText`, `toCsv`, `writeFileAtomically`, `roundCount`, `noteCount`). Existing bridge internals in `SequenceLoadBridge_qt.cpp`: `reviewForPath(const std::string& normalisedPath) -> gfcReview&` (anonymous namespace), `trackManager.getSequence(int) -> gfcSequence*`, `gfcSequence::filenameGeneric`, `gfcSequence::myGUI->getFilename() -> std::string`, `playlistManager.getPlaylist()` (pointer to the vector of `gfcPlaylistItem`, each with `loadParams[i].fileName`), `plateManager.plateCount()`, `plateManager.getTrackOnPlate(int)`, `plateManager.setPlateNotes(int, const std::vector<const gfcNote*>&)`, `GFC_MAX_SEQUENCES` (4), `JEFE_VERSION`. Existing public API: `MainWindow_Qt::loadFileIntoPlate(int, const QString&)`, `jefe::qt::setActivePlate(int)`.
- Produces (in `namespace jefe::qt`):
  - `struct SessionMedia { std::string mediaPath; std::string anyFramePath; int track = -1; int playlistItem = -1; int playlistTrack = -1; };`
  - `std::vector<SessionMedia> getSessionMediaSet();`
  - `gfcReviewSummary::Doc buildReviewSummary(const std::vector<SessionMedia>& media, const std::string& title);`
  - `int plateShowingTrack(int track);`
  - `bool setPlateNotesToRound(int plateIdx, const std::string& mediaPath, int roundIndex);`
  - `MainWindow_Qt::ReviewSummaryStats { int media, rounds, notes, thumbs, thumbFail; QString firstThumbnail; }`
  - `bool MainWindow_Qt::exportReviewSummary(const QString& outPath, ReviewSummaryStats* stats, QString* message);`
  - `int MainWindow_Qt::runHeadlessSummaryTest(const QString& imagePath);`
  - CLI: `--summary-test <image>`, `--export-summary <out>`

- [ ] **Step 1: Declare the bridge API**

In `src/qt/SequenceLoadBridge_qt.h`, add `#include "../gfcReviewSummary.h"` with the header's other includes, and after `void syncPlateNotes();` add:

```cpp
/**
 * One piece of media in the session, for the review summary and the review
 * package. `mediaPath` is the normalised pattern notes are keyed by;
 * `anyFramePath` is a real file of it. `track` is the first track (0..3)
 * holding it, or -1 when only a playlist item does — then `playlistItem` and
 * `playlistTrack` say which item and which of its tracks.
 */
struct SessionMedia {
    std::string mediaPath;
    std::string anyFramePath;
    int track = -1;
    int playlistItem = -1;
    int playlistTrack = -1;
};

/** Tracks A–D in order, then every playlist item's tracks in playlist order;
    a media already listed is not repeated. */
std::vector<SessionMedia> getSessionMediaSet();

/** The summary model for @a media, from the in-memory reviews (sidecars load
    on first touch). Thumbnail paths are left empty. */
gfcReviewSummary::Doc buildReviewSummary(const std::vector<SessionMedia>& media,
                                         const std::string& title);

/** The first plate showing @a track, or -1. */
int plateShowingTrack(int track);

/**
 * Points plate @a plateIdx at round @a roundIndex of the review for
 * @a mediaPath only, whatever plate those notes were drawn on. The plate draws
 * copies owned by the bridge until the next syncPlateNotes(), which restores
 * the normal list. Returns false for an unknown plate or round.
 */
bool setPlateNotesToRound(int plateIdx, const std::string& mediaPath, int roundIndex);
```

- [ ] **Step 2: Implement the bridge API**

In `src/qt/SequenceLoadBridge_qt.cpp`:

(a) Add `#include "../gfcReviewSummary.h"` and `#include <filesystem>` with the other includes (skip any already present; `gfcnotestroke.h`, `gfcnotearrow.h`, `gfcnotebox.h`, `gfcnotetext.h` are already included for the pencil).

(b) Directly after `std::vector<gfcNotePoint> g_drawPoints;` add:

```cpp
/** Copies published by setPlateNotesToRound(); plates borrow them until the
    next syncPlateNotesImpl() republishes the real notes and frees these. */
std::vector<std::unique_ptr<gfcNote>> g_roundNoteCopies;
```

(c) At the end of `syncPlateNotesImpl()`, after its loop over plates, add:

```cpp
    // Every plate now borrows the real notes again, so the round copies a
    // summary thumbnail was drawing can go.
    g_roundNoteCopies.clear();
```

(d) Directly after the definition of `void syncPlateNotes()`, add:

```cpp
std::vector<SessionMedia> getSessionMediaSet() {
    std::vector<SessionMedia> out;
    auto add = [&out](const std::string& key, const std::string& framePath,
                      int track, int item, int itemTrack) {
        if (key.empty()) return;
        const std::string mediaPath = gfcNoteStore::normalisePath(key);
        for (const SessionMedia& m : out) {
            if (m.mediaPath == mediaPath) return;
        }
        SessionMedia m;
        m.mediaPath = mediaPath;
        m.anyFramePath = framePath.empty() ? key : framePath;
        m.track = track;
        m.playlistItem = item;
        m.playlistTrack = itemTrack;
        out.push_back(m);
    };
    for (int t = 0; t < GFC_MAX_SEQUENCES; ++t) {
        gfcSequence* seq = trackManager.getSequence(t);
        if (!seq || !seq->myGUI) continue;
        const std::string gui = seq->myGUI->getFilename();
        // Same key reviewForPlate() uses, so the summary finds the same review.
        add(seq->filenameGeneric.empty() ? gui : seq->filenameGeneric, gui, t, -1, -1);
    }
    if (auto* entries = playlistManager.getPlaylist()) {
        for (int i = 0; i < (int)entries->size(); ++i) {
            const gfcPlaylistItem& item = (*entries)[i];
            for (size_t k = 0; k < item.loadParams.size(); ++k) {
                add(item.loadParams[k].fileName, item.loadParams[k].fileName, -1, i, (int)k);
            }
        }
    }
    return out;
}

gfcReviewSummary::Doc buildReviewSummary(const std::vector<SessionMedia>& media,
                                         const std::string& title) {
    gfcReviewSummary::Doc doc;
    doc.title = title;
    doc.exportedAt = time(nullptr);
    doc.appVersion = JEFE_VERSION;
    for (const SessionMedia& m : media) {
        gfcReview& review = reviewForPath(m.mediaPath);
        gfcReviewSummary::Media entry = gfcReviewSummary::fromReview(review);
        if (review.revisions.empty()) {
            // No rounds: either there is no sidecar, or it exists and did not parse.
            gfcReview probe;
            probe.mediaPath = m.mediaPath;
            std::error_code ec;
            if (!gfcNoteStore::load(m.mediaPath, probe) &&
                std::filesystem::exists(gfcNoteStore::sidecarPathFor(m.mediaPath), ec)) {
                entry.notesReadable = false;
            }
        }
        doc.media.push_back(std::move(entry));
    }
    return doc;
}

int plateShowingTrack(int track) {
    if (track < 0) return -1;
    for (int i = 0; i < plateManager.plateCount(); ++i) {
        if (plateManager.getTrackOnPlate(i) == track) return i;
    }
    return -1;
}

namespace {
// A note's copy drawn on `plateIdx`. The overlay only draws notes whose quadID
// matches the plate, and a round's notes may have been drawn on another plate.
std::unique_ptr<gfcNote> copyNoteForPlate(const gfcNote& n, int plateIdx) {
    std::unique_ptr<gfcNote> c;
    if (const auto* s = dynamic_cast<const gfcNoteStroke*>(&n)) {
        auto x = std::make_unique<gfcNoteStroke>();
        x->pts = s->pts;
        c = std::move(x);
    } else if (const auto* a = dynamic_cast<const gfcNoteArrow*>(&n)) {
        auto x = std::make_unique<gfcNoteArrow>();
        x->tail = a->tail;
        x->head = a->head;
        c = std::move(x);
    } else if (const auto* b = dynamic_cast<const gfcNoteBox*>(&n)) {
        auto x = std::make_unique<gfcNoteBox>();
        x->a = b->a;
        x->b = b->b;
        c = std::move(x);
    } else if (const auto* t = dynamic_cast<const gfcNoteText*>(&n)) {
        auto x = std::make_unique<gfcNoteText>();
        x->anchor = t->anchor;
        x->text = t->text;
        c = std::move(x);
    } else {
        return nullptr;
    }
    c->id = n.id;
    c->author = n.author;
    c->name = n.name;
    c->quadID = plateIdx;
    c->from = n.from;
    c->to = n.to;
    c->always = n.always;
    c->colorR = n.colorR;
    c->colorG = n.colorG;
    c->colorB = n.colorB;
    c->size = n.size;
    return c;
}
}  // namespace

bool setPlateNotesToRound(int plateIdx, const std::string& mediaPath, int roundIndex) {
    if (plateIdx < 0 || plateIdx >= plateManager.plateCount()) return false;
    gfcReview& review = reviewForPath(mediaPath);
    if (roundIndex < 0 || roundIndex >= (int)review.revisions.size()) return false;
    std::vector<std::unique_ptr<gfcNote>> copies;
    std::vector<const gfcNote*> borrowed;
    for (const auto& n : review.revisions[roundIndex].notes) {
        if (!n) continue;
        if (auto c = copyNoteForPlate(*n, plateIdx)) {
            borrowed.push_back(c.get());
            copies.push_back(std::move(c));
        }
    }
    // Publish the new list before freeing the copies the plate may still hold.
    plateManager.setPlateNotes(plateIdx, borrowed);
    g_roundNoteCopies.clear();
    g_roundNoteCopies = std::move(copies);
    return true;
}
```

If `gfcSequence`, `gfcPlaylistItem`, `JEFE_VERSION` or `time()` are not already visible in this TU, add the includes the file uses elsewhere for them (`"../gfcSequence.h"`, `"../gfcplaylistitem.h"`, `"../gfcStructures.h"`, `<ctime>`).

- [ ] **Step 3: Declare the MainWindow API**

In `src/qt/MainWindow_qt.h`, after `stampActiveFrameNotes(...)`, add:

```cpp
    /** Counts from exportReviewSummary(). */
    struct ReviewSummaryStats {
        int media = 0;
        int rounds = 0;
        int notes = 0;
        int thumbs = 0;
        int thumbFail = 0;
        QString firstThumbnail;   // PDF only: the first thumbnail rendered (for tests)
    };

    /**
     * Writes the review summary of every media in the session to @a outPath;
     * the extension picks the format (.pdf, .txt or .csv). Fills @a message
     * with a one-line outcome either way. See
     * docs/superpowers/specs/2026-09-14-review-summary-export-design.md.
     */
    bool exportReviewSummary(const QString& outPath, ReviewSummaryStats* stats, QString* message);

    /** Headless end-to-end proof of the summary export (--summary-test <image>). */
    int runHeadlessSummaryTest(const QString& imagePath);
```

- [ ] **Step 4: Write the end-to-end test and the flags**

In `src/qt/MainWindow_qt.cpp`, add these includes with the others (skip any already present): `<QDateTime>`, `<QDir>`, `<QFile>`, `<QFileInfo>`, `<memory>`, `"../gfcReviewSummary.h"`, `"../gfcNoteStore.h"`, `"../gfcreview.h"`, `"../gfcrevision.h"`, `"../gfcnotestroke.h"`, `"../gfcnotetext.h"`. Then add a stub export and the complete test:

```cpp
bool MainWindow_Qt::exportReviewSummary(const QString&, ReviewSummaryStats*, QString* message) {
    if (message) *message = tr("Not implemented");
    return false;
}

int MainWindow_Qt::runHeadlessSummaryTest(const QString& imagePath) {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        printf("SUMMARY-TEST %s %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    auto readBytes = [](const QString& path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };
    if (!viewport_) { printf("SUMMARY-TEST FAIL no viewport\n"); fflush(stdout); return 2; }

    // A private copy of the image, so the sidecar next to it is this test's alone.
    const QString work = QDir::tempPath() + "/jefecheck_summarytest_" +
                         QString::number(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(work);
    const QString media = work + "/" + QFileInfo(imagePath).fileName();
    if (!QFile::copy(imagePath, media)) {
        printf("SUMMARY-TEST FAIL cannot copy %s\n", qPrintable(imagePath));
        fflush(stdout);
        return 2;
    }

    // One locked round with a stroke on frame 1 and a text note on every
    // frame, then an open round with no notes.
    {
        gfcReview review;
        review.mediaPath = gfcNoteStore::normalisePath(media.toStdString());
        gfcRevision& r1 = review.beginRevision("Supervisor");
        auto stroke = std::make_unique<gfcNoteStroke>();
        stroke->author = "Supervisor";
        stroke->quadID = 0;
        stroke->from = 1;
        stroke->to = 1;
        stroke->colorR = 1.0f; stroke->colorG = 0.0f; stroke->colorB = 0.0f;
        stroke->size = 12;
        stroke->pts = { gfcNotePoint{0.1f, 0.1f}, gfcNotePoint{0.9f, 0.9f}, gfcNotePoint{0.1f, 0.9f} };
        r1.addNote(std::move(stroke));
        auto text = std::make_unique<gfcNoteText>();
        text->author = "Supervisor";
        text->quadID = 0;
        text->always = true;
        text->anchor = gfcNotePoint{0.5f, 0.5f};
        text->text = "too warm, \"here\"";
        r1.addNote(std::move(text));
        r1.locked = true;
        review.beginRevision("Artist");
        check(gfcNoteStore::save(review), "fixture sidecar saved");
    }

    loadFileIntoPlate(0, media);
    jefe::qt::setActivePlate(0);

    ReviewSummaryStats stats;
    QString msg;
    const QString txt = work + "/summary.txt";
    check(exportReviewSummary(txt, &stats, &msg), "text summary exported");
    check(stats.media == 1 && stats.rounds == 2 && stats.notes == 2,
          "counts: one media, two rounds, two notes");
    const QString text = QString::fromUtf8(readBytes(txt));
    check(text.contains("== " + QFileInfo(media).fileName() + " =="), "text names the media");
    check(text.contains(QString::fromUtf8("Round 1 \xE2\x80\x94 Supervisor")) && text.contains("locked"),
          "text has the locked Supervisor round");
    check(text.contains(QString::fromUtf8("Round 2 \xE2\x80\x94 Artist")) && text.contains("  No notes"),
          "text keeps the empty Artist round");
    check(text.contains("\"too warm, \"here\"\""), "text quotes the text note");

    const QString csv = work + "/summary.csv";
    check(exportReviewSummary(csv, &stats, &msg), "CSV summary exported");
    const QByteArray csvBytes = readBytes(csv);
    check(csvBytes.startsWith("media,round_id,round_author,"), "CSV starts with the header");
    check(csvBytes.count("\r\n") == 3, "CSV has the header and one row per note");
    check(csvBytes.contains("\"too warm, \"\"here\"\"\""), "CSV quotes the text note");
    check(!QFile::exists(txt + ".partial") && !QFile::exists(csv + ".partial"), "no partial files left");
    check(!exportReviewSummary(work + "/summary.doc", &stats, &msg), "an unknown extension is refused");

    printf("SUMMARY-TEST: %s\n", failures == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    return failures == 0 ? 0 : 2;
}
```

In `src/main_qt.cpp`, directly after the `--stamp-notes` loop, add:

```cpp
    // --summary-test <image>: end-to-end proof of File -> Export Review Summary.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--summary-test") != 0) continue;
        const QString image = QString::fromUtf8(argv[i + 1]);
        QTimer::singleShot(5000, &window, [&window, image]() {
            const int code = window.runHeadlessSummaryTest(image);
            fflush(stdout);
            std::_Exit(code);
        });
        break;
    }

    // --export-summary <out>: write the review summary of what is loaded, then quit.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--export-summary") != 0) continue;
        const QString out = QString::fromUtf8(argv[i + 1]);
        QTimer::singleShot(6000, &window, [&window, out]() {
            MainWindow_Qt::ReviewSummaryStats s;
            QString msg;
            const bool ok = window.exportReviewSummary(out, &s, &msg);
            if (ok) {
                printf("SUMMARY: wrote=%s media=%d rounds=%d notes=%d thumbs=%d thumbfail=%d\n",
                       qPrintable(out), s.media, s.rounds, s.notes, s.thumbs, s.thumbFail);
            } else {
                printf("SUMMARY: FAIL %s\n", qPrintable(msg));
            }
            fflush(stdout);
            std::_Exit(ok ? 0 : 2);
        });
        break;
    }
```

- [ ] **Step 5: Build and run — expect failures**

Run: `cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; T=$(mktemp -d) && mkdir -p "$T/JefeCheck" && printf '[%%General]\nopenLoadWindowAtStartup=0\n\n[Session]\ncleanExit=true\n' > "$T/JefeCheck/JefeCheck.ini" && ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --config-dir "$T" --summary-test /Users/dgollas/projects/openexr-images/ScanLines/Blobbies.exr 2>&1 | grep SUMMARY-TEST; echo "exit=${pipestatus[1]}"`
Expected: `SUMMARY-TEST ok   fixture sidecar saved`, several `SUMMARY-TEST FAIL` lines, `SUMMARY-TEST: FAIL`, `exit=2`.

- [ ] **Step 6: Implement text and CSV export**

Replace the stub `exportReviewSummary` in `src/qt/MainWindow_qt.cpp` with:

```cpp
bool MainWindow_Qt::exportReviewSummary(const QString& outPath, ReviewSummaryStats* stats, QString* message) {
    auto say = [&](const QString& m) { if (message) *message = m; };
    const QString suffix = QFileInfo(outPath).suffix().toLower();
    if (suffix != "txt" && suffix != "csv") {
        say(tr("Unsupported summary format \"%1\": use .pdf, .txt or .csv").arg(suffix));
        return false;
    }
    const std::vector<jefe::qt::SessionMedia> media = jefe::qt::getSessionMediaSet();
    if (media.empty()) {
        say(tr("Nothing to summarise: the session has no media"));
        return false;
    }
    const QString title = currentSessionPath_.isEmpty()
                          ? tr("Untitled session")
                          : QFileInfo(currentSessionPath_).completeBaseName();
    const gfcReviewSummary::Doc doc = jefe::qt::buildReviewSummary(media, title.toStdString());

    const std::string contents = (suffix == "txt") ? gfcReviewSummary::toText(doc)
                                                   : gfcReviewSummary::toCsv(doc);
    std::string err;
    if (!gfcReviewSummary::writeFileAtomically(outPath.toStdString(), contents, &err)) {
        say(QString::fromStdString(err));
        return false;
    }

    ReviewSummaryStats s;
    s.media = int(doc.media.size());
    s.rounds = gfcReviewSummary::roundCount(doc);
    s.notes = gfcReviewSummary::noteCount(doc);
    if (stats) *stats = s;
    say(tr("Summary written: %1 %2 %3 media, %4 rounds, %5 notes")
            .arg(QFileInfo(outPath).fileName(), QString::fromUtf8("\xE2\x80\x94"))
            .arg(s.media).arg(s.rounds).arg(s.notes));
    return true;
}
```

- [ ] **Step 7: Build and run — expect pass**

Run the command from Step 5.
Expected: every line `SUMMARY-TEST ok`, then `SUMMARY-TEST: PASS`, `exit=0`. Also confirm `--notes-test` still exits 0 with all six self-tests `fail=0`.

- [ ] **Step 8: Commit**

`git add src/qt/SequenceLoadBridge_qt.h src/qt/SequenceLoadBridge_qt.cpp src/qt/MainWindow_qt.h src/qt/MainWindow_qt.cpp src/main_qt.cpp` and commit with subject `JEF-39: export a session's review summary as text or CSV`.

---

### Task 4: PDF export with thumbnails, state restore and the File menu action

**Files:**
- Modify: `src/qt/SequenceLoadBridge_qt.h`, `src/qt/SequenceLoadBridge_qt.cpp` (add `prepareTrackForRender` beside the Task 3 functions)
- Modify: `src/qt/MainWindow_qt.h` (private `renderSummaryThumbnails`), `src/qt/MainWindow_qt.cpp` (export, test, File menu around the "Stamp Notes into EXR…" action)
- Modify: `docs/superpowers/specs/2026-09-14-review-summary-export-design.md` (the "State restore" test bullet)

**Interfaces:**
- Consumes: Task 2 `jefe::qt::writeReviewSummaryPdf(doc, path, &pages, &err)`; Task 3 `SessionMedia`, `getSessionMediaSet`, `buildReviewSummary`, `plateShowingTrack`, `setPlateNotesToRound`, `ReviewSummaryStats`, `exportReviewSummary`, `runHeadlessSummaryTest`. Existing: `jefe::qt::saveSession(const std::string&) -> bool`, `jefe::qt::loadSession(const std::string&) -> bool`, `jefe::qt::startLoadingAllTracks()`, `jefe::qt::loadPlaylistItem(int)`, `jefe::qt::getTrackTimelineState(int).rangeStart`, `jefe::qt::getRenderSourceSize(int quadrant, int& w, int& h)`, `jefe::qt::RenderParams` (`quadrant, format, formatString, from, to, padding, scale, path, prefix, outWidth, outHeight, burnInNotes`), `jefe::qt::previewRenderFilename(const RenderParams&) -> std::string`, `jefe::qt::triggerSyncRender(const RenderParams&) -> int`, `jefe::qt::getCurrentFrame()`, `jefe::qt::seekToFrame(int)`, `jefe::qt::syncPlateNotes()`, `jefe::qt::getTrackParams(int).filename`, `MainWindow_Qt::refreshAfterSessionLoad()`. Bridge internals: `trackManager.getSequence(int)`, `trackManager.startLoadingSequence(int)`, `gfcSequence::getNumFrames()`.
- Produces: `bool jefe::qt::prepareTrackForRender(int track);` and the `.pdf` branch of `exportReviewSummary`; File → Export Review Summary… (`menu.file.exportsummary`).

- [ ] **Step 1: Extend the end-to-end test (failing)**

In `MainWindow_Qt::runHeadlessSummaryTest`, insert before the final `printf("SUMMARY-TEST: %s\n", …)`:

```cpp
    // PDF: thumbnails through the plate pipeline with the round's notes burned
    // in, and the session put back afterwards.
    const std::string beforeFile = jefe::qt::getTrackParams(0).filename;
    const int beforeFrame = jefe::qt::getCurrentFrame();
    const QString pdf = work + "/summary.pdf";
    check(exportReviewSummary(pdf, &stats, &msg), "PDF summary exported");
    printf("SUMMARY-TEST pdf: %s thumbs=%d thumbfail=%d\n", qPrintable(msg), stats.thumbs, stats.thumbFail);
    check(stats.thumbs == 2 && stats.thumbFail == 0, "one thumbnail per frame entry, none failed");
    const QByteArray pdfBytes = readBytes(pdf);
    check(pdfBytes.startsWith("%PDF-") && pdfBytes.trimmed().endsWith("%%EOF"), "PDF file is complete");
    check(!QFile::exists(pdf + ".partial"), "no partial PDF left");
    check(jefe::qt::getTrackParams(0).filename == beforeFile, "the track's media is restored");
    check(jefe::qt::getCurrentFrame() == beforeFrame, "the current frame is restored");

    // Burn-in reached the thumbnail: the same frame rendered with the empty
    // round (no notes) must differ from it.
    QImage withNotes(stats.firstThumbnail);
    check(!withNotes.isNull(), "first thumbnail readable");
    const std::vector<jefe::qt::SessionMedia> set = jefe::qt::getSessionMediaSet();
    if (!withNotes.isNull() && !set.empty()) {
        check(jefe::qt::setPlateNotesToRound(0, set[0].mediaPath, 1), "plate shows the empty round");
        jefe::qt::RenderParams p;
        p.quadrant = 0;
        p.format = 5;
        p.formatString = "png";
        p.from = p.to = jefe::qt::getTrackTimelineState(0).rangeStart;
        p.padding = 4;
        p.scale = 1.0f;
        p.path = work.toStdString();
        p.prefix = "nonotes_";
        p.outWidth = withNotes.width();
        p.outHeight = withNotes.height();
        p.burnInNotes = true;
        const QString file = QString::fromStdString(jefe::qt::previewRenderFilename(p));
        viewport_->makeCurrent();
        jefe::qt::triggerSyncRender(p);
        viewport_->doneCurrent();
        jefe::qt::syncPlateNotes();
        QImage without(file);
        double diff = 0.0;
        if (!without.isNull()) {
            const QImage a = withNotes.convertToFormat(QImage::Format_RGBA8888);
            const QImage b = without.convertToFormat(QImage::Format_RGBA8888);
            const int w = std::min(a.width(), b.width());
            const int h = std::min(a.height(), b.height());
            double sum = 0.0;
            long long n = 0;
            for (int y = 0; y < h; ++y) {
                const uchar* ra = a.constScanLine(y);
                const uchar* rb = b.constScanLine(y);
                for (int x = 0; x < w * 4; ++x) { sum += std::abs(int(ra[x]) - int(rb[x])); ++n; }
            }
            diff = n ? sum / double(n) : 0.0;
        }
        printf("SUMMARY-TEST burn-in mean abs diff: %.3f\n", diff);
        check(!without.isNull() && diff > 0.0, "notes are burned into the thumbnail");
    }
```

Add `#include <QImage>`, `<algorithm>` and `<cstdlib>` to `MainWindow_qt.cpp` if missing.

- [ ] **Step 2: Build and run — expect failures**

Run: `cmake --build build_qt -j8 2>&1 | grep -E 'error|Built target jefecheck$' ; T=$(mktemp -d) && mkdir -p "$T/JefeCheck" && printf '[%%General]\nopenLoadWindowAtStartup=0\n\n[Session]\ncleanExit=true\n' > "$T/JefeCheck/JefeCheck.ini" && ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --config-dir "$T" --summary-test /Users/dgollas/projects/openexr-images/ScanLines/Blobbies.exr 2>&1 | grep SUMMARY-TEST; echo "exit=${pipestatus[1]}"`
Expected: the Task 3 checks still `ok`; `SUMMARY-TEST FAIL PDF summary exported` (unsupported format) and the dependent PDF checks fail; `exit=2`.

- [ ] **Step 3: Add `prepareTrackForRender` to the bridge**

In `src/qt/SequenceLoadBridge_qt.h`, after `setPlateNotesToRound`:

```cpp
/**
 * Makes sure @a track has its frame list, starting the track's load when it
 * has none (a single image quick-loaded shows as a preview without one).
 * Renders force-decode the frame they draw, so no decode wait is needed.
 * Returns whether the track now has frames.
 */
bool prepareTrackForRender(int track);
```

In `src/qt/SequenceLoadBridge_qt.cpp`, after `setPlateNotesToRound`:

```cpp
bool prepareTrackForRender(int track) {
    gfcSequence* seq = trackManager.getSequence(track);
    if (!seq) return false;
    if (seq->getNumFrames() <= 0) trackManager.startLoadingSequence(track);
    return seq->getNumFrames() > 0;
}
```

Check `gfcSequence::getNumFrames()` in `src/gfcSequence.cpp` returns the size of the frame list that `initializeSequence` fills; if it returns something else (for example the loaded count), use the accessor that returns `frames.size()`.

- [ ] **Step 4: Render thumbnails and write the PDF**

In `src/qt/MainWindow_qt.h`, in the `private:` section, add:

```cpp
    /** Renders one thumbnail per frame entry of @a doc into a temporary directory
        (through the plate pipeline, that round's notes burned in), then restores
        the plates' notes, the session and the current frame. */
    void renderSummaryThumbnails(const std::vector<jefe::qt::SessionMedia>& media,
                                 gfcReviewSummary::Doc& doc, ReviewSummaryStats* stats);
```

with `#include "SequenceLoadBridge_qt.h"` near the top of `MainWindow_qt.h` if `jefe::qt::SessionMedia` is not yet visible there (it is glad-free).

In `src/qt/MainWindow_qt.cpp`, add `#include "ReviewSummaryPdf_qt.h"` and replace `exportReviewSummary` with:

```cpp
bool MainWindow_Qt::exportReviewSummary(const QString& outPath, ReviewSummaryStats* stats, QString* message) {
    auto say = [&](const QString& m) { if (message) *message = m; };
    const QString suffix = QFileInfo(outPath).suffix().toLower();
    if (suffix != "txt" && suffix != "csv" && suffix != "pdf") {
        say(tr("Unsupported summary format \"%1\": use .pdf, .txt or .csv").arg(suffix));
        return false;
    }
    const std::vector<jefe::qt::SessionMedia> media = jefe::qt::getSessionMediaSet();
    if (media.empty()) {
        say(tr("Nothing to summarise: the session has no media"));
        return false;
    }
    {
        // Refuse an unwritable destination before any rendering starts.
        QFile probe(outPath + ".partial");
        if (!probe.open(QIODevice::WriteOnly)) {
            say(tr("Cannot write %1").arg(outPath));
            return false;
        }
        probe.close();
        probe.remove();
    }
    const QString title = currentSessionPath_.isEmpty()
                          ? tr("Untitled session")
                          : QFileInfo(currentSessionPath_).completeBaseName();
    gfcReviewSummary::Doc doc = jefe::qt::buildReviewSummary(media, title.toStdString());

    ReviewSummaryStats s;
    s.media = int(doc.media.size());
    s.rounds = gfcReviewSummary::roundCount(doc);
    s.notes = gfcReviewSummary::noteCount(doc);

    if (suffix == "pdf") {
        renderSummaryThumbnails(media, doc, &s);
        int pages = 0;
        QString err;
        if (!jefe::qt::writeReviewSummaryPdf(doc, outPath, &pages, &err)) {
            say(err);
            return false;
        }
    } else {
        const std::string contents = (suffix == "txt") ? gfcReviewSummary::toText(doc)
                                                       : gfcReviewSummary::toCsv(doc);
        std::string err;
        if (!gfcReviewSummary::writeFileAtomically(outPath.toStdString(), contents, &err)) {
            say(QString::fromStdString(err));
            return false;
        }
    }

    if (stats) *stats = s;
    say(tr("Summary written: %1 %2 %3 media, %4 rounds, %5 notes")
            .arg(QFileInfo(outPath).fileName(), QString::fromUtf8("\xE2\x80\x94"))
            .arg(s.media).arg(s.rounds).arg(s.notes));
    return true;
}

void MainWindow_Qt::renderSummaryThumbnails(const std::vector<jefe::qt::SessionMedia>& media,
                                            gfcReviewSummary::Doc& doc, ReviewSummaryStats* stats) {
    const QString dir = QDir::tempPath() + "/jefecheck_summary_" +
                        QString::number(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(dir);
    const QString restore = dir + "/restore.jcs";
    const bool saved = jefe::qt::saveSession(restore.toStdString());
    const int savedFrame = jefe::qt::getCurrentFrame();

    for (size_t mi = 0; mi < media.size() && mi < doc.media.size(); ++mi) {
        const jefe::qt::SessionMedia& m = media[mi];
        gfcReviewSummary::Media& entry = doc.media[mi];
        int track = m.track;
        if (track < 0 && m.playlistItem >= 0) {
            // Loads the item's tracks, FX stacks and program state: its reviewed look.
            jefe::qt::loadPlaylistItem(m.playlistItem);
            track = m.playlistTrack;
        }
        const int plate = jefe::qt::plateShowingTrack(track);
        const bool ready = plate >= 0 && jefe::qt::prepareTrackForRender(track);
        const int firstFrame = ready ? jefe::qt::getTrackTimelineState(track).rangeStart : 1;

        for (size_t ri = 0; ri < entry.rounds.size(); ++ri) {
            for (size_t fi = 0; fi < entry.rounds[ri].frames.size(); ++fi) {
                gfcReviewSummary::Frame& f = entry.rounds[ri].frames[fi];
                if (!ready || !jefe::qt::setPlateNotesToRound(plate, m.mediaPath, int(ri))) {
                    ++stats->thumbFail;
                    continue;
                }
                jefe::qt::RenderParams p;
                p.quadrant = plate;
                p.format = 5;               // PNG
                p.formatString = "png";
                p.from = p.to = (f.frame == gfcReviewSummary::kAllFrames) ? firstFrame : f.frame;
                p.padding = 4;
                p.scale = 1.0f;
                p.path = dir.toStdString();
                p.prefix = QString("thumb_m%1_r%2_f%3_").arg(mi).arg(ri).arg(fi).toStdString();
                int sw = 0, sh = 0;
                jefe::qt::getRenderSourceSize(plate, sw, sh);
                if (sw > 0 && sh > 0) {
                    p.outWidth = 960;
                    p.outHeight = std::max(1, int(960.0 * sh / sw + 0.5));
                }
                p.burnInNotes = true;
                const QString file = QString::fromStdString(jefe::qt::previewRenderFilename(p));
                viewport_->makeCurrent();
                const int rendered = jefe::qt::triggerSyncRender(p);
                viewport_->doneCurrent();
                if (rendered == 1 && !QImage(file).isNull()) {
                    f.thumbnailPath = file.toStdString();
                    ++stats->thumbs;
                    if (stats->firstThumbnail.isEmpty()) stats->firstThumbnail = file;
                } else {
                    ++stats->thumbFail;
                }
            }
        }
    }

    // Put everything back: the plates' real notes, the session, the frame.
    jefe::qt::syncPlateNotes();
    if (saved) {
        viewport_->makeCurrent();
        if (jefe::qt::loadSession(restore.toStdString())) jefe::qt::startLoadingAllTracks();
        viewport_->doneCurrent();
        refreshAfterSessionLoad();
    }
    jefe::qt::seekToFrame(savedFrame);
}
```

- [ ] **Step 5: Build and run — expect pass**

Run the command from Step 2.
Expected: every `SUMMARY-TEST ok`, `SUMMARY-TEST pdf: Summary written: summary.pdf — 1 media, 2 rounds, 2 notes thumbs=2 thumbfail=0`, a burn-in diff line with a value above 0, `SUMMARY-TEST: PASS`, `exit=0`.
If the thumbnails render but are black or identical to the no-notes render, the plate is not drawing the track's frame: check whether the plate is still in preview mode for the quick-loaded image and whether `prepareTrackForRender` started the load, and fix it in the bridge — do not weaken the checks.

- [ ] **Step 6: Look at a thumbnail and the PDF**

`P=$(ls -dt ${TMPDIR:-/tmp}/jefecheck_summarytest_* | head -1) && sips -s format png "$P/summary.pdf" --out "$P/summary_page1.png" > /dev/null && ls "$P" && echo "$P/summary_page1.png"`
Read `summary_page1.png` and one `thumb_*.png` from the newest `${TMPDIR:-/tmp}/jefecheck_summary_*` directory with the Read tool. Expected: the Blobbies image with a red stroke and the text note burned in; the PDF shows the header, `Blobbies.exr`, `Round 1 — Supervisor — … — locked` with two entries (`All frames`, `Frame 1`) each with that thumbnail, and `Round 2 — Artist — … — open` / `No notes`.

- [ ] **Step 7: Add the File menu action**

In `src/qt/MainWindow_qt.cpp`, directly after the `"Stamp Notes into EXR…"` action (`->setObjectName("menu.file.stampnotes");`), add:

```cpp
    fileMenu->addAction(tr("Export Review Summary…"), this, [this]() {
        QString filter;
        QString out = QFileDialog::getSaveFileName(
            this, tr("Export Review Summary"), QString(),
            tr("PDF (*.pdf);;Text (*.txt);;CSV (*.csv)"), &filter);
        if (out.isEmpty()) return;
        if (QFileInfo(out).suffix().isEmpty()) {
            out += filter.startsWith("Text") ? ".txt" : filter.startsWith("CSV") ? ".csv" : ".pdf";
        }
        ReviewSummaryStats stats;
        QString message;
        if (exportReviewSummary(out, &stats, &message)) {
            statusBar()->showMessage(message, 8000);
        } else {
            QMessageBox::warning(this, tr("Export Review Summary"), message);
        }
    })->setObjectName("menu.file.exportsummary");
```

- [ ] **Step 8: CLI check**

Run: `T=$(mktemp -d) && mkdir -p "$T/JefeCheck" && printf '[%%General]\nopenLoadWindowAtStartup=0\n\n[Session]\ncleanExit=true\n' > "$T/JefeCheck/JefeCheck.ini" && ./build_qt/jefecheck.app/Contents/MacOS/jefecheck --config-dir "$T" --open-file /Users/dgollas/projects/openexr-images/ScanLines/Blobbies.exr --export-summary "$T/cli.pdf" 2>&1 | grep '^SUMMARY:'; echo "exit=${pipestatus[1]}"`
Expected: `SUMMARY: wrote=<T>/cli.pdf media=1 rounds=<n> notes=<n> thumbs=<n> thumbfail=0` and `exit=0` (counts depend on any sidecar already beside that file; `rounds=0 notes=0 thumbs=0` when there is none).
Then confirm `--notes-test` still exits 0.

- [ ] **Step 9: Align the spec's restore test with what is checked**

In `docs/superpowers/specs/2026-09-14-review-summary-export-design.md`, replace the bullet

```
- **State restore**: after the export the active track's filename, current
  frame and plate exposure equal their values before it.
```

with

```
- **State restore**: after the export the track's filename and the current
  frame equal their values before it (the session reload restores colour
  correction with the rest of the session file).
```

- [ ] **Step 10: Commit**

`git add src/qt/SequenceLoadBridge_qt.h src/qt/SequenceLoadBridge_qt.cpp src/qt/MainWindow_qt.h src/qt/MainWindow_qt.cpp docs/superpowers/specs/2026-09-14-review-summary-export-design.md` and commit with subject `JEF-39: export the review summary as a PDF with graded thumbnails`.

---

## Self-Review

- **Spec coverage:** menu action + dialog filters (Task 4 Step 7); CLI `--export-summary` with the specified output line and exit codes (Task 3 Step 4); media set = tracks then playlist, de-duplicated (Task 3 Step 2); media without notes / unreadable notes (Task 1 model + Task 3 `buildReviewSummary`); document model and grouping rules (Task 1); TXT and CSV formats (Task 1 golden strings); thumbnails through the plate pipeline with only that round's notes, `kAllFrames` at the first frame, 960 px PNG, restore on every path (Task 4 Step 4 — the loop has no early return, so the restore always runs); "thumbnail unavailable" and `thumbfail` (Tasks 2 and 4); PDF layout — A4, 300 dpi, 15 mm margins, header block, media per page, round header, 80 mm thumbnail entries never split, footer `page X of Y` (Task 2); tests: model self-test, PDF self-test, end-to-end counts, burn-in diff, restore (Tasks 1–4); errors: no media, unwritable path before rendering, partial + rename (Tasks 1, 2, 4).
- **Deviation recorded:** the restore check covers filename and current frame, not plate exposure (no exposure getter exists in the bridge); Task 4 Step 9 updates the spec to match.
- **Type consistency:** `SessionMedia`, `ReviewSummaryStats` fields, `setPlateNotesToRound(int, const std::string&, int)`, `prepareTrackForRender(int)`, `writeReviewSummaryPdf(doc, path, int*, QString*)` are used with the same signatures in every task.

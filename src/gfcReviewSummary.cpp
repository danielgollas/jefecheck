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

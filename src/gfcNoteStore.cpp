#include "gfcNoteStore.h"

#include "gfcreview.h"
#include "gfcrevision.h"
#include "gfcnote.h"
#include "gfcnotestroke.h"
#include "gfcnotearrow.h"
#include "gfcnotebox.h"
#include "gfcnotetext.h"

#include "xmlParser.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
namespace
{
	// Matches the trailing ".<digits-or-hashes>.<ext>" frame-number segment
	// of a sequence filename, e.g. ".0101.exr" or ".####.exr". Used both to
	// normalise a path (Task's normalisePath) and to strip the frame+ext
	// segment off when computing the sidecar's basename.
	const std::regex& frameNumberPattern()
	{
		static const std::regex re(R"(\.(\d+|#+)\.([^./\\]+)$)");
		return re;
	}

	// ---- minimal self-contained SHA-1 -------------------------------------
	// Only used to name the fallback sidecar file so two different sequence
	// paths never collide. Deliberately not sharing RakNet's CSHA1 (src/SHA1.h)
	// here -- that header drags in RakMemoryOverride.h/Export.h from the
	// networking side of the tree, and this store owns exactly two files
	// (src/gfcNoteStore.{h,cpp}) per the plan's file-ownership map, so it
	// stays fully self-contained instead of creating a cross-module coupling
	// nobody asked for. Public-domain algorithm (FIPS PUB 180-1).
	struct Sha1State
	{
		uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
		uint64_t bitLen = 0;
		unsigned char buffer[64];
		size_t bufferLen = 0;

		static uint32_t rol(uint32_t v, int bits) { return (v << bits) | (v >> (32 - bits)); }

		void processBlock(const unsigned char* p)
		{
			uint32_t w[80];
			for (int i = 0; i < 16; ++i)
			{
				w[i] = (static_cast<uint32_t>(p[i * 4]) << 24) |
					   (static_cast<uint32_t>(p[i * 4 + 1]) << 16) |
					   (static_cast<uint32_t>(p[i * 4 + 2]) << 8) |
					   (static_cast<uint32_t>(p[i * 4 + 3]));
			}
			for (int i = 16; i < 80; ++i)
			{
				w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
			}

			uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
			for (int i = 0; i < 80; ++i)
			{
				uint32_t f, k;
				if (i < 20)      { f = (b & c) | ((~b) & d);        k = 0x5A827999u; }
				else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
				else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
				else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }

				uint32_t temp = rol(a, 5) + f + e + k + w[i];
				e = d; d = c; c = rol(b, 30); b = a; a = temp;
			}

			h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
		}

		void update(const unsigned char* data, size_t len)
		{
			bitLen += static_cast<uint64_t>(len) * 8;
			while (len > 0)
			{
				size_t take = std::min(len, sizeof(buffer) - bufferLen);
				std::memcpy(buffer + bufferLen, data, take);
				bufferLen += take;
				data += take;
				len -= take;
				if (bufferLen == sizeof(buffer))
				{
					processBlock(buffer);
					bufferLen = 0;
				}
			}
		}
	};

	// Runs Merkle-Damgard padding (0x80, zero pad to 56 mod 64, then the
	// ORIGINAL bit length as big-endian 64-bit) and returns the digest as
	// lowercase hex. A free function rather than a method on Sha1State so
	// the "original bit length" is a local captured before update() mutates
	// state.bitLen with the padding bytes.

	std::string sha1Hex(const std::string& input)
	{
		Sha1State state;
		const uint64_t originalBitLen = static_cast<uint64_t>(input.size()) * 8;
		state.update(reinterpret_cast<const unsigned char*>(input.data()), input.size());

		// Standard SHA-1 finish: append 0x80, zero-pad to 56 bytes mod 64,
		// then the ORIGINAL bit length as a big-endian 64-bit integer.
		unsigned char pad = 0x80;
		state.update(&pad, 1);
		unsigned char zero = 0x00;
		while (state.bufferLen != 56)
		{
			state.update(&zero, 1);
		}
		unsigned char lenBytes[8];
		for (int i = 0; i < 8; ++i)
		{
			lenBytes[7 - i] = static_cast<unsigned char>(originalBitLen >> (8 * i));
		}
		std::memcpy(state.buffer + 56, lenBytes, 8);
		state.processBlock(state.buffer);
		state.bufferLen = 0;

		unsigned char digest[20];
		for (int i = 0; i < 5; ++i)
		{
			digest[i * 4]     = static_cast<unsigned char>(state.h[i] >> 24);
			digest[i * 4 + 1] = static_cast<unsigned char>(state.h[i] >> 16);
			digest[i * 4 + 2] = static_cast<unsigned char>(state.h[i] >> 8);
			digest[i * 4 + 3] = static_cast<unsigned char>(state.h[i]);
		}

		static const char hexDigits[] = "0123456789abcdef";
		std::string out;
		out.reserve(40);
		for (unsigned char byte : digest)
		{
			out.push_back(hexDigits[(byte >> 4) & 0xF]);
			out.push_back(hexDigits[byte & 0xF]);
		}
		return out;
	}

	// <sequence_dir>/<basename>.jnotes -- basename has the frame-number
	// segment AND the extension stripped (sh010_v003.####.exr -> sh010_v003).
	std::string mediaDirSidecarPath(const std::string& normalisedPath)
	{
		std::filesystem::path p(normalisedPath);
		std::string dir = p.parent_path().string();
		std::string filename = p.filename().string();

		std::string base;
		std::smatch m;
		if (std::regex_search(filename, m, frameNumberPattern()))
		{
			base = filename.substr(0, static_cast<size_t>(m.position(0)));
		}
		else
		{
			size_t dot = filename.find_last_of('.');
			base = (dot == std::string::npos) ? filename : filename.substr(0, dot);
		}

		if (dir.empty())
		{
			return base + ".jnotes";
		}
		return dir + "/" + base + ".jnotes";
	}

	std::string fallbackSidecarPath(const std::string& normalisedPath)
	{
		const char* home = std::getenv("HOME");
		std::string homeDir = (home && *home) ? home : ".";
		return homeDir + "/.config/jefecheck/notes/" + sha1Hex(normalisedPath) + ".jnotes";
	}

	// Detect unwritability by attempting a real write, never by inspecting
	// permission bits (an NFS mount can lie about those).
	bool directoryIsWritable(const std::string& dir)
	{
		std::error_code ec;
		std::filesystem::create_directories(dir, ec); // no-op if it already exists

		std::string probe = dir + "/.jnotes_write_probe";
		std::ofstream f(probe, std::ios::out | std::ios::trunc);
		bool ok = f.is_open();
		if (ok)
		{
			f.close();
			std::remove(probe.c_str());
		}
		return ok;
	}

	const char* noteTypeToString(gfcNoteType t)
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

	bool noteTypeFromString(const std::string& s, gfcNoteType& out)
	{
		if (s == "stroke") { out = GFCNOTE_STROKE; return true; }
		if (s == "arrow")  { out = GFCNOTE_ARROW;  return true; }
		if (s == "box")    { out = GFCNOTE_BOX;    return true; }
		if (s == "text")   { out = GFCNOTE_TEXT;   return true; }
		return false;
	}

	std::string intToStr(long long v)
	{
		return std::to_string(v);
	}

	// Full float precision so a value written survives the text round trip
	// (default ostream precision, 6 significant digits, is not enough).
	std::string floatToStr(float v)
	{
		std::ostringstream ss;
		ss.precision(9);
		ss << v;
		return ss.str();
	}

	std::string attrStr(const XMLNode& node, const char* name)
	{
		XMLCSTR v = node.getAttribute(name);
		return v ? std::string(v) : std::string();
	}

	int attrInt(const XMLNode& node, const char* name, int def)
	{
		XMLCSTR v = node.getAttribute(name);
		if (!v || !*v)
		{
			return def;
		}
		return std::atoi(v);
	}

	float attrFloat(const XMLNode& node, const char* name, float def)
	{
		XMLCSTR v = node.getAttribute(name);
		if (!v || !*v)
		{
			return def;
		}
		return static_cast<float>(std::atof(v));
	}

	long long attrInt64(const XMLNode& node, const char* name, long long def)
	{
		XMLCSTR v = node.getAttribute(name);
		if (!v || !*v)
		{
			return def;
		}
		return std::atoll(v);
	}

	// Builds the whole <jefecheckNotes> document for `review`.
	XMLNode buildXml(const gfcReview& review, const std::string& normalisedPath)
	{
		XMLNode xTop = XMLNode::createXMLTopNode("jefecheckNotes");
		xTop.addAttribute("version", "1");

		XMLNode xMedia = xTop.addChild("media");
		xMedia.addAttribute("path", normalisedPath.c_str());
		xMedia.addAttribute("fingerprint", review.fingerprint.c_str());

		for (const gfcRevision& rev : review.revisions)
		{
			XMLNode xRev = xTop.addChild("revision");
			xRev.addAttribute("id", rev.id.c_str());
			xRev.addAttribute("author", rev.author.c_str());
			xRev.addAttribute("created", intToStr(static_cast<long long>(rev.created)).c_str());
			xRev.addAttribute("modified", intToStr(static_cast<long long>(rev.modified)).c_str());
			xRev.addAttribute("locked", rev.locked ? "1" : "0");

			for (const std::unique_ptr<gfcNote>& notePtr : rev.notes)
			{
				const gfcNote& n = *notePtr;
				XMLNode xNote = xRev.addChild("note");
				xNote.addAttribute("id", n.id.c_str());
				xNote.addAttribute("type", noteTypeToString(n.noteType()));
				xNote.addAttribute("author", n.author.c_str());
				xNote.addAttribute("name", n.name.c_str());
				xNote.addAttribute("quad", intToStr(n.quadID).c_str());
				xNote.addAttribute("from", intToStr(n.from).c_str());
				xNote.addAttribute("to", intToStr(n.to).c_str());
				xNote.addAttribute("always", n.always ? "1" : "0");
				xNote.addAttribute("r", floatToStr(n.colorR).c_str());
				xNote.addAttribute("g", floatToStr(n.colorG).c_str());
				xNote.addAttribute("b", floatToStr(n.colorB).c_str());
				xNote.addAttribute("size", intToStr(n.size).c_str());

				if (n.noteType() == GFCNOTE_TEXT)
				{
					xNote.addAttribute("text", static_cast<const gfcNoteText&>(n).text.c_str());
				}

				for (const gfcNotePoint& pt : n.points())
				{
					XMLNode xp = xNote.addChild("p");
					xp.addAttribute("x", floatToStr(pt.x).c_str());
					xp.addAttribute("y", floatToStr(pt.y).c_str());
				}
			}
		}

		return xTop;
	}

	// Constructs the correct concrete subclass for `type` and restores its
	// own geometry members from `pts` -- points() is read-only output, so
	// this is the only place geometry gets written back in.
	std::unique_ptr<gfcNote> makeNoteWithGeometry(gfcNoteType type, const std::vector<gfcNotePoint>& pts)
	{
		switch (type)
		{
			case GFCNOTE_STROKE:
			{
				auto n = std::make_unique<gfcNoteStroke>();
				n->pts = pts;
				return n;
			}
			case GFCNOTE_ARROW:
			{
				auto n = std::make_unique<gfcNoteArrow>();
				if (pts.size() >= 1) n->tail = pts[0];
				if (pts.size() >= 2) n->head = pts[1];
				return n;
			}
			case GFCNOTE_BOX:
			{
				auto n = std::make_unique<gfcNoteBox>();
				if (pts.size() >= 1) n->a = pts[0];
				if (pts.size() >= 2) n->b = pts[1];
				return n;
			}
			case GFCNOTE_TEXT:
			{
				auto n = std::make_unique<gfcNoteText>();
				if (pts.size() >= 1) n->anchor = pts[0];
				return n;
			}
		}
		return nullptr;
	}

	// Parses a <jefecheckNotes> document (already located) into `out`.
	bool loadFromXml(const XMLNode& xTop, gfcReview& out)
	{
		XMLNode xMedia = xTop.getChildNode("media");
		if (!xMedia.isEmpty())
		{
			out.mediaPath = attrStr(xMedia, "path");
			out.fingerprint = attrStr(xMedia, "fingerprint");
		}

		int numRevisions = xTop.nChildNode("revision");
		int revIter = 0;
		for (int i = 0; i < numRevisions; ++i)
		{
			XMLNode xRev = xTop.getChildNode("revision", &revIter);

			out.revisions.emplace_back();
			gfcRevision& rev = out.revisions.back();
			rev.id = attrStr(xRev, "id");
			rev.author = attrStr(xRev, "author");
			rev.created = static_cast<time_t>(attrInt64(xRev, "created", 0));
			rev.modified = static_cast<time_t>(attrInt64(xRev, "modified", 0));
			rev.locked = attrInt(xRev, "locked", 0) != 0;

			int numNotes = xRev.nChildNode("note");
			int noteIter = 0;
			for (int j = 0; j < numNotes; ++j)
			{
				XMLNode xNote = xRev.getChildNode("note", &noteIter);

				gfcNoteType type;
				if (!noteTypeFromString(attrStr(xNote, "type"), type))
				{
					continue; // unknown type= value: skip rather than guess a subclass
				}

				std::vector<gfcNotePoint> pts;
				int numPts = xNote.nChildNode("p");
				int ptIter = 0;
				for (int k = 0; k < numPts; ++k)
				{
					XMLNode xp = xNote.getChildNode("p", &ptIter);
					gfcNotePoint pt;
					pt.x = attrFloat(xp, "x", 0.0f);
					pt.y = attrFloat(xp, "y", 0.0f);
					pts.push_back(pt);
				}

				std::unique_ptr<gfcNote> note = makeNoteWithGeometry(type, pts);
				if (!note)
				{
					continue;
				}

				note->id = attrStr(xNote, "id");
				note->author = attrStr(xNote, "author");
				note->name = attrStr(xNote, "name");
				note->quadID = attrInt(xNote, "quad", 0);
				note->from = attrInt(xNote, "from", 0);
				note->to = attrInt(xNote, "to", 0);
				note->always = attrInt(xNote, "always", 0) != 0;
				note->colorR = attrFloat(xNote, "r", 1.0f);
				note->colorG = attrFloat(xNote, "g", 0.2f);
				note->colorB = attrFloat(xNote, "b", 0.2f);
				note->size = attrInt(xNote, "size", 3);

				if (type == GFCNOTE_TEXT)
				{
					static_cast<gfcNoteText&>(*note).text = attrStr(xNote, "text");
				}

				rev.notes.push_back(std::move(note));
			}
		}

		return true;
	}

	bool writeXmlTo(const XMLNode& xTop, const std::string& path)
	{
		std::error_code ec;
		std::filesystem::path parent = std::filesystem::path(path).parent_path();
		if (!parent.empty())
		{
			std::filesystem::create_directories(parent, ec);
		}
		return xTop.writeToFile(path.c_str()) == eXMLErrorNone;
	}

	// Loads the <jefecheckNotes> root from `path` if it exists and parses.
	// Silent on a missing file -- callers try multiple candidate locations.
	bool tryLoad(const std::string& path, gfcReview& out)
	{
		std::error_code ec;
		if (!std::filesystem::exists(path, ec) || ec)
		{
			return false;
		}

		XMLResults results;
		XMLNode xFile = XMLNode::parseFile(path.c_str(), NULL, &results);
		if (results.error != eXMLErrorNone)
		{
			return false;
		}

		XMLNode xTop = xFile.getChildNode("jefecheckNotes");
		if (xTop.isEmpty())
		{
			// Some xmlParser builds hand back the named root directly when
			// there is no separate <?xml?> declaration in front of it.
			if (std::strcmp(xFile.getName() ? xFile.getName() : "", "jefecheckNotes") == 0)
			{
				xTop = xFile;
			}
			else
			{
				return false;
			}
		}

		return loadFromXml(xTop, out);
	}
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

std::string gfcNoteStore::normalisePath(const std::string& anyFramePath)
{
	std::smatch m;
	if (std::regex_search(anyFramePath, m, frameNumberPattern()))
	{
		std::string hashes(static_cast<size_t>(m.length(1)), '#');
		std::string result = anyFramePath;
		result.replace(static_cast<size_t>(m.position(1)), static_cast<size_t>(m.length(1)), hashes);
		return result;
	}
	return anyFramePath;
}

std::string gfcNoteStore::sidecarPathFor(const std::string& normalisedPath)
{
	std::string primary = mediaDirSidecarPath(normalisedPath);
	std::filesystem::path primaryDir = std::filesystem::path(primary).parent_path();
	std::string dirStr = primaryDir.empty() ? std::string(".") : primaryDir.string();

	if (directoryIsWritable(dirStr))
	{
		return primary;
	}
	return fallbackSidecarPath(normalisedPath);
}

bool gfcNoteStore::save(const gfcReview& review)
{
	std::string normalisedPath = normalisePath(review.mediaPath);
	std::string path = sidecarPathFor(normalisedPath);

	XMLNode xTop = buildXml(review, normalisedPath);

	if (writeXmlTo(xTop, path))
	{
		return true;
	}

	// The path sidecarPathFor() chose failed anyway (e.g. a race between the
	// writability probe and the real write) -- fall back as a last resort
	// rather than silently losing the round.
	std::string fallback = fallbackSidecarPath(normalisedPath);
	if (fallback == path)
	{
		return false;
	}
	return writeXmlTo(xTop, fallback);
}

bool gfcNoteStore::load(const std::string& normalisedPath, gfcReview& out)
{
	std::string primary = mediaDirSidecarPath(normalisedPath);
	if (tryLoad(primary, out))
	{
		return true;
	}

	std::string fallback = fallbackSidecarPath(normalisedPath);
	if (fallback != primary && tryLoad(fallback, out))
	{
		return true;
	}

	return false;
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------
int noteStoreSelfTest()
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
			std::fprintf(stderr, "NOTE-STORE FAIL: %s\n", msg);
		}
	};

	check(gfcNoteStore::normalisePath("/j/sh010.0101.exr") == "/j/sh010.####.exr",
		  "a frame number collapses to a pattern");
	check(gfcNoteStore::normalisePath("/j/sh010.####.exr") == "/j/sh010.####.exr",
		  "an already-normalised path is unchanged");

	// round trip, in a scratch directory that is writable
	std::string testDir = "/tmp/jefe_notes_test_" + std::to_string(static_cast<long long>(time(nullptr)));
	std::filesystem::create_directories(testDir);

	gfcReview w;
	w.mediaPath = testDir + "/sh.####.exr";
	w.fingerprint = "a1b2c3";
	gfcRevision& rev = w.beginRevision("tester");
	rev.locked = true; // exercise the locked path through save/load too

	auto s = std::make_unique<gfcNoteStroke>();
	s->pts = { {0.1f, 0.2f}, {0.3f, 0.4f} };
	s->from = 7; s->to = 9; s->quadID = 2;
	s->author = "tester";
	s->name = "note-name";
	s->colorR = 1.0f; s->colorG = 0.2f; s->colorB = 0.2f;
	s->size = 5;
	const std::string strokeId = s->id;
	rev.notes.push_back(std::move(s)); // push directly: rev is locked, addNote() would refuse

	auto arrow = std::make_unique<gfcNoteArrow>();
	arrow->tail = {0.0f, 0.0f};
	arrow->head = {1.0f, 1.0f};
	rev.notes.push_back(std::move(arrow));

	auto box = std::make_unique<gfcNoteBox>();
	box->a = {0.2f, 0.2f};
	box->b = {0.8f, 0.8f};
	rev.notes.push_back(std::move(box));

	auto text = std::make_unique<gfcNoteText>();
	text->anchor = {0.5f, 0.5f};
	text->text = "hello notes";
	rev.notes.push_back(std::move(text));

	check(gfcNoteStore::save(w), "the review writes");

	std::string expectedPath = testDir + "/sh.jnotes";
	check(std::filesystem::exists(expectedPath), "the sidecar lands beside the media");

	gfcReview back;
	check(gfcNoteStore::load(w.mediaPath, back), "the review reads back");
	check(back.mediaPath == w.mediaPath, "the media path survives");
	check(back.fingerprint == w.fingerprint, "the fingerprint survives");
	check(back.revisions.size() == 1, "one revision survives");
	if (back.revisions.size() == 1)
	{
		check(back.revisions[0].locked == true, "the lock flag survives");
		check(back.revisions[0].author == "tester", "the revision author survives");
		check(back.revisions[0].notes.size() == 4, "all four notes survive");

		if (back.revisions[0].notes.size() == 4)
		{
			gfcNote* n0 = back.revisions[0].notes[0].get();
			check(n0->noteType() == GFCNOTE_STROKE, "note 0 is a stroke");
			check(n0->id == strokeId, "a note's id survives");
			check(n0->quadID == 2, "quadID survives");
			check(n0->from == 7 && n0->to == 9, "the frame range survives");
			check(n0->author == "tester", "note author survives");
			check(n0->name == "note-name", "note name survives");
			check(n0->size == 5, "size survives");
			check(n0->colorR == 1.0f && n0->colorG == 0.2f && n0->colorB == 0.2f, "colour survives");
			check(n0->points().size() == 2, "both stroke points survive");
			if (n0->points().size() == 2)
			{
				check(n0->points()[0].x == 0.1f && n0->points()[0].y == 0.2f, "stroke point 0 survives");
				check(n0->points()[1].x == 0.3f && n0->points()[1].y == 0.4f, "stroke point 1 survives");
			}

			gfcNote* n1 = back.revisions[0].notes[1].get();
			check(n1->noteType() == GFCNOTE_ARROW, "note 1 is an arrow");
			check(n1->points().size() == 2, "an arrow has tail+head");

			gfcNote* n2 = back.revisions[0].notes[2].get();
			check(n2->noteType() == GFCNOTE_BOX, "note 2 is a box");
			check(n2->points().size() == 2, "a box has two corners");

			gfcNote* n3 = back.revisions[0].notes[3].get();
			check(n3->noteType() == GFCNOTE_TEXT, "note 3 is text");
			check(static_cast<gfcNoteText*>(n3)->text == "hello notes", "text content survives");
			check(n3->points().size() == 1, "text has one anchor point");
		}
	}

	// read-only fallback: make the media directory unwritable, save again
	// (a *different* review so it can't be satisfied by the file we already
	// wrote above), and confirm the note lands under ~/.config and still
	// reads back from there.
	std::filesystem::permissions(testDir,
		std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
		std::filesystem::perm_options::replace);

	gfcReview ro;
	ro.mediaPath = testDir + "/ro.####.exr";
	gfcRevision& roRev = ro.beginRevision("readonly-tester");
	auto roNote = std::make_unique<gfcNoteBox>();
	roNote->a = {0.0f, 0.0f};
	roNote->b = {1.0f, 1.0f};
	roRev.addNote(std::move(roNote));

	bool roSaved = gfcNoteStore::save(ro);

	// restore permissions unconditionally before asserting, so a failed
	// check doesn't leave a directory the test harness can't clean up
	std::filesystem::permissions(testDir,
		std::filesystem::perms::owner_all,
		std::filesystem::perm_options::replace);

	check(roSaved, "save() still succeeds when the media directory is read-only");

	std::string roNormalised = gfcNoteStore::normalisePath(ro.mediaPath);
	std::string roMediaDirPath = testDir + "/ro.jnotes";
	check(!std::filesystem::exists(roMediaDirPath),
		  "the read-only media directory did NOT receive the sidecar");

	const char* home = std::getenv("HOME");
	std::string expectedFallbackDir = (home && *home) ? (std::string(home) + "/.config/jefecheck/notes") : "./.config/jefecheck/notes";
	check(std::filesystem::exists(expectedFallbackDir), "the fallback directory was created under ~/.config");

	gfcReview roBack;
	check(gfcNoteStore::load(roNormalised, roBack), "load() finds the note via the fallback location");
	check(roBack.revisions.size() == 1 && roBack.revisions[0].notes.size() == 1,
		  "the read-only-fallback note round-trips");

	std::printf("NOTE-STORE: pass=%d fail=%d\n", pass, fail);
	return fail == 0 ? 0 : 1;
}

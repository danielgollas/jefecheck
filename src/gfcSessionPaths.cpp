#include "gfcSessionPaths.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "xmlParser.h"

namespace
{
	void setErr(std::string* err, const std::string& msg)
	{
		if (err) *err = msg;
	}

	bool parseRoot(const std::string& xml, XMLNode& top, XMLNode& root, std::string* err)
	{
		// A saved .jcs always starts with a UTF-8 BOM (gfcSessionManager
		// writes one). XMLNode::parseFile strips a leading EF BB BF itself;
		// XMLNode::parseString, which every caller here uses, does not --
		// so every caller of this file would otherwise see a document with
		// no <root>.
		const char* text = xml.c_str();
		if (xml.compare(0, 3, "\xEF\xBB\xBF") == 0) text += 3;

		XMLResults results;
		top = XMLNode::parseString(text, NULL, &results);
		if (results.error != eXMLErrorNone)
		{
			setErr(err, "session XML does not parse (xmlParser error " + std::to_string(static_cast<int>(results.error)) + ")");
			return false;
		}
		root = top.getChildNode("root");
		if (root.isEmpty())
		{
			setErr(err, "session XML has no <root>");
			return false;
		}
		return true;
	}

	// Calls fn(node, attributeName, kind, index, sub) for every media reference.
	template <typename Fn>
	void forEachMediaAttribute(XMLNode root, Fn fn)
	{
		XMLNode tracks = root.getChildNode("tracks");
		const int trackCount = tracks.isEmpty() ? 0 : tracks.nChildNode("track");
		for (int i = 0; i < trackCount; ++i)
		{
			fn(tracks.getChildNode("track", i), "filename", gfcSessionPaths::Kind::Track, i, -1);
		}
		XMLNode playlist = root.getChildNode("playlist");
		const int entryCount = playlist.isEmpty() ? 0 : playlist.nChildNode("e");
		for (int i = 0; i < entryCount; ++i)
		{
			XMLNode entry = playlist.getChildNode("e", i);
			const int tCount = entry.nChildNode("t");
			for (int k = 0; k < tCount; ++k)
			{
				fn(entry.getChildNode("t", k), "fn", gfcSessionPaths::Kind::Playlist, i, k);
			}
		}
	}

	// "no LUT" is the plate@lut placeholder for "none selected", not a real LUT
	// name; excludeNoLut narrows that exclusion to plate@lut callers so it
	// cannot swallow a widget value or FX literally named "no LUT".
	void addUnique(std::vector<std::string>& out, XMLCSTR value, bool excludeNoLut = false)
	{
		if (!value) return;
		const std::string name(value);
		if (name.empty() || (excludeNoLut && name == "no LUT")) return;
		if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
	}

	// Depth-first, document order.
	template <typename Fn>
	void walk(XMLNode node, Fn fn)
	{
		fn(node);
		const int n = node.nChildNode();
		for (int i = 0; i < n; ++i)
		{
			walk(node.getChildNode(i), fn);
		}
	}

	bool nameIs(XMLNode node, const char* tag)
	{
		XMLCSTR name = node.getName();
		return name && std::strcmp(name, tag) == 0;
	}
}

bool gfcSessionPaths::listMedia(const std::string& jcsXml, std::vector<MediaRef>& out, std::string* err)
{
	out.clear();
	XMLNode top, root;
	if (!parseRoot(jcsXml, top, root, err)) return false;
	forEachMediaAttribute(root, [&out](XMLNode node, const char* attribute, Kind kind, int index, int sub)
	{
		XMLCSTR value = node.getAttribute(attribute);
		if (!value || !*value) return;
		MediaRef ref;
		ref.kind = kind;
		ref.index = index;
		ref.sub = sub;
		ref.path = value;
		out.push_back(ref);
	});
	return true;
}

bool gfcSessionPaths::listLutNames(const std::string& jcsXml, std::vector<std::string>& out, std::string* err)
{
	out.clear();
	XMLNode top, root;
	if (!parseRoot(jcsXml, top, root, err)) return false;
	walk(root, [&out](XMLNode node)
	{
		if (nameIs(node, "plate"))
		{
			addUnique(out, node.getAttribute("lut"), /*excludeNoLut=*/true);
		}
		else if (nameIs(node, "widget"))
		{
			XMLCSTR type = node.getAttribute("type");
			if (type && (std::strcmp(type, "cube") == 0 || std::strcmp(type, "lut") == 0))
			{
				addUnique(out, node.getAttribute("value"));
			}
		}
	});
	return true;
}

bool gfcSessionPaths::listFxNames(const std::string& jcsXml, std::vector<std::string>& out, std::string* err)
{
	out.clear();
	XMLNode top, root;
	if (!parseRoot(jcsXml, top, root, err)) return false;
	walk(root, [&out](XMLNode node)
	{
		if (nameIs(node, "FX")) addUnique(out, node.getAttribute("name"));
	});
	return true;
}

bool gfcSessionPaths::rewriteMedia(const std::string& jcsXml, const std::map<std::string, std::string>& mapping,
								   std::string& out, std::string* err)
{
	XMLNode top, root;
	if (!parseRoot(jcsXml, top, root, err)) return false;
	forEachMediaAttribute(root, [&mapping](XMLNode node, const char* attribute, Kind, int, int)
	{
		XMLCSTR value = node.getAttribute(attribute);
		if (!value) return;
		const auto it = mapping.find(value);
		if (it != mapping.end()) node.updateAttribute(it->second.c_str(), NULL, attribute);
	});
	int size = 0;
	XMLSTR text = top.createXMLString(1, &size);
	if (!text)
	{
		setErr(err, "cannot serialise the session XML");
		return false;
	}
	out.assign(text, static_cast<size_t>(size));
	free(text);   // this xmlParser allocates with malloc and has no freeXMLString
	return true;
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------
int sessionPathsSelfTest()
{
	using namespace gfcSessionPaths;
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
			std::fprintf(stderr, "NOTE-SESSIONPATHS FAIL: %s\n", msg);
		}
	};

	// The shape gfcSessionManager::saveSession writes, trimmed to what matters.
	const std::string xml = R"xml(<?xml version="1.0"?>
<root comment="fixture">
  <settings framingMode="1" from="1" to="10"/>
  <plates>
    <plate plateID="0" trackID="0" gamma="1.2" lut="show_look.cube">
      <stack><FXS><FX name="Grade" menuName="Grade" hash="h1" active="1">
        <widget varName="lut" type="cube" value="grade_a.cube" group="g"/>
        <widget varName="amount" type="float" value="0.5" group="g"/>
      </FX></FXS></stack>
    </plate>
    <plate plateID="1" trackID="1" gamma="1" lut="no LUT"/>
  </plates>
  <tracks>
    <track trackID="0" filename="/shots/a/sh010.0001.exr" from="1" to="10"/>
    <track trackID="1" filename="" from="1" to="1"/>
  </tracks>
  <playlist>
    <e>
      <t fn="/shots/b/sh020.0001.exr" fr="1" to="5"/>
      <t fn="/shots/a/sh010.0001.exr" fr="1" to="10"/>
      <FXS><FX name="Look" menuName="Look" hash="h2" active="1"><widget varName="l" type="lut" value="film.lut" group="g"/></FX><FX name="Grade" menuName="Grade" hash="h1" active="1"/></FXS>
    </e>
  </playlist>
</root>
)xml";

	std::string err;
	std::vector<MediaRef> refs;
	check(listMedia(xml, refs, &err), "media listed");
	check(refs.size() == 3, "empty track filenames are skipped");
	if (refs.size() == 3)
	{
		check(refs[0].kind == Kind::Track && refs[0].index == 0 && refs[0].sub == -1 &&
			  refs[0].path == "/shots/a/sh010.0001.exr", "the track path");
		check(refs[1].kind == Kind::Playlist && refs[1].index == 0 && refs[1].sub == 0 &&
			  refs[1].path == "/shots/b/sh020.0001.exr", "the first playlist track");
		check(refs[2].kind == Kind::Playlist && refs[2].index == 0 && refs[2].sub == 1 &&
			  refs[2].path == "/shots/a/sh010.0001.exr", "the second playlist track");
	}

	// A saved .jcs always starts with a UTF-8 BOM (gfcSessionManager writes
	// one); XMLNode::parseString, unlike parseFile, does not skip it on its
	// own, so parseRoot() must.
	std::vector<MediaRef> bomRefs;
	check(listMedia("\xEF\xBB\xBF" + xml, bomRefs, &err) && bomRefs.size() == 3 &&
		  bomRefs[0].kind == Kind::Track && bomRefs[0].index == 0 && bomRefs[0].sub == -1 &&
		  bomRefs[0].path == "/shots/a/sh010.0001.exr" &&
		  bomRefs[1].kind == Kind::Playlist && bomRefs[1].index == 0 && bomRefs[1].sub == 0 &&
		  bomRefs[1].path == "/shots/b/sh020.0001.exr" &&
		  bomRefs[2].kind == Kind::Playlist && bomRefs[2].index == 0 && bomRefs[2].sub == 1 &&
		  bomRefs[2].path == "/shots/a/sh010.0001.exr",
		  "a leading UTF-8 BOM does not break parsing -- the same 3 media refs are listed");

	std::vector<std::string> luts;
	check(listLutNames(xml, luts, &err) &&
		  luts == std::vector<std::string>{"show_look.cube", "grade_a.cube", "film.lut"},
		  "LUT names from plates and cube/lut widgets, unique, in document order");
	std::vector<std::string> fx;
	check(listFxNames(xml, fx, &err) && fx == std::vector<std::string>{"Grade", "Look"},
		  "FX names, unique, in document order");

	std::string rewritten;
	check(rewriteMedia(xml, {{"/shots/a/sh010.0001.exr", "media/000/sh010.0001.exr"}}, rewritten, &err),
		  "media rewritten");
	std::vector<MediaRef> after;
	check(listMedia(rewritten, after, &err) && after.size() == 3 &&
		  after[0].path == "media/000/sh010.0001.exr" && after[1].path == "/shots/b/sh020.0001.exr" &&
		  after[2].path == "media/000/sh010.0001.exr",
		  "mapped paths are replaced everywhere, others kept");
	check(rewritten.find("gamma=\"1.2\"") != std::string::npos && rewritten.find("to=\"10\"") != std::string::npos &&
		  rewritten.find("value=\"0.5\"") != std::string::npos && rewritten.find("comment=\"fixture\"") != std::string::npos,
		  "untouched attributes keep their values");
	std::vector<std::string> lutsAfter;
	check(listLutNames(rewritten, lutsAfter, &err) && lutsAfter == luts, "LUT names survive a rewrite");

	err.clear();
	check(!listMedia("<other/>", refs, &err) && !err.empty(), "a document without <root> is refused");

	// "no LUT" is only a plate-LUT placeholder: an FX (or a cube/lut widget)
	// literally named/valued "no LUT" is a real name, not "none selected".
	const std::string noLutXml = R"xml(<?xml version="1.0"?>
<root comment="fixture">
  <plates>
    <plate plateID="0" trackID="0" gamma="1" lut="no LUT">
      <stack><FXS><FX name="no LUT" menuName="X" hash="h1" active="1"/></FXS></stack>
    </plate>
  </plates>
</root>
)xml";
	std::vector<std::string> noLutFx;
	check(listFxNames(noLutXml, noLutFx, &err) && noLutFx == std::vector<std::string>{"no LUT"},
		  "an FX literally named \"no LUT\" is still listed");
	std::vector<std::string> noLutLuts;
	check(listLutNames(noLutXml, noLutLuts, &err) && noLutLuts.empty(),
		  "a plate with lut=\"no LUT\" still contributes nothing to the LUT list");

	std::printf("NOTE-SESSIONPATHS: pass=%d fail=%d\n", pass, fail);
	return fail;
}

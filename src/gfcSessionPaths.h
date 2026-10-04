#ifndef GFCSESSIONPATHS_H
#define GFCSESSIONPATHS_H

#include <map>
#include <string>
#include <vector>

/**
	@brief Reads and rewrites what a .jcs session references outside itself:
	media paths (tracks/track@filename, playlist/e/t@fn), LUT names (plate@lut,
	FX widgets of type cube or lut) and FX names (FX@name).
	See docs/superpowers/specs/2026-09-14-review-package-design.md.
*/
namespace gfcSessionPaths
{
	enum class Kind { Track, Playlist };

	struct MediaRef
	{
		Kind kind = Kind::Track;
		int index = 0;       // position among <track>s, or among playlist <e>s
		int sub = -1;        // position of the <t> inside its <e>; -1 for tracks
		std::string path;
	};

	/** Non-empty track filenames in document order, then playlist entry tracks. */
	bool listMedia(const std::string& jcsXml, std::vector<MediaRef>& out, std::string* err);

	/** Unique LUT names in document order; "" and "no LUT" are not names. */
	bool listLutNames(const std::string& jcsXml, std::vector<std::string>& out, std::string* err);

	/** Unique FX names in document order. */
	bool listFxNames(const std::string& jcsXml, std::vector<std::string>& out, std::string* err);

	/** The document with every media path that is a key of `mapping` replaced by
	    its value; every other attribute keeps its value. */
	bool rewriteMedia(const std::string& jcsXml, const std::map<std::string, std::string>& mapping,
					  std::string& out, std::string* err);
}

/** Prints NOTE-SESSIONPATHS: pass=N fail=N; returns the fail count. */
int sessionPathsSelfTest();

#endif

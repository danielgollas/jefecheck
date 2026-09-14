#ifndef GFCNOTESTORE_H
#define GFCNOTESTORE_H

#include <string>

class gfcReview;

/**
	@author Daniel Gollas Gilman <dgollas@ollin.com.mx>
	@brief Sidecar persistence for gfcReview / gfcRevision / gfcNote.

	Notes for a sequence live in `<sequence_dir>/<basename>.jnotes`, XML via
	the vendored xmlParser (see gfcsessionmanager.cpp:150-188 for the same
	pattern: XMLNode::createXMLTopNode / addChild / addAttribute /
	writeToFile).

	Render mounts are commonly read-only, so when the media directory can't
	be written, the sidecar falls back to
	`~/.config/jefecheck/notes/<sha1-of-path>.jnotes`. Writability is
	determined by attempting a real write and handling failure -- never by
	inspecting permission bits, because an NFS mount can lie about those.

	See docs/superpowers/specs/2026-09-07-annotations-design.md ("Storage").
*/
namespace gfcNoteStore
{
	/** Collapse a frame path to its sequence pattern: .0101.exr -> .####.exr.
	    Idempotent: a path that is already normalised is returned unchanged. */
	std::string normalisePath(const std::string& anyFramePath);

	/** Where notes for this (already-normalised) sequence path live. Prefers
	    the media directory; falls back to the per-user location when the
	    media directory is not writable. */
	std::string sidecarPathFor(const std::string& normalisedPath);

	/** Writes every revision/note in `review` to its sidecar (media directory
	    if writable, the fallback location otherwise). Returns false if
	    neither location could be written. */
	bool save(const gfcReview& review);

	/** Reads the sidecar for `normalisedPath` into `out`, checking the media
	    directory first and the fallback location second. Returns false if
	    neither exists or parses. `out` is left untouched on failure. */
	bool load(const std::string& normalisedPath, gfcReview& out);

	/**
	 * A review as JSON, for embedding in an EXR header (jefecheck:notes).
	 *
	 * JSON rather than the sidecar's XML, deliberately. The sidecar is ours
	 * and only we read it, so it matches the .jcs/.jpl the rest of the app
	 * already speaks. An embedded blob exists to be read by SOMEONE ELSE --
	 * and everyone else in this industry speaks JSON: OpenTimelineIO is JSON,
	 * the W3C Web Annotation model is JSON-LD, ShotGrid's API is JSON. A
	 * pipeline TD should be able to read this in one line of Python.
	 *
	 * Field names follow OTIO's vocabulary where they map, so an exporter
	 * later is a renaming rather than a migration.
	 *
	 * Hand-written rather than pulled from a JSON library: the schema is a
	 * dozen fields and the only JSON in the tree today is vendored inside
	 * libdatachannel, which is not a dependency worth inheriting for this.
	 */
	std::string toJsonString(const gfcReview& review);
}

/** Runs the sidecar store self-test (path normalisation, XML round-trip, and
    the read-only-directory fallback); prints NOTE-STORE: pass=N fail=N and
    returns non-zero if any check failed. Wired to --notes-test in a later
    task. */
int noteStoreSelfTest();

#endif

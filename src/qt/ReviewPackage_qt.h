// Review package (.jcreview): manifest, exporter and opener.
// docs/superpowers/specs/2026-09-14-review-package-design.md
// QtCore only — no glad, no managers (developer_notes.md §1). App-side actions
// the opener needs arrive as callbacks.
#ifndef JEFECHECK_QT_REVIEW_PACKAGE_H
#define JEFECHECK_QT_REVIEW_PACKAGE_H

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "../gfcTarArchive.h"

namespace jefe::qt::package {

inline constexpr const char* kFormat = "jefecheck-review-package";
inline constexpr int kVersion = 1;

struct ManifestMedia {
    int index = 0;
    std::string originalPath;           // normalised pattern when exported
    std::string packagedPath;           // "media/NNN/<pattern file name>"; empty when media is not included
    std::string fingerprint;            // "fp1:…"
    int width = 0;                      // first frame
    int height = 0;
    std::vector<std::string> frames;    // bare frame file names, in frame order
    std::string notes;                  // "notes/NNN.jnotes"
};

struct ManifestLut {
    std::string name;                   // the name sessions refer to
    std::string file;                   // "luts/<file name>"
};

struct Manifest {
    std::string created;                // ISO-8601 UTC
    std::string app;
    std::string session = "session.jcs";
    bool mediaIncluded = true;
    std::vector<ManifestMedia> media;
    std::vector<ManifestLut> luts;
};

QByteArray manifestToJson(const Manifest& manifest);
/** Refuses a foreign format, an unknown version (named in @a err) and unsafe entry names. */
bool manifestFromJson(const QByteArray& json, Manifest& out, QString* err);

/** "<root>/NNN", e.g. indexDir("media", 7) == "media/007". */
std::string indexDir(const char* root, int index);

/** One media as the caller gathered it. */
struct ExportMedia {
    std::string mediaPath;              // normalised pattern
    std::vector<std::string> frames;    // absolute frame paths, in frame order
    std::string notesXml;               // gfcNoteStore::toXmlString of its review
    std::string fingerprint;
    int width = 0;
    int height = 0;
};

struct ExportInput {
    std::string outPath;
    bool includeMedia = true;
    std::string sessionXml;             // the saved session; media paths still absolute
    std::vector<ExportMedia> media;
    std::vector<std::pair<std::string, std::string>> luts;   // LUT name -> source file (non-install LUTs only)
    std::string appVersion;
    std::string createdIso;
};

/**
 * Writes a package a little at a time so a dialog can stay responsive:
 * begin() plans every entry (manifest, session, notes, LUTs, media — in that
 * order) and opens <out>.partial; each step() writes one in-memory entry or one
 * 8 MiB chunk of a file; the step that writes the last entry finishes the
 * archive and renames it to <out>. A failure or cancel() removes the partial.
 */
class Exporter {
public:
    enum class State { Running, Done, Failed, Cancelled };

    bool begin(const ExportInput& input, QString* err);
    State step(QString* err);
    void cancel();
    State state() const { return state_; }
    qint64 bytesDone() const { return done_; }
    qint64 bytesTotal() const { return total_; }

private:
    struct Item {
        std::string entryName;
        std::string bytes;              // in-memory entries
        std::string sourcePath;         // file entries
        uint64_t size = 0;
    };

    void failWith(const std::string& message, QString* err);

    std::vector<Item> items_;
    size_t next_ = 0;
    uint64_t offsetInItem_ = 0;
    bool entryOpen_ = false;
    gfcTar::Writer writer_;
    std::string outPath_;
    std::string partialPath_;
    qint64 done_ = 0;
    qint64 total_ = 0;
    State state_ = State::Failed;
};

/** What opening a package needs from the running app. */
struct OpenServices {
    /** Loads a packaged LUT file; called for every LUT before the session. */
    std::function<bool(const std::string& lutPath)> loadLut;
    /** Called after a media's notes are merged on disk, so in-memory copies reload. */
    std::function<void(const std::string& mediaPath)> reloadReview;
    /** Preferences -> Search Paths, used whether or not "use search paths" is ticked. */
    std::vector<std::string> searchPaths;
    bool searchRecursive = false;
    /** When false, nothing prompts: unresolved media count as missing. */
    bool interactive = false;
    /** Returns a chosen frame file of the media, or "" to skip it. */
    std::function<std::string(const ManifestMedia& media)> locate;
    /** Asked when the located media's fingerprint differs; true uses it anyway. */
    std::function<bool(const ManifestMedia& media, const std::string& chosenFrame)> confirmMismatch;
};

struct OpenResult {
    Manifest manifest;
    std::string extractDir;
    std::string sessionPath;                  // the rewritten session, ready to load
    int resolved = 0;
    int missing = 0;
    std::vector<std::string> missingMedia;    // display names
    std::vector<std::string> fxNames;         // every FX the session uses
    std::vector<std::string> notesProblems;   // "<originalPath>: <reason>"; local notes untouched
    std::vector<std::string> lutsNotLoaded;   // packaged LUT paths services.loadLut failed on
};

/**
 * The frames of the first sequence under @a roots whose fingerprint equals the
 * media's. Candidates must match the frame count and first-frame size before
 * any fingerprint is computed; candidates with the media's file-name pattern
 * are tried first. Empty when nothing matches.
 */
std::vector<std::string> findByFingerprint(const ManifestMedia& media, const std::vector<std::string>& roots,
                                           bool recursive);

/**
 * Validates the manifest, extracts into <cacheRoot>/<id> (id = SHA-1 of the
 * package's absolute path, size, modification time and manifest bytes;
 * reused once it holds a .complete marker AND every file the manifest names
 * -- a partial or damaged cache is discarded and re-extracted), loads
 * packaged LUTs (failures recorded in lutsNotLoaded), resolves every media
 * (packaged -> original path -> fingerprint search -> services.locate),
 * union-merges each media's notes into the sidecar beside the resolved
 * media, and writes the session with media paths rewritten to the resolved
 * frames. Notes are placed, and the session rewritten, only after the
 * session has been read and parsed; a session that fails to parse or to
 * rewrite loads no LUTs and changes no notes. A sidecar that exists but
 * fails to load is left untouched, and a save failure leaves it untouched
 * too -- both are recorded in notesProblems as "<originalPath>: <reason>"
 * without failing the open. Media that stays unresolved keeps its original
 * path in the session (a reference to the packaged copy is mapped back to
 * alongside the original path) and is counted as missing. Returns false
 * (loading nothing) for an unreadable, truncated, foreign or unknown-version
 * package.
 */
bool openPackage(const std::string& packagePath, const std::string& cacheRoot, const OpenServices& services,
                 OpenResult& result, QString* err);

/** Manifest and exporter self-test; prints NOTE-PACKAGE: pass=N fail=N. */
int packageSelfTest();

/** Opener self-test; prints NOTE-PACKAGE-OPEN: pass=N fail=N. */
int packageOpenSelfTest();

}  // namespace jefe::qt::package

#endif

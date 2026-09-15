// Review package (.jcreview): manifest, exporter and opener.
// docs/superpowers/specs/2026-09-14-review-package-design.md
// QtCore only — no glad, no managers (developer_notes.md §1). App-side actions
// the opener needs arrive as callbacks.
#ifndef JEFECHECK_QT_REVIEW_PACKAGE_H
#define JEFECHECK_QT_REVIEW_PACKAGE_H

#include <QByteArray>
#include <QString>

#include <cstdint>
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

/** Manifest and exporter self-test; prints NOTE-PACKAGE: pass=N fail=N. */
int packageSelfTest();

}  // namespace jefe::qt::package

#endif

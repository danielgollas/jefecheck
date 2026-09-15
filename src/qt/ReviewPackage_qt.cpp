#include "ReviewPackage_qt.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <map>
#include <memory>
#include <system_error>

#include "../gfcMediaFingerprint.h"
#include "../gfcNoteMerge.h"
#include "../gfcNoteStore.h"
#include "../gfcSessionPaths.h"
#include "../gfcSha1.h"
#include "../gfcnotestroke.h"
#include "../gfcreview.h"
#include "../gfcrevision.h"

namespace jefe::qt::package {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
std::string ss(const QJsonValue& v) { return v.toString().toStdString(); }

std::string lowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return s;
}

// Test-only counters, read by packageOpenSelfTest: how many search roots were
// walked and how many candidate sequences had their first frame probed.
int searchRootWalks = 0;
int candidateProbes = 0;
}  // namespace

std::string indexDir(const char* root, int index) {
    char digits[16];
    std::snprintf(digits, sizeof(digits), "%03d", index);
    return std::string(root) + "/" + digits;
}

QByteArray manifestToJson(const Manifest& manifest) {
    QJsonObject root;
    root["format"] = QLatin1String(kFormat);
    root["version"] = kVersion;
    root["created"] = qs(manifest.created);
    root["app"] = qs(manifest.app);
    root["session"] = qs(manifest.session);
    root["mediaIncluded"] = manifest.mediaIncluded;
    QJsonArray media;
    for (const ManifestMedia& mm : manifest.media) {
        QJsonObject o;
        o["index"] = mm.index;
        o["originalPath"] = qs(mm.originalPath);
        o["packagedPath"] = qs(mm.packagedPath);
        o["fingerprint"] = qs(mm.fingerprint);
        o["width"] = mm.width;
        o["height"] = mm.height;
        QJsonArray frames;
        for (const std::string& f : mm.frames) frames.append(qs(f));
        o["frames"] = frames;
        o["notes"] = qs(mm.notes);
        media.append(o);
    }
    root["media"] = media;
    QJsonArray luts;
    for (const ManifestLut& lut : manifest.luts) {
        QJsonObject o;
        o["name"] = qs(lut.name);
        o["file"] = qs(lut.file);
        luts.append(o);
    }
    root["luts"] = luts;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool manifestFromJson(const QByteArray& json, Manifest& out, QString* err) {
    auto fail = [err](const QString& message) {
        if (err) *err = message;
        return false;
    };
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return fail(QStringLiteral("manifest.json does not parse: %1").arg(parseError.errorString()));
    }
    const QJsonObject root = doc.object();
    if (root.value("format").toString() != QLatin1String(kFormat)) {
        return fail(QStringLiteral("Not a JefeCheck review package"));
    }
    const int version = root.value("version").toInt(-1);
    if (version != kVersion) {
        return fail(QStringLiteral("Unsupported review package version %1").arg(version));
    }

    Manifest m;
    m.created = ss(root.value("created"));
    m.app = ss(root.value("app"));
    m.session = root.value("session").toString(QStringLiteral("session.jcs")).toStdString();
    m.mediaIncluded = root.value("mediaIncluded").toBool(false);
    if (!gfcTar::isSafeName(m.session)) return fail(QStringLiteral("Unsafe session name in the manifest"));
    const QJsonArray mediaArray = root.value("media").toArray();
    std::vector<bool> seenIndex(size_t(mediaArray.size()), false);
    for (const QJsonValue& value : mediaArray) {
        const QJsonObject o = value.toObject();
        ManifestMedia mm;
        mm.index = o.value("index").toInt();
        if (mm.index < 0 || mm.index >= mediaArray.size() || seenIndex[size_t(mm.index)]) {
            return fail(QStringLiteral("Invalid media index %1 in the manifest").arg(mm.index));
        }
        seenIndex[size_t(mm.index)] = true;
        mm.originalPath = ss(o.value("originalPath"));
        mm.packagedPath = ss(o.value("packagedPath"));
        mm.fingerprint = ss(o.value("fingerprint"));
        mm.width = o.value("width").toInt();
        mm.height = o.value("height").toInt();
        mm.notes = ss(o.value("notes"));
        for (const QJsonValue& f : o.value("frames").toArray()) mm.frames.push_back(ss(f));
        if (!gfcTar::isSafeName(mm.notes) || (!mm.packagedPath.empty() && !gfcTar::isSafeName(mm.packagedPath))) {
            return fail(QStringLiteral("Unsafe entry name in the manifest"));
        }
        for (const std::string& f : mm.frames) {
            if (f.find('/') != std::string::npos || !gfcTar::isSafeName(f)) {
                return fail(QStringLiteral("Unsafe frame name in the manifest"));
            }
        }
        m.media.push_back(std::move(mm));
    }
    for (const QJsonValue& value : root.value("luts").toArray()) {
        const QJsonObject o = value.toObject();
        ManifestLut lut{ss(o.value("name")), ss(o.value("file"))};
        if (!gfcTar::isSafeName(lut.file)) return fail(QStringLiteral("Unsafe LUT name in the manifest"));
        m.luts.push_back(std::move(lut));
    }
    out = std::move(m);
    return true;
}

bool Exporter::begin(const ExportInput& input, QString* err) {
    namespace fs = std::filesystem;
    auto fail = [this, err](const std::string& message) {
        if (err) *err = QString::fromStdString(message);
        state_ = State::Failed;
        return false;
    };
    items_.clear();
    next_ = 0;
    offsetInItem_ = 0;
    entryOpen_ = false;
    done_ = 0;
    total_ = 0;
    outPath_ = input.outPath;
    partialPath_ = input.outPath + ".partial";

    // The finished package replaces whatever is at the output path, so that
    // must not be one of its own sources. Compared canonically where the
    // files exist, lexically (after resolving what does exist) otherwise.
    auto sameFile = [](const std::string& a, const std::string& b) {
        std::error_code ea, eb;
        if (fs::exists(a, ea) && fs::exists(b, eb)) {
            std::error_code e;
            return fs::equivalent(a, b, e) && !e;
        }
        const fs::path ca = fs::weakly_canonical(a, ea);
        const fs::path cb = fs::weakly_canonical(b, eb);
        return !ea && !eb && ca == cb;
    };
    for (const ExportMedia& m : input.media) {
        for (const std::string& frame : m.frames) {
            if (sameFile(frame, input.outPath)) return fail("The package would overwrite its own source " + frame);
        }
    }
    for (const auto& lutSource : input.luts) {
        if (sameFile(lutSource.second, input.outPath)) {
            return fail("The package would overwrite its own source " + lutSource.second);
        }
    }

    Manifest manifest;
    manifest.created = input.createdIso;
    manifest.app = input.appVersion;
    manifest.mediaIncluded = input.includeMedia;

    std::vector<Item> notes, luts, media;
    for (size_t i = 0; i < input.media.size(); ++i) {
        const ExportMedia& m = input.media[i];
        ManifestMedia mm;
        mm.index = int(i);
        mm.originalPath = m.mediaPath;
        mm.fingerprint = m.fingerprint;
        mm.width = m.width;
        mm.height = m.height;
        mm.notes = indexDir("notes", int(i)) + ".jnotes";
        for (const std::string& frame : m.frames) {
            const std::string name = fs::path(frame).filename().string();
            mm.frames.push_back(name);
            if (!input.includeMedia) continue;
            std::error_code ec;
            const uint64_t size = fs::file_size(frame, ec);
            if (ec) return fail("Cannot read " + frame);
            media.push_back(Item{indexDir("media", int(i)) + "/" + name, std::string(), frame, size});
        }
        if (input.includeMedia) {
            mm.packagedPath = indexDir("media", int(i)) + "/" + fs::path(m.mediaPath).filename().string();
        }
        notes.push_back(Item{mm.notes, m.notesXml, std::string(), m.notesXml.size()});
        manifest.media.push_back(std::move(mm));
    }
    std::map<std::string, std::string> lutEntryToSource;
    for (const auto& [name, source] : input.luts) {
        std::error_code ec;
        const uint64_t size = fs::file_size(source, ec);
        if (ec) return fail("Cannot read " + source);
        const std::string entry = "luts/" + fs::path(source).filename().string();
        const auto existing = lutEntryToSource.find(entry);
        if (existing != lutEntryToSource.end()) {
            return fail("Two LUT files are both named " + fs::path(source).filename().string() + ": " +
                         existing->second + " and " + source);
        }
        lutEntryToSource[entry] = source;
        luts.push_back(Item{entry, std::string(), source, size});
        manifest.luts.push_back(ManifestLut{name, entry});
    }

    std::string session = input.sessionXml;
    if (input.includeMedia) {
        std::vector<gfcSessionPaths::MediaRef> refs;
        std::string perr;
        if (!gfcSessionPaths::listMedia(input.sessionXml, refs, &perr)) return fail(perr);
        std::map<std::string, std::string> mapping;
        for (const gfcSessionPaths::MediaRef& ref : refs) {
            const std::string key = gfcNoteStore::normalisePath(ref.path);
            for (size_t i = 0; i < input.media.size(); ++i) {
                if (input.media[i].mediaPath == key) {
                    mapping[ref.path] = indexDir("media", int(i)) + "/" + fs::path(ref.path).filename().string();
                }
            }
        }
        if (!gfcSessionPaths::rewriteMedia(input.sessionXml, mapping, session, &perr)) return fail(perr);
    }

    const std::string manifestJson = manifestToJson(manifest).toStdString();
    items_.push_back(Item{"manifest.json", manifestJson, std::string(), manifestJson.size()});
    items_.push_back(Item{manifest.session, session, std::string(), session.size()});
    for (std::vector<Item>* group : {&notes, &luts, &media}) {
        for (Item& item : *group) items_.push_back(std::move(item));
    }
    for (const Item& item : items_) total_ += qint64(item.size);

    std::string werr;
    if (!writer_.open(partialPath_, &werr)) return fail(werr);
    state_ = State::Running;
    return true;
}

Exporter::State Exporter::step(QString* err) {
    namespace fs = std::filesystem;
    if (state_ != State::Running) return state_;
    std::string e;

    if (next_ >= items_.size()) {
        if (!writer_.finish(&e)) {
            failWith(e, err);
            return state_;
        }
        std::error_code ec;
#ifdef _WIN32
        // POSIX rename replaces the destination atomically; Windows' does not.
        fs::remove(outPath_, ec);
#endif
        fs::rename(partialPath_, outPath_, ec);
        if (ec) {
            failWith("Cannot rename " + partialPath_ + ": " + ec.message(), err);
            return state_;
        }
        state_ = State::Done;
        return state_;
    }

    Item& item = items_[next_];
    if (!entryOpen_) {
        if (!writer_.beginEntry(item.entryName, item.size, &e)) {
            failWith(e, err);
            return state_;
        }
        entryOpen_ = true;
        offsetInItem_ = 0;
    }
    if (item.sourcePath.empty()) {
        if (!writer_.write(item.bytes.data(), item.bytes.size(), &e)) {
            failWith(e, err);
            return state_;
        }
        offsetInItem_ = item.size;
        done_ += qint64(item.size);
    } else {
        constexpr uint64_t kChunk = 8 * 1024 * 1024;
        const size_t n = size_t(std::min<uint64_t>(kChunk, item.size - offsetInItem_));
        std::string chunk(n, '\0');
        std::ifstream in(item.sourcePath, std::ios::binary);
        in.seekg(std::streamoff(offsetInItem_));
        if (n > 0) in.read(&chunk[0], std::streamsize(n));
        if (!in) {
            failWith("Cannot read " + item.sourcePath, err);
            return state_;
        }
        if (!writer_.write(chunk.data(), n, &e)) {
            failWith(e, err);
            return state_;
        }
        offsetInItem_ += n;
        done_ += qint64(n);
    }
    if (offsetInItem_ >= item.size) {
        if (!writer_.endEntry(&e)) {
            failWith(e, err);
            return state_;
        }
        entryOpen_ = false;
        ++next_;
    }
    return state_;
}

void Exporter::cancel() {
    if (state_ != State::Running) return;
    writer_.abandon();
    std::error_code ec;
    std::filesystem::remove(partialPath_, ec);
    state_ = State::Cancelled;
}

void Exporter::failWith(const std::string& message, QString* err) {
    writer_.abandon();
    std::error_code ec;
    std::filesystem::remove(partialPath_, ec);
    state_ = State::Failed;
    if (err) *err = QString::fromStdString(message);
}

namespace {
std::string readText(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void writeText(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

Exporter::State runToEnd(Exporter& exporter, QString* err) {
    Exporter::State state = Exporter::State::Running;
    while ((state = exporter.step(err)) == Exporter::State::Running) {}
    return state;
}

constexpr uint64_t kMaxEntryBytes = 64ull * 1024 * 1024;

// Reads `path`, refusing anything past kMaxEntryBytes. `label` names the
// entry in the failure message.
bool readCapped(const std::filesystem::path& path, const std::string& label, std::string& out, std::string* err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const uint64_t size = fs::file_size(path, ec);
    if (ec) {
        if (err) *err = "Cannot read " + label;
        return false;
    }
    if (size > kMaxEntryBytes) {
        if (err) *err = label + " is larger than 64 MiB";
        return false;
    }
    out = readText(path.string());
    return true;
}

// True once every file the manifest names -- the session, each media's
// notes entry, each LUT, and (when media is included) every packaged frame
// -- exists under `dir`.
bool manifestFilesPresent(const Manifest& manifest, const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir / manifest.session, ec)) return false;
    for (const ManifestMedia& media : manifest.media) {
        if (!fs::exists(dir / media.notes, ec)) return false;
        if (manifest.mediaIncluded && !media.packagedPath.empty()) {
            for (const std::string& f : media.frames) {
                if (!fs::exists(dir / indexDir("media", media.index) / f, ec)) return false;
            }
        }
    }
    for (const ManifestLut& lut : manifest.luts) {
        if (!fs::exists(dir / lut.file, ec)) return false;
    }
    return true;
}

// True when `candidate`, lexically normalised, lies strictly inside `dir`.
// Compares whole path components, so "/cache/abc" does not contain
// "/cache/abcd/x".
bool isInsideDir(const std::filesystem::path& dir, const std::filesystem::path& candidate) {
    const std::filesystem::path base = dir.lexically_normal();
    const std::filesystem::path full = candidate.lexically_normal();
    auto b = base.begin();
    auto f = full.begin();
    for (; b != base.end(); ++b, ++f) {
        if (b->empty() && std::next(b) == base.end()) break;   // a trailing separator's empty element
        if (f == full.end() || *f != *b) return false;
    }
    return f != full.end();
}

// Extracts `reader` into `dir`, unless a prior extraction there is already
// complete (a `.complete` marker AND every manifest-named file present). A
// partial (crash before `.complete`) or damaged (a manifest-named file went
// missing) cache is repaired by writing the archive's entries back over the
// directory -- gfcTar::Reader::extractTo truncates an existing file and
// creates a missing one -- rather than discarding the directory first, so
// anything that isn't part of the package (most notably a reviewer's own
// .jnotes sidecar beside the extracted media) survives the repair.
bool ensureExtracted(const gfcTar::Reader& reader, const Manifest& manifest, const std::filesystem::path& dir,
                     std::string* err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(dir / ".complete", ec) && manifestFilesPresent(manifest, dir)) return true;

    // A marker left from an earlier extraction must not outlive a repair that
    // stops part-way (a crash, a failed entry): drop it before rewriting.
    fs::remove(dir / ".complete", ec);
    if (ec) {
        if (err) *err = "Cannot write to " + dir.string();
        return false;
    }
    for (const gfcTar::Entry& entry : reader.entries()) {
        // Reader::open has already refused unsafe names (gfcTar::isSafeName);
        // this is a second guard against anything that still resolves outside.
        const fs::path dest = dir / entry.name;
        if (!isInsideDir(dir, dest)) {
            if (err) *err = "Cannot extract the package: unsafe entry name: " + entry.name;
            return false;
        }
        std::string entryErr;
        if (!reader.extractTo(entry, dest.string(), &entryErr)) {
            if (err) *err = "Cannot extract the package: " + entryErr;
            return false;
        }
    }
    // An archive that lacks a file its manifest names is usable (that media
    // just resolves elsewhere or counts as missing) but never marked complete.
    if (!manifestFilesPresent(manifest, dir)) return true;
    std::ofstream marker((dir / ".complete").string());
    marker << "ok\n";
    marker.close();
    if (marker.fail()) {
        if (err) *err = "Cannot write to " + dir.string();
        return false;
    }
    return true;
}

// Frames of the media, in order: packaged (only when every packaged frame is
// present) -> original path (when absolute) -> fingerprint search ->
// interactive locate. Empty when unresolved.
std::vector<std::string> resolveMedia(const ManifestMedia& media, bool mediaIncluded,
                                      const std::filesystem::path& extractDir, const OpenServices& services,
                                      SequenceSearch& search) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (media.frames.empty()) return {};

    auto framesAt = [&](const fs::path& base) {
        std::vector<std::string> out;
        bool allThere = true;
        for (const std::string& f : media.frames) {
            out.push_back((base / f).string());
            if (!fs::exists(out.back(), ec)) allThere = false;
        }
        return allThere ? out : std::vector<std::string>{};
    };

    if (mediaIncluded && !media.packagedPath.empty()) {
        std::vector<std::string> frames = framesAt(extractDir / indexDir("media", media.index));
        if (!frames.empty()) return frames;
    }

    const fs::path originalPath(media.originalPath);
    if (originalPath.is_absolute()) {
        std::vector<std::string> frames = framesAt(originalPath.parent_path());
        if (!frames.empty()) return frames;
    }

    std::vector<std::string> frames = findByFingerprint(media, search);
    if (!frames.empty() || !services.interactive || !services.locate) return frames;

    const std::string chosen = services.locate(media);
    if (chosen.empty()) return {};
    auto sequences = gfcMediaFingerprint::sequencesIn(fs::path(chosen).parent_path().string(), false);
    const auto found = sequences.find(gfcNoteStore::normalisePath(chosen));
    if (found == sequences.end()) return {};
    if (gfcMediaFingerprint::compute(found->second, nullptr) == media.fingerprint ||
        (services.confirmMismatch && services.confirmMismatch(media, chosen))) {
        return found->second;
    }
    return {};
}

// Loads any existing sidecar for `resolvedMedia`, merges `notesXml` (the
// package's own notes entry) into it and saves the result when anything
// actually changed. `notesXml` being unreadable or failing to parse is
// reported as a problem too -- a well-formed package always carries a
// parseable (if empty) notes document for every media, so a parse failure
// here is not "nothing to merge", it is the packaged notes themselves being
// broken.
//
// A PRIMARY sidecar (gfcNoteStore::sidecarPathFor) that exists must load on
// its own: gfcNoteStore::load() would otherwise fall through to a stale
// fallback-location sidecar when the primary fails to parse, silently
// merging into the wrong file and masking the corruption. gfcNoteStore
// exposes no way to load one specific location, so the primary's bytes are
// read and parsed directly (gfcNoteStore::fromXmlString), the same parse
// tryLoad() uses internally.
//
// Returns true when the packaged notes were placed -- `*changed` says
// whether that meant an actual save (false when the merge added no
// revision and no note to an existing sidecar; a sidecar that doesn't yet
// exist is always created, even from empty packaged notes). Returns false
// and sets `problem`, leaving the local sidecar untouched, when the
// packaged notes don't parse, a local sidecar exists but doesn't load, or
// the save fails.
bool mergeNotes(const std::string& resolvedMedia, const std::string& notesXml, const std::string& fallbackFingerprint,
                std::string* problem, bool* changed) {
    if (changed) *changed = false;
    gfcReview incoming;
    if (!gfcNoteStore::fromXmlString(notesXml, incoming)) {
        if (problem) *problem = "package notes unreadable, not merged";
        return false;
    }

    const std::string sidecarPath = gfcNoteStore::sidecarPathFor(resolvedMedia);
    std::error_code ec;
    const bool sidecarExists = std::filesystem::exists(sidecarPath, ec);
    gfcReview local;
    local.mediaPath = resolvedMedia;
    if (sidecarExists) {
        // gfcNoteStore::save() (XMLNode::writeToFile) writes a leading
        // UTF-8 BOM; fromXmlString's parseString, unlike load()'s
        // parseFile, does not strip one on its own.
        std::string primaryBytes = readText(sidecarPath);
        if (primaryBytes.compare(0, 3, "\xEF\xBB\xBF") == 0) primaryBytes.erase(0, 3);
        if (!gfcNoteStore::fromXmlString(primaryBytes, local)) {
            if (problem) *problem = "local notes unreadable, package notes not merged";
            return false;
        }
    } else {
        gfcNoteStore::load(resolvedMedia, local);   // may still find a fallback-location sidecar
    }
    local.mediaPath = resolvedMedia;   // a loaded sidecar may name an older path
    const gfcNoteMerge::Result mergeResult = gfcNoteMerge::mergeInto(local, std::move(incoming));
    if (local.fingerprint.empty()) local.fingerprint = fallbackFingerprint;
    if (sidecarExists && mergeResult.revisionsAdded == 0 && mergeResult.notesAdded == 0) {
        return true;   // nothing changed; leave the sidecar and its modification time alone
    }
    if (!gfcNoteStore::save(local)) {
        if (problem) *problem = "notes could not be saved";
        return false;
    }
    if (changed) *changed = true;
    return true;
}
}  // namespace

SequenceSearch::SequenceSearch(std::vector<std::string> roots, bool recursive)
    : roots_(std::move(roots)), recursive_(recursive), indexes_(roots_.size()) {}

const SequenceSearch::Index& SequenceSearch::root(size_t i) {
    if (!indexes_[i]) {
        ++searchRootWalks;
        indexes_[i] = gfcMediaFingerprint::sequencesIn(roots_[i], recursive_);
    }
    return *indexes_[i];
}

std::vector<std::string> findByFingerprint(const ManifestMedia& media, const std::vector<std::string>& roots,
                                           bool recursive) {
    SequenceSearch search(roots, recursive);
    return findByFingerprint(media, search);
}

std::vector<std::string> findByFingerprint(const ManifestMedia& media, SequenceSearch& search) {
    namespace fs = std::filesystem;
    if (media.fingerprint.empty() || media.frames.empty()) return {};
    const std::string wantedName = fs::path(media.originalPath).filename().string();
    const std::string wantedExtension = lowerAscii(fs::path(media.originalPath).extension().string());
    std::vector<std::vector<std::string>> sameName;
    std::vector<std::vector<std::string>> others;
    for (size_t r = 0; r < search.rootCount(); ++r) {
        for (const auto& [key, frames] : search.root(r)) {
            if (frames.size() != media.frames.size()) continue;
            // Every file in the tree is a sequence candidate; probing opens it,
            // so anything that isn't even the same kind of file is skipped first.
            if (lowerAscii(fs::path(frames.front()).extension().string()) != wantedExtension) continue;
            ++candidateProbes;
            gfcMediaFingerprint::Probe probe;
            if (!gfcMediaFingerprint::probe(frames.front(), probe, nullptr)) continue;
            if (probe.width != media.width || probe.height != media.height) continue;
            (fs::path(key).filename().string() == wantedName ? sameName : others).push_back(frames);
        }
    }
    for (const auto* group : {&sameName, &others}) {
        for (const std::vector<std::string>& frames : *group) {
            if (gfcMediaFingerprint::compute(frames, nullptr) == media.fingerprint) return frames;
        }
    }
    return {};
}

bool openPackage(const std::string& packagePath, const std::string& cacheRoot, const OpenServices& services,
                 OpenResult& result, QString* err) {
    namespace fs = std::filesystem;
    auto fail = [err](const std::string& message) {
        if (err) *err = QString::fromStdString(message);
        return false;
    };
    result = OpenResult{};

    gfcTar::Reader reader;
    std::string e;
    if (!reader.open(packagePath, &e)) return fail("Cannot read the package: " + e);
    if (reader.entries().empty() || reader.entries()[0].name != "manifest.json") {
        return fail("Not a JefeCheck review package: manifest.json is missing");
    }
    const gfcTar::Entry& manifestEntry = reader.entries()[0];
    if (manifestEntry.size > kMaxEntryBytes) return fail("manifest.json is larger than 64 MiB");
    std::string manifestBytes;
    if (!reader.readBytes(manifestEntry, manifestBytes, &e)) return fail("Cannot read the package: " + e);
    if (!manifestFromJson(QByteArray::fromStdString(manifestBytes), result.manifest, err)) return false;
    const Manifest& manifest = result.manifest;

    std::error_code ec;
    const std::string absolute = fs::absolute(packagePath, ec).string();
    const uint64_t size = fs::file_size(packagePath, ec);
    const long long mtime = static_cast<long long>(fs::last_write_time(packagePath, ec).time_since_epoch().count());
    const fs::path dir = fs::path(cacheRoot) /
        gfcSha1::hex(absolute + "|" + std::to_string(size) + "|" + std::to_string(mtime) + "|" + manifestBytes);
    result.extractDir = dir.string();
    if (!ensureExtracted(reader, manifest, dir, &e)) return fail(e);

    // The session must exist, be a sane size, and actually parse before
    // anything else happens: a package this broken loads no LUTs and
    // changes no notes.
    std::string sessionXml, capErr;
    if (!readCapped(dir / manifest.session, manifest.session, sessionXml, &capErr)) return fail(capErr);
    std::vector<gfcSessionPaths::MediaRef> refs;
    if (!gfcSessionPaths::listMedia(sessionXml, refs, &e)) return fail("The package's session is unreadable: " + e);

    struct PendingNotes {
        std::string resolvedMedia;
        std::string notesXml;
        std::string originalPath;
        std::string fingerprint;
    };
    std::vector<PendingNotes> pending;
    std::map<std::string, std::string> mapping;
    // Shared by every media of this open: a root is walked only when a media
    // first reaches the fingerprint search, and then reused.
    SequenceSearch search(services.searchPaths, services.searchRecursive);
    for (const ManifestMedia& media : manifest.media) {
        const std::vector<std::string> frames = resolveMedia(media, manifest.mediaIncluded, dir, services, search);
        const std::string packagedPrefix = indexDir("media", media.index) + "/";
        if (frames.empty()) {
            ++result.missing;
            result.missingMedia.push_back(fs::path(media.originalPath).filename().string());
            // A ref that pointed at the packaged copy (which turned out to be
            // unusable) is mapped back to alongside the original path; a ref
            // that already named the original path is left untouched by
            // having no mapping entry.
            const fs::path originalDir = fs::path(media.originalPath).parent_path();
            for (const gfcSessionPaths::MediaRef& ref : refs) {
                if (ref.path.rfind(packagedPrefix, 0) != 0) continue;
                mapping[ref.path] = (originalDir / fs::path(ref.path).filename()).string();
            }
            continue;
        }
        ++result.resolved;

        // Every session reference to this media points at the same frame of the
        // resolved sequence (matched by position, since a relinked sequence may
        // be named differently).
        for (const gfcSessionPaths::MediaRef& ref : refs) {
            const bool ours = ref.path.rfind(packagedPrefix, 0) == 0 ||
                              gfcNoteStore::normalisePath(ref.path) == media.originalPath;
            if (!ours) continue;
            const std::string name = fs::path(ref.path).filename().string();
            const auto at = std::find(media.frames.begin(), media.frames.end(), name);
            const size_t index = (at == media.frames.end()) ? 0 : size_t(at - media.frames.begin());
            mapping[ref.path] = frames[std::min(index, frames.size() - 1)];
        }

        // Notes are only read here; nothing is merged or saved to disk until
        // the session rewrite below has succeeded and been verified. An
        // unreadable notes entry is left as an empty string, which
        // mergeNotes below reports as a problem rather than skipping
        // silently -- a well-formed package always carries a parseable (if
        // empty) notes document for every media.
        std::string notesXml;
        if (!readCapped(dir / media.notes, media.notes, notesXml, nullptr)) notesXml.clear();
        pending.push_back(PendingNotes{gfcNoteStore::normalisePath(frames.front()), notesXml, media.originalPath,
                                       media.fingerprint});
    }

    std::string rewritten;
    if (!gfcSessionPaths::rewriteMedia(sessionXml, mapping, rewritten, &e)) return fail(e);
    const fs::path sessionOut = dir / "session.opened.jcs";
    writeText(sessionOut.string(), rewritten);
    if (readText(sessionOut.string()) != rewritten) return fail("Cannot write " + sessionOut.string());
    result.sessionPath = sessionOut.string();
    gfcSessionPaths::listFxNames(rewritten, result.fxNames, nullptr);

    // LUTs and notes are only touched once the rewritten session is
    // confirmed on disk: a rewrite or write failure loads nothing and
    // changes nothing.
    for (const ManifestLut& lut : manifest.luts) {
        const std::string lutPath = (dir / lut.file).string();
        if (!services.loadLut || !services.loadLut(lutPath)) result.lutsNotLoaded.push_back(lutPath);
    }

    for (PendingNotes& p : pending) {
        std::string problem;
        bool changed = false;
        if (!mergeNotes(p.resolvedMedia, p.notesXml, p.fingerprint, &problem, &changed)) {
            result.notesProblems.push_back(p.originalPath + ": " + problem);
            continue;
        }
        if (changed && services.reloadReview) services.reloadReview(p.resolvedMedia);
    }

    return true;
}

int packageSelfTest() {
    namespace fs = std::filesystem;
    int pass = 0;
    int fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) {
            ++pass;
        } else {
            ++fail;
            std::fprintf(stderr, "NOTE-PACKAGE FAIL: %s\n", msg);
        }
    };

    // --- manifest ---
    Manifest m;
    m.created = "2026-09-14T20:10:00Z";
    m.app = "1.7.0";
    ManifestMedia mm;
    mm.index = 0;
    mm.originalPath = "/shots/sh010.####.exr";
    mm.packagedPath = "media/000/sh010.####.exr";
    mm.fingerprint = "fp1:abc";
    mm.width = 1920;
    mm.height = 1080;
    mm.frames = {"sh010.0001.exr", "sh010.0002.exr"};
    mm.notes = "notes/000.jnotes";
    m.media.push_back(mm);
    m.luts.push_back(ManifestLut{"look.cube", "luts/look.cube"});

    Manifest back;
    QString err;
    check(manifestFromJson(manifestToJson(m), back, &err), "a manifest round trip parses");
    check(back.created == m.created && back.app == m.app && back.session == "session.jcs" && back.mediaIncluded &&
          back.media.size() == 1 && back.media[0].frames == mm.frames && back.media[0].width == 1920 &&
          back.media[0].height == 1080 && back.media[0].packagedPath == mm.packagedPath &&
          back.media[0].fingerprint == "fp1:abc" && back.media[0].notes == mm.notes &&
          back.luts.size() == 1 && back.luts[0].name == "look.cube" && back.luts[0].file == "luts/look.cube",
          "every manifest field survives");
    auto withField = [&m](const char* key, const QJsonValue& value) {
        QJsonObject o = QJsonDocument::fromJson(manifestToJson(m)).object();
        o[key] = value;
        return QJsonDocument(o).toJson();
    };
    check(!manifestFromJson(withField("format", "something-else"), back, &err), "a foreign format is refused");
    err.clear();
    check(!manifestFromJson(withField("version", 2), back, &err) && err.contains("version 2"),
          "an unknown version is refused, naming it");
    Manifest unsafe = m;
    unsafe.media[0].notes = "../escape.jnotes";
    check(!manifestFromJson(manifestToJson(unsafe), back, &err), "an unsafe entry name is refused");
    Manifest driveSession = m;
    driveSession.session = "C:/evil";
    check(!manifestFromJson(manifestToJson(driveSession), back, &err), "a drive-qualified session name is refused");
    Manifest driveNotes = m;
    driveNotes.media[0].notes = "C:/evil";
    check(!manifestFromJson(manifestToJson(driveNotes), back, &err), "a drive-qualified notes name is refused");
    Manifest driveLut = m;
    driveLut.luts[0].file = "C:/evil";
    check(!manifestFromJson(manifestToJson(driveLut), back, &err), "a drive-qualified LUT name is refused");
    auto withMediaIndex = [&m](int index) {
        QJsonObject o = QJsonDocument::fromJson(manifestToJson(m)).object();
        QJsonArray media = o["media"].toArray();
        QJsonObject mo = media[0].toObject();
        mo["index"] = index;
        media[0] = mo;
        o["media"] = media;
        return QJsonDocument(o).toJson();
    };
    err.clear();
    check(!manifestFromJson(withMediaIndex(5), back, &err) && err.contains("Invalid media index"),
          "an out-of-range media index is refused");
    auto withDuplicateMediaIndex = [&m]() {
        QJsonObject o = QJsonDocument::fromJson(manifestToJson(m)).object();
        QJsonArray media = o["media"].toArray();
        media.append(media[0]);
        o["media"] = media;
        return QJsonDocument(o).toJson();
    };
    err.clear();
    check(!manifestFromJson(withDuplicateMediaIndex(), back, &err) && err.contains("Invalid media index"),
          "a repeated media index is refused");
    check(isInsideDir("/cache/abc", "/cache/abc/media/000/x.exr") &&
          !isInsideDir("/cache/abc", "/cache/abcd/x.exr") &&
          !isInsideDir("/cache/abc", "/cache/abc/../evil.txt") &&
          !isInsideDir("/cache/abc", "/cache/abc"),
          "extraction containment compares whole path components");
    check(!manifestFromJson("not json", back, &err), "text that is not JSON is refused");
    check(indexDir("media", 7) == "media/007", "index directories are zero-padded");

    // --- exporter ---
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() /
        ("jefe_package_test_" + std::to_string(QDateTime::currentMSecsSinceEpoch()));
    fs::create_directories(dir / "src", ec);
    const std::string f1 = (dir / "src" / "sh010.0001.exr").string();
    const std::string f2 = (dir / "src" / "sh010.0002.exr").string();
    const std::string lut = (dir / "look.cube").string();
    writeText(f1, "frame-one");
    writeText(f2, "frame-two-xyz");
    writeText(lut, "LUT");

    ExportInput in;
    in.outPath = (dir / "with.jcreview").string();
    in.includeMedia = true;
    in.sessionXml = std::string("<?xml version=\"1.0\"?>\n<root><plates><plate plateID=\"0\" gamma=\"1.5\" lut=\"look.cube\"/></plates>") +
                    "<tracks><track trackID=\"0\" filename=\"" + f1 + "\" from=\"1\" to=\"2\"/></tracks><playlist/></root>\n";
    in.appVersion = "1.7.0";
    in.createdIso = "2026-09-14T20:10:00Z";
    ExportMedia em;
    em.mediaPath = gfcNoteStore::normalisePath(f1);
    em.frames = {f1, f2};
    em.notesXml = "<jefecheckNotes version=\"1\"/>";
    em.fingerprint = "fp1:abc";
    em.width = 2;
    em.height = 2;
    in.media.push_back(em);
    in.luts.emplace_back("look.cube", lut);

    Exporter exporter;
    check(exporter.begin(in, &err), "the exporter begins");
    check(runToEnd(exporter, &err) == Exporter::State::Done, "the exporter finishes");
    check(exporter.bytesTotal() > 0 && exporter.bytesDone() == exporter.bytesTotal(), "progress reaches the total");
    check(fs::exists(in.outPath, ec) && !fs::exists(in.outPath + ".partial", ec), "package written, no partial left");

    gfcTar::Reader reader;
    std::string terr;
    check(reader.open(in.outPath, &terr), "the package is a valid archive");
    std::vector<std::string> names;
    for (const gfcTar::Entry& e : reader.entries()) names.push_back(e.name);
    check(names == std::vector<std::string>{"manifest.json", "session.jcs", "notes/000.jnotes", "luts/look.cube",
                                            "media/000/sh010.0001.exr", "media/000/sh010.0002.exr"},
          "entries in the stated order");
    std::string bytes;
    const gfcTar::Entry* frame2 = reader.find("media/000/sh010.0002.exr");
    check(frame2 && reader.readBytes(*frame2, bytes, &terr) && bytes == "frame-two-xyz", "media packaged byte for byte");
    const gfcTar::Entry* session = reader.find("session.jcs");
    check(session && reader.readBytes(*session, bytes, &terr) &&
          bytes.find("filename=\"media/000/sh010.0001.exr\"") != std::string::npos &&
          bytes.find(f1) == std::string::npos && bytes.find("gamma=\"1.5\"") != std::string::npos,
          "the session points at the packaged media and keeps its settings");
    Manifest written;
    check(!reader.entries().empty() && reader.readBytes(reader.entries()[0], bytes, &terr) &&
          manifestFromJson(QByteArray::fromStdString(bytes), written, &err) && written.mediaIncluded &&
          written.media.size() == 1 && written.media[0].packagedPath == "media/000/sh010.####.exr" &&
          written.media[0].frames == std::vector<std::string>{"sh010.0001.exr", "sh010.0002.exr"} &&
          written.luts.size() == 1 && written.luts[0].file == "luts/look.cube",
          "the manifest describes what was packaged");

    ExportInput lean = in;
    lean.outPath = (dir / "without.jcreview").string();
    lean.includeMedia = false;
    Exporter leanExporter;
    check(leanExporter.begin(lean, &err) && runToEnd(leanExporter, &err) == Exporter::State::Done, "a lean package is written");
    gfcTar::Reader leanReader;
    bool anyMedia = false;
    if (leanReader.open(lean.outPath, &terr)) {
        for (const gfcTar::Entry& e : leanReader.entries()) {
            if (e.name.rfind("media/", 0) == 0) anyMedia = true;
        }
    }
    const gfcTar::Entry* leanSession = leanReader.find("session.jcs");
    check(!anyMedia && leanSession && leanReader.readBytes(*leanSession, bytes, &terr) &&
          bytes.find(f1) != std::string::npos,
          "a lean package has no media and keeps absolute paths");

    ExportInput cancelled = in;
    cancelled.outPath = (dir / "cancelled.jcreview").string();
    Exporter cancelExporter;
    cancelExporter.begin(cancelled, &err);
    cancelExporter.step(&err);
    cancelExporter.cancel();
    check(cancelExporter.state() == Exporter::State::Cancelled && !fs::exists(cancelled.outPath, ec) &&
          !fs::exists(cancelled.outPath + ".partial", ec),
          "cancel leaves neither package nor partial file");

    ExportInput missing = in;
    missing.outPath = (dir / "missing.jcreview").string();
    missing.media[0].frames.push_back((dir / "src" / "gone.0003.exr").string());
    Exporter missingExporter;
    err.clear();
    check(!missingExporter.begin(missing, &err) && err.contains("gone.0003.exr") &&
          !fs::exists(missing.outPath + ".partial", ec),
          "a missing frame fails before anything is written");

    ExportInput dupLut = in;
    dupLut.outPath = (dir / "duplut.jcreview").string();
    fs::create_directories(dir / "other", ec);
    const std::string lut2 = (dir / "other" / "look.cube").string();
    writeText(lut2, "LUT2");
    dupLut.luts.emplace_back("look2.cube", lut2);
    Exporter dupLutExporter;
    err.clear();
    check(!dupLutExporter.begin(dupLut, &err) && err.contains("look.cube") &&
          !fs::exists(dupLut.outPath + ".partial", ec),
          "two same-named LUT files are refused");

    // Final review, item 6: the package must never be written over one of
    // its own sources (the rename at the end would replace the frame).
    ExportInput overSource = in;
    overSource.outPath = f2;
    Exporter overSourceExporter;
    err.clear();
    check(!overSourceExporter.begin(overSource, &err) && err.contains("sh010.0002.exr") &&
          readText(f2) == "frame-two-xyz" && !fs::exists(f2 + ".partial", ec),
          "an output path equal to a source frame is refused");
    overSourceExporter.cancel();

    fs::remove_all(dir, ec);
    std::printf("NOTE-PACKAGE: pass=%d fail=%d\n", pass, fail);
    return fail;
}

int packageOpenSelfTest() {
    namespace fs = std::filesystem;
    int pass = 0;
    int fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) {
            ++pass;
        } else {
            ++fail;
            std::fprintf(stderr, "NOTE-PACKAGE-OPEN FAIL: %s\n", msg);
        }
    };
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() /
        ("jefe_package_open_test_" + std::to_string(QDateTime::currentMSecsSinceEpoch()));
    fs::create_directories(dir / "src", ec);
    const std::string f1 = (dir / "src" / "sh010.0001.exr").string();
    const std::string lut = (dir / "look.cube").string();
    writeText(f1, "frame-one");
    writeText(lut, "LUT");

    gfcReview review;
    review.mediaPath = gfcNoteStore::normalisePath(f1);
    {
        gfcRevision& round = review.beginRevision("Supervisor");
        round.id = "round-1";
        auto n = std::make_unique<gfcNoteStroke>();
        n->id = "note-1";
        n->pts = { gfcNotePoint{0.1f, 0.1f} };
        round.addNote(std::move(n));
    }

    ExportInput in;
    in.outPath = (dir / "with.jcreview").string();
    in.includeMedia = true;
    in.appVersion = "1.7.0";
    in.createdIso = "2026-09-14T20:10:00Z";
    in.sessionXml = std::string("<?xml version=\"1.0\"?>\n<root><plates><plate plateID=\"0\" lut=\"look.cube\"><stack><FXS><FX name=\"Grade\"/></FXS></stack></plate></plates>") +
                    "<tracks><track trackID=\"0\" filename=\"" + f1 + "\"/></tracks><playlist/></root>\n";
    ExportMedia em;
    em.mediaPath = review.mediaPath;
    em.frames = {f1};
    em.notesXml = gfcNoteStore::toXmlString(review);
    em.fingerprint = "fp1:0000";
    em.width = 1;
    em.height = 1;
    in.media.push_back(em);
    in.luts.emplace_back("look.cube", lut);
    auto exportAll = [](const ExportInput& input, QString* err) {
        Exporter exporter;
        return exporter.begin(input, err) && runToEnd(exporter, err) == Exporter::State::Done;
    };
    QString err;
    check(exportAll(in, &err), "fixture package exported");

    std::vector<std::string> lutsLoaded;
    std::vector<std::string> reviewsReloaded;
    OpenServices services;
    services.loadLut = [&lutsLoaded](const std::string& path) { lutsLoaded.push_back(path); return true; };
    services.reloadReview = [&reviewsReloaded](const std::string& media) { reviewsReloaded.push_back(media); };
    const std::string cache = (dir / "cache").string();

    OpenResult result;
    check(openPackage(in.outPath, cache, services, result, &err), "the package opens");
    const fs::path extracted = fs::path(result.extractDir) / "media" / "000" / "sh010.0001.exr";
    const std::string extractedMedia = gfcNoteStore::normalisePath(extracted.string());
    check(result.resolved == 1 && result.missing == 0, "packaged media resolves");
    check(fs::exists(fs::path(result.extractDir) / ".complete", ec) && fs::exists(extracted, ec),
          "the package is extracted with a completion marker");
    check(readText(result.sessionPath).find("filename=\"" + extracted.string() + "\"") != std::string::npos,
          "the session points at the extracted media");
    check(lutsLoaded.size() == 1 && lutsLoaded[0] == (fs::path(result.extractDir) / "luts" / "look.cube").string(),
          "the packaged LUT is loaded");
    check(reviewsReloaded.size() == 1 && reviewsReloaded[0] == extractedMedia, "the app is told to reload the review");
    gfcReview placed;
    check(gfcNoteStore::load(extractedMedia, placed) && placed.revisions.size() == 1 &&
          placed.revisions[0].notes.size() == 1 && placed.fingerprint == "fp1:0000",
          "notes are placed beside the extracted media");
    check(result.fxNames == std::vector<std::string>{"Grade"}, "the session's FX names are reported");
    check(result.notesProblems.empty() && result.lutsNotLoaded.empty(),
          "no notes problems or LUT failures on a normal open");

    // Fix round 1, point 2a: a file that does not belong to the package
    // must survive an open that reuses a complete cache.
    writeText((fs::path(result.extractDir) / "sentinel.txt").string(), "keep-me");

    OpenResult again;
    check(openPackage(in.outPath, cache, services, again, &err) && again.extractDir == result.extractDir,
          "reopening reuses the extraction");
    gfcReview placedAgain;
    check(gfcNoteStore::load(extractedMedia, placedAgain) && placedAgain.revisions.size() == 1 &&
          placedAgain.revisions[0].notes.size() == 1,
          "reopening does not duplicate notes");
    check(fs::exists(fs::path(again.extractDir) / "sentinel.txt", ec),
          "reopening a complete cache does not touch its other contents");

    // Fix round 1, point 2a / fix round 2, point 3: a completed cache
    // missing one packaged frame is damaged, not reusable as-is -- it is
    // repaired by writing the archive's entries back over the directory
    // (gfcTar::Reader::extractTo truncates an existing file and creates a
    // missing one) rather than discarding the whole directory first, so a
    // reviewer's own sidecar beside the extracted media -- and anything
    // else that isn't part of the package -- survives the repair.
    gfcReview sidecarBeforeDamage;
    check(gfcNoteStore::load(extractedMedia, sidecarBeforeDamage) && sidecarBeforeDamage.revisions.size() == 1,
          "a sidecar exists beside the extracted media before the cache is damaged");
    fs::remove(extracted, ec);
    OpenResult damaged;
    check(openPackage(in.outPath, cache, services, damaged, &err) && damaged.extractDir == result.extractDir &&
          damaged.resolved == 1 && fs::exists(extracted, ec) &&
          fs::exists(fs::path(damaged.extractDir) / "sentinel.txt", ec),
          "a damaged cache (a packaged frame missing) is repaired without discarding the sentinel file");
    gfcReview sidecarAfterDamage;
    check(gfcNoteStore::load(extractedMedia, sidecarAfterDamage) && sidecarAfterDamage.revisions.size() == 1 &&
          sidecarAfterDamage.revisions[0].id == "round-1",
          "the sidecar beside the extracted media survives re-extraction");

    // Fix round 1, point 2a: a cache whose .complete marker is gone is not
    // reusable either.
    fs::remove(fs::path(result.extractDir) / ".complete", ec);
    OpenResult noMarker;
    check(openPackage(in.outPath, cache, services, noMarker, &err) && noMarker.resolved == 1 &&
          fs::exists(fs::path(noMarker.extractDir) / ".complete", ec),
          "a cache without a .complete marker is re-extracted and succeeds");

    // Final review, item 5: a repair that fails part-way must not leave the
    // old .complete marker vouching for a half-rewritten cache. A directory
    // where the notes entry belongs makes that entry's extraction fail.
    fs::remove(extracted, ec);
    const fs::path notesEntry = fs::path(result.extractDir) / "notes" / "000.jnotes";
    fs::remove(notesEntry, ec);
    fs::create_directories(notesEntry / "blocker", ec);
    OpenResult brokenRepair;
    check(!openPackage(in.outPath, cache, services, brokenRepair, &err) &&
          !fs::exists(fs::path(result.extractDir) / ".complete", ec),
          "a repair that fails part-way leaves no .complete marker");
    fs::remove_all(notesEntry, ec);
    OpenResult repaired;
    check(openPackage(in.outPath, cache, services, repaired, &err) && repaired.resolved == 1 &&
          fs::exists(fs::path(result.extractDir) / ".complete", ec),
          "a repaired cache has its .complete marker again");

    // Fix round 2, point 1: LUTs are only loaded once the rewritten session
    // is confirmed on disk -- a session-write failure (after the rewrite
    // itself already succeeded) loads none. Triggered by pre-creating a
    // directory at the exact path openPackage writes the rewritten session
    // to, so the write-and-verify step fails cleanly.
    fs::remove(fs::path(result.extractDir) / "session.opened.jcs", ec);
    fs::create_directory(fs::path(result.extractDir) / "session.opened.jcs", ec);
    const size_t lutsLoadedBeforeWriteFailure = lutsLoaded.size();
    OpenResult writeFail;
    err.clear();
    check(!openPackage(in.outPath, cache, services, writeFail, &err) &&
          lutsLoaded.size() == lutsLoadedBeforeWriteFailure,
          "a session-write failure after a successful rewrite loads no LUTs");
    fs::remove_all(fs::path(result.extractDir) / "session.opened.jcs", ec);

    // Fix round 2, point 5: reopening a package whose notes haven't changed
    // must leave the sidecar untouched and must not tell the app to reload
    // it.
    const std::string stableSidecar = gfcNoteStore::sidecarPathFor(extractedMedia);
    const std::string stableSidecarBytesBefore = readText(stableSidecar);
    const auto stableSidecarMtimeBefore = fs::last_write_time(stableSidecar, ec);
    const size_t reloadsBeforeUnchangedReopen = reviewsReloaded.size();
    OpenResult unchanged;
    check(openPackage(in.outPath, cache, services, unchanged, &err) && unchanged.notesProblems.empty(),
          "reopening an unchanged package still opens cleanly");
    check(readText(stableSidecar) == stableSidecarBytesBefore, "an unchanged reopen leaves the sidecar bytes unchanged");
    check(fs::last_write_time(stableSidecar, ec) == stableSidecarMtimeBefore,
          "an unchanged reopen leaves the sidecar's modification time unchanged");
    check(reviewsReloaded.size() == reloadsBeforeUnchangedReopen, "an unchanged reopen makes no reloadReview call");

    // Fix round 2, point 4: a PRIMARY sidecar that exists but is corrupt
    // must not silently fall through to a stale fallback-location sidecar
    // -- gfcNoteStore::load() spans both locations, so the primary is
    // checked directly. The fallback location is set up the same way
    // gfcNoteStore's own self-test (noteStoreSelfTest) verifies it, since
    // gfcNoteStore exposes no way to target one location specifically.
    fs::create_directories(dir / "src5", ec);
    const std::string fbFrame = (dir / "src5" / "fb010.0001.exr").string();
    writeText(fbFrame, "fallback-frame");
    const std::string fbNormalised = gfcNoteStore::normalisePath(fbFrame);

    gfcReview fbFallbackReview;
    fbFallbackReview.mediaPath = fbNormalised;
    {
        gfcRevision& r = fbFallbackReview.beginRevision("Fallback-Author");
        r.id = "round-fallback";
        auto n = std::make_unique<gfcNoteStroke>();
        n->id = "note-fallback";
        n->pts = { gfcNotePoint{0.4f, 0.4f} };
        r.addNote(std::move(n));
    }
    const char* fbHome = std::getenv("HOME");
    const std::string fbHomeDir = (fbHome && *fbHome) ? fbHome : ".";
    const std::string fbFallbackPath = fbHomeDir + "/.config/jefecheck/notes/" + gfcSha1::hex(fbNormalised) + ".jnotes";
    fs::create_directories(fs::path(fbFallbackPath).parent_path(), ec);
    writeText(fbFallbackPath, gfcNoteStore::toXmlString(fbFallbackReview));

    // A CORRUPT primary sidecar, right beside the media (that directory is
    // definitely writable -- it is inside our own temp dir).
    const std::string fbPrimaryPath = gfcNoteStore::sidecarPathFor(fbNormalised);
    check(fbPrimaryPath != fbFallbackPath, "the fallback setup actually targets the fallback location");
    writeText(fbPrimaryPath, "not xml, a corrupt primary sidecar");

    ExportInput fbPkg;
    fbPkg.outPath = (dir / "fallback.jcreview").string();
    fbPkg.includeMedia = false;
    fbPkg.appVersion = "1.7.0";
    fbPkg.createdIso = "2026-09-14T20:10:00Z";
    fbPkg.sessionXml = std::string("<?xml version=\"1.0\"?>\n<root><plates/>") +
                       "<tracks><track trackID=\"0\" filename=\"" + fbFrame + "\"/></tracks><playlist/></root>\n";
    ExportMedia fbMedia;
    fbMedia.mediaPath = fbNormalised;
    fbMedia.frames = {fbFrame};
    fbMedia.notesXml = "<jefecheckNotes version=\"1\"/>";
    fbMedia.fingerprint = "fp1:fallback";
    fbMedia.width = 1;
    fbMedia.height = 1;
    fbPkg.media.push_back(fbMedia);
    check(exportAll(fbPkg, &err), "fallback-precedence fixture package exported");

    OpenResult fbResult;
    err.clear();
    check(openPackage(fbPkg.outPath, cache, services, fbResult, &err), "fallback-precedence fixture opens");
    check(fbResult.notesProblems.size() == 1 &&
          fbResult.notesProblems[0] == fbMedia.mediaPath + ": local notes unreadable, package notes not merged",
          "a corrupt primary sidecar is reported even though a valid fallback sidecar exists");
    check(readText(fbPrimaryPath) == "not xml, a corrupt primary sidecar", "the corrupt primary sidecar is left untouched");
    gfcReview fbFallbackAfter;
    check(gfcNoteStore::load(fbNormalised, fbFallbackAfter) && fbFallbackAfter.revisions.size() == 1 &&
          fbFallbackAfter.revisions[0].id == "round-fallback",
          "the untouched fallback sidecar still has only its own original content");
    fs::remove(fbFallbackPath, ec);   // best-effort: don't leave test debris under the real $HOME

    // Fix round 1, point 1: merging into an existing, readable local sidecar
    // keeps both the local and the package's notes.
    fs::create_directories(dir / "src3", ec);
    const std::string mrgFrame = (dir / "src3" / "mrg010.0001.exr").string();
    writeText(mrgFrame, "merge-frame");
    gfcReview mrgReview;
    mrgReview.mediaPath = gfcNoteStore::normalisePath(mrgFrame);
    {
        gfcRevision& r = mrgReview.beginRevision("Pkg-Author");
        r.id = "round-pkg";
        auto n = std::make_unique<gfcNoteStroke>();
        n->id = "note-pkg";
        n->pts = { gfcNotePoint{0.2f, 0.2f} };
        r.addNote(std::move(n));
    }
    ExportInput mergePkg;
    mergePkg.outPath = (dir / "merge.jcreview").string();
    mergePkg.includeMedia = true;
    mergePkg.appVersion = "1.7.0";
    mergePkg.createdIso = "2026-09-14T20:10:00Z";
    mergePkg.sessionXml = std::string("<?xml version=\"1.0\"?>\n<root><plates/>") +
                          "<tracks><track trackID=\"0\" filename=\"" + mrgFrame + "\"/></tracks><playlist/></root>\n";
    ExportMedia mrgMedia;
    mrgMedia.mediaPath = mrgReview.mediaPath;
    mrgMedia.frames = {mrgFrame};
    mrgMedia.notesXml = gfcNoteStore::toXmlString(mrgReview);
    mrgMedia.fingerprint = "fp1:merge";
    mrgMedia.width = 1;
    mrgMedia.height = 1;
    mergePkg.media.push_back(mrgMedia);
    check(exportAll(mergePkg, &err), "merge-fixture package exported");

    OpenResult mergePre;
    check(openPackage(mergePkg.outPath, cache, services, mergePre, &err),
          "merge fixture opens once to learn its resolved path");
    const fs::path mrgExtracted = fs::path(mergePre.extractDir) / "media" / "000" / "mrg010.0001.exr";
    const std::string mrgResolvedMedia = gfcNoteStore::normalisePath(mrgExtracted.string());

    // Seed a DIFFERENT local sidecar, as if the user had already annotated
    // this media outside of any package.
    gfcReview localOnly;
    localOnly.mediaPath = mrgResolvedMedia;
    {
        gfcRevision& r = localOnly.beginRevision("Local-Author");
        r.id = "round-local";
        auto n = std::make_unique<gfcNoteStroke>();
        n->id = "note-local";
        n->pts = { gfcNotePoint{0.7f, 0.7f} };
        r.addNote(std::move(n));
    }
    check(gfcNoteStore::save(localOnly), "a pre-existing local sidecar is seeded");

    OpenResult merged;
    check(openPackage(mergePkg.outPath, cache, services, merged, &err), "merge fixture reopens");
    gfcReview mrgPlaced;
    check(gfcNoteStore::load(mrgResolvedMedia, mrgPlaced) && mrgPlaced.revisions.size() == 2,
          "both the local and the package revisions are present after merging");
    bool hasLocalNote = false, hasPkgNote = false;
    for (const gfcRevision& r : mrgPlaced.revisions) {
        for (const auto& n : r.notes) {
            if (n->id == "note-local") hasLocalNote = true;
            if (n->id == "note-pkg") hasPkgNote = true;
        }
    }
    check(hasLocalNote && hasPkgNote, "the local note and the package note are both present");

    // Fix round 1, point 1: a local sidecar that exists but fails to parse
    // must not be merged into or overwritten; the open still succeeds and
    // reports the problem.
    fs::create_directories(dir / "src4", ec);
    const std::string garbleFrame = (dir / "src4" / "garble010.0001.exr").string();
    writeText(garbleFrame, "garble-frame");
    gfcReview garbleReview;
    garbleReview.mediaPath = gfcNoteStore::normalisePath(garbleFrame);
    {
        gfcRevision& r = garbleReview.beginRevision("Pkg-Author");
        r.id = "round-pkg2";
        auto n = std::make_unique<gfcNoteStroke>();
        n->id = "note-pkg2";
        n->pts = { gfcNotePoint{0.3f, 0.3f} };
        r.addNote(std::move(n));
    }
    ExportInput garblePkg;
    garblePkg.outPath = (dir / "garble.jcreview").string();
    garblePkg.includeMedia = true;
    garblePkg.appVersion = "1.7.0";
    garblePkg.createdIso = "2026-09-14T20:10:00Z";
    garblePkg.sessionXml = std::string("<?xml version=\"1.0\"?>\n<root><plates/>") +
                           "<tracks><track trackID=\"0\" filename=\"" + garbleFrame + "\"/></tracks><playlist/></root>\n";
    ExportMedia garbleMedia;
    garbleMedia.mediaPath = garbleReview.mediaPath;
    garbleMedia.frames = {garbleFrame};
    garbleMedia.notesXml = gfcNoteStore::toXmlString(garbleReview);
    garbleMedia.fingerprint = "fp1:garble";
    garbleMedia.width = 1;
    garbleMedia.height = 1;
    garblePkg.media.push_back(garbleMedia);
    check(exportAll(garblePkg, &err), "garbled-sidecar fixture package exported");

    OpenResult garblePre;
    check(openPackage(garblePkg.outPath, cache, services, garblePre, &err),
          "garbled fixture opens once to learn its resolved path");
    const fs::path garbleExtracted = fs::path(garblePre.extractDir) / "media" / "000" / "garble010.0001.exr";
    const std::string garbleResolvedMedia = gfcNoteStore::normalisePath(garbleExtracted.string());
    const std::string garbleSidecar = gfcNoteStore::sidecarPathFor(garbleResolvedMedia);
    const std::string garbageBytes = "this is not xml at all, just garbage bytes";
    writeText(garbleSidecar, garbageBytes);

    OpenResult garbled;
    err.clear();
    check(openPackage(garblePkg.outPath, cache, services, garbled, &err),
          "opening still succeeds despite a corrupt local sidecar");
    check(readText(garbleSidecar) == garbageBytes, "the corrupt local sidecar is left byte-identical");
    check(garbled.notesProblems.size() == 1 &&
          garbled.notesProblems[0] == garbleMedia.mediaPath + ": local notes unreadable, package notes not merged",
          "the problem is reported against the media's original path");

    // Fix round 1 follow-up: the package's OWN notes entry being garbage
    // must not be skipped silently either -- it is reported, and no local
    // sidecar gets created from it.
    {
        Manifest badNotesManifest;
        badNotesManifest.created = "2026-09-14T20:10:00Z";
        badNotesManifest.app = "1.7.0";
        badNotesManifest.session = "session.jcs";
        badNotesManifest.mediaIncluded = true;
        ManifestMedia bnMedia;
        bnMedia.index = 0;
        bnMedia.originalPath = (dir / "badnotes_src" / "badnotes.####.exr").string();
        bnMedia.packagedPath = "media/000/badnotes.0001.exr";
        bnMedia.fingerprint = "fp1:badnotes";
        bnMedia.width = 1;
        bnMedia.height = 1;
        bnMedia.frames = {"badnotes.0001.exr"};
        bnMedia.notes = "notes/000.jnotes";
        badNotesManifest.media.push_back(bnMedia);

        const std::string badNotesSessionXml = std::string("<?xml version=\"1.0\"?>\n<root><plates/>") +
            "<tracks><track trackID=\"0\" filename=\"media/000/badnotes.0001.exr\"/></tracks><playlist/></root>\n";

        gfcTar::Writer writer;
        std::string terr;
        writer.open((dir / "badnotes.jcreview").string(), &terr);
        writer.addBytes("manifest.json", manifestToJson(badNotesManifest).toStdString(), &terr);
        writer.addBytes("session.jcs", badNotesSessionXml, &terr);
        writer.addBytes("notes/000.jnotes", "this is not xml notes at all, just garbage", &terr);
        writer.addBytes("media/000/badnotes.0001.exr", "badnotes-frame", &terr);
        writer.finish(&terr);

        OpenResult badNotes;
        err.clear();
        check(openPackage((dir / "badnotes.jcreview").string(), cache, services, badNotes, &err) &&
              badNotes.resolved == 1 && badNotes.missing == 0 &&
              badNotes.notesProblems.size() == 1 &&
              badNotes.notesProblems[0] == bnMedia.originalPath + ": package notes unreadable, not merged",
              "the package's own unparsable notes entry is reported, not silently skipped");
        const std::string bnResolvedMedia = gfcNoteStore::normalisePath(
            (fs::path(badNotes.extractDir) / "media" / "000" / "badnotes.0001.exr").string());
        gfcReview bnLocal;
        check(!gfcNoteStore::load(bnResolvedMedia, bnLocal), "no local sidecar is created from unreadable package notes");
    }

    ExportInput lean = in;
    lean.outPath = (dir / "lean.jcreview").string();
    lean.includeMedia = false;
    check(exportAll(lean, &err), "lean package exported");
    OpenResult leanResult;
    check(openPackage(lean.outPath, cache, services, leanResult, &err) && leanResult.resolved == 1 &&
          readText(leanResult.sessionPath).find("filename=\"" + f1 + "\"") != std::string::npos,
          "a lean package uses the media at its original path");
    fs::remove(f1, ec);
    OpenResult gone;
    check(openPackage(lean.outPath, cache, services, gone, &err) && gone.resolved == 0 && gone.missing == 1 &&
          gone.missingMedia == std::vector<std::string>{"sh010.####.exr"} &&
          readText(gone.sessionPath).find("filename=\"" + f1 + "\"") != std::string::npos,
          "missing media is counted and keeps its original path");

    // Fix round 1: a lean-package rewrite where the resolved path actually
    // differs from what the session recorded, so the rewrite is genuinely
    // observed rather than a same-string round trip. The session names a
    // second, non-existent frame of the sequence; only the first frame
    // exists, so the rewrite must map the reference to a different path.
    fs::create_directories(dir / "src2", ec);
    const std::string rw1 = (dir / "src2" / "rw010.0001.exr").string();
    const std::string rw2 = (dir / "src2" / "rw010.0002.exr").string();   // never created
    writeText(rw1, "rw-frame-one");
    gfcReview rwReview;
    rwReview.mediaPath = gfcNoteStore::normalisePath(rw1);
    ExportInput rewriteCheck;
    rewriteCheck.outPath = (dir / "rewrite_check.jcreview").string();
    rewriteCheck.includeMedia = false;
    rewriteCheck.appVersion = "1.7.0";
    rewriteCheck.createdIso = "2026-09-14T20:10:00Z";
    rewriteCheck.sessionXml = std::string("<?xml version=\"1.0\"?>\n<root><plates/>") +
                              "<tracks><track trackID=\"0\" filename=\"" + rw2 + "\"/></tracks><playlist/></root>\n";
    ExportMedia rwMedia;
    rwMedia.mediaPath = rwReview.mediaPath;
    rwMedia.frames = {rw1};
    rwMedia.notesXml = gfcNoteStore::toXmlString(rwReview);
    rwMedia.fingerprint = "fp1:rw";
    rwMedia.width = 1;
    rwMedia.height = 1;
    rewriteCheck.media.push_back(rwMedia);
    check(exportAll(rewriteCheck, &err), "rewrite-check package exported");
    OpenResult rewriteResult;
    check(openPackage(rewriteCheck.outPath, cache, services, rewriteResult, &err) && rewriteResult.resolved == 1 &&
          readText(rewriteResult.sessionPath).find("filename=\"" + rw1 + "\"") != std::string::npos &&
          readText(rewriteResult.sessionPath).find("filename=\"" + rw2 + "\"") == std::string::npos,
          "a lean package's rewrite is observed when the resolved path differs from the recorded reference");

    // Fix round 1, point 2b/2c: included media whose packaged frame the
    // archive does not actually contain, and whose original path is also
    // absent, counts as missing -- and the rewritten session maps the
    // packaged-path reference back to alongside the original path, not
    // "media/NNN/...".
    {
        Manifest ghostManifest;
        ghostManifest.created = "2026-09-14T20:10:00Z";
        ghostManifest.app = "1.7.0";
        ghostManifest.session = "session.jcs";
        ghostManifest.mediaIncluded = true;
        ManifestMedia gm;
        gm.index = 0;
        gm.originalPath = (dir / "ghost_src" / "ghost.####.exr").string();
        gm.packagedPath = "media/000/ghost.0001.exr";
        gm.fingerprint.clear();   // deliberately empty: skip any fingerprint search
        gm.width = 1;
        gm.height = 1;
        gm.frames = {"ghost.0001.exr"};
        gm.notes = "notes/000.jnotes";
        ghostManifest.media.push_back(gm);

        const std::string ghostSessionXml = std::string("<?xml version=\"1.0\"?>\n<root><plates/>") +
            "<tracks><track trackID=\"0\" filename=\"media/000/ghost.0001.exr\"/></tracks><playlist/></root>\n";

        gfcTar::Writer writer;
        std::string terr;
        writer.open((dir / "ghost.jcreview").string(), &terr);
        writer.addBytes("manifest.json", manifestToJson(ghostManifest).toStdString(), &terr);
        writer.addBytes("session.jcs", ghostSessionXml, &terr);
        writer.addBytes("notes/000.jnotes", "<jefecheckNotes version=\"1\"/>", &terr);
        // Deliberately no "media/000/ghost.0001.exr" entry: the manifest
        // names a frame the archive does not contain.
        writer.finish(&terr);

        OpenResult ghost;
        err.clear();
        const std::string expectedGhostPath = (dir / "ghost_src" / "ghost.0001.exr").string();
        check(openPackage((dir / "ghost.jcreview").string(), cache, services, ghost, &err) &&
              ghost.resolved == 0 && ghost.missing == 1 &&
              ghost.missingMedia == std::vector<std::string>{"ghost.####.exr"} &&
              readText(ghost.sessionPath).find("filename=\"" + expectedGhostPath + "\"") != std::string::npos &&
              readText(ghost.sessionPath).find("filename=\"media/000/ghost.0001.exr\"") == std::string::npos,
              "missing included media maps the session reference back to alongside the original path");
        check(!ghost.extractDir.empty() && !fs::exists(fs::path(ghost.extractDir) / ".complete", ec),
              "an archive lacking a manifest-named frame leaves no .complete marker");
    }

    // Fix round 1, point 4: a session that fails to parse loads no LUTs and
    // writes no notes.
    {
        Manifest badManifest;
        badManifest.created = "2026-09-14T20:10:00Z";
        badManifest.app = "1.7.0";
        badManifest.session = "session.jcs";
        badManifest.mediaIncluded = true;
        ManifestMedia bm;
        bm.index = 0;
        bm.originalPath = (dir / "src" / "bad.####.exr").string();
        bm.packagedPath = "media/000/bad.0001.exr";
        bm.fingerprint = "fp1:bad";
        bm.width = 1;
        bm.height = 1;
        bm.frames = {"bad.0001.exr"};
        bm.notes = "notes/000.jnotes";
        badManifest.media.push_back(bm);
        badManifest.luts.push_back(ManifestLut{"look.cube", "luts/look.cube"});

        gfcTar::Writer writer;
        std::string terr;
        writer.open((dir / "badsession.jcreview").string(), &terr);
        writer.addBytes("manifest.json", manifestToJson(badManifest).toStdString(), &terr);
        writer.addBytes("session.jcs", "not a valid session document", &terr);
        writer.addBytes("notes/000.jnotes", "<jefecheckNotes version=\"1\"/>", &terr);
        writer.addBytes("luts/look.cube", "LUT", &terr);
        writer.addBytes("media/000/bad.0001.exr", "frame", &terr);
        writer.finish(&terr);

        const std::vector<std::string> lutsBefore = lutsLoaded;
        OpenResult badSession;
        err.clear();
        check(!openPackage((dir / "badsession.jcreview").string(), cache, services, badSession, &err) &&
              lutsLoaded.size() == lutsBefore.size(),
              "an unparsable session loads no LUTs");
        // Fix round 2, point 2: a merge would write beside the EXTRACTED
        // media (the archive's own frame is present, since extraction runs
        // before the session is even read), not beside the fictitious
        // original path -- point the check there, or it can never fail.
        const std::string badSessionResolvedMedia = gfcNoteStore::normalisePath(
            (fs::path(badSession.extractDir) / "media" / "000" / "bad.0001.exr").string());
        gfcReview shouldNotExist;
        check(!gfcNoteStore::load(badSessionResolvedMedia, shouldNotExist),
              "and no sidecar is written beside the extracted media");
    }

    const std::string whole = readText(in.outPath);
    writeText((dir / "truncated.jcreview").string(), whole.substr(0, whole.size() / 2));
    OpenResult cut;
    err.clear();
    check(!openPackage((dir / "truncated.jcreview").string(), cache, services, cut, &err) && err.contains("truncated"),
          "a truncated package is refused");
    {
        QJsonObject future = QJsonDocument::fromJson(manifestToJson(Manifest{})).object();
        future["version"] = 2;
        gfcTar::Writer writer;
        std::string terr;
        writer.open((dir / "future.jcreview").string(), &terr);
        writer.addBytes("manifest.json", QJsonDocument(future).toJson().toStdString(), &terr);
        writer.finish(&terr);
    }
    OpenResult future;
    err.clear();
    check(!openPackage((dir / "future.jcreview").string(), cache, services, future, &err) && err.contains("version 2"),
          "an unknown package version is refused, naming it");

    // Open point 2: the tar reader's refusal of an unsafe entry name is what
    // the opener relies on to stay inside the cache root. Build a minimal
    // ustar header by hand (gfcTar::Writer itself refuses an unsafe name, so
    // it cannot be used to create this fixture) naming an entry "../evil.txt",
    // with a correctly recomputed checksum, and confirm openPackage refuses
    // the package and creates nothing outside the cache root.
    {
        auto putOctal = [](unsigned char* field, size_t width, uint64_t value) {
            std::string digits(width - 1, '0');
            for (size_t i = width - 1; i-- > 0 && value > 0; ) {
                digits[i] = static_cast<char>('0' + (value & 7));
                value >>= 3;
            }
            std::memcpy(field, digits.data(), width - 1);
            field[width - 1] = '\0';
        };
        const std::string content = "evil";
        const std::string name = "../evil.txt";
        unsigned char header[512] = {};
        std::memcpy(header, name.data(), name.size());
        putOctal(header + 100, 8, 0644);
        putOctal(header + 108, 8, 0);
        putOctal(header + 116, 8, 0);
        putOctal(header + 124, 12, content.size());
        putOctal(header + 136, 12, 0);
        header[156] = '0';
        std::memcpy(header + 257, "ustar", 6);
        std::memcpy(header + 263, "00", 2);
        std::memset(header + 148, ' ', 8);
        uint64_t sum = 0;
        for (unsigned char b : header) sum += b;
        putOctal(header + 148, 7, sum);
        header[155] = ' ';

        std::ofstream out((dir / "unsafe.jcreview").string(), std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(header), sizeof(header));
        out.write(content.data(), std::streamsize(content.size()));
        const std::string contentPad(512 - content.size(), '\0');
        out.write(contentPad.data(), std::streamsize(contentPad.size()));
        const std::string endMarker(1024, '\0');
        out.write(endMarker.data(), std::streamsize(endMarker.size()));
    }
    OpenResult unsafe;
    err.clear();
    check(!openPackage((dir / "unsafe.jcreview").string(), cache, services, unsafe, &err) &&
          err.contains("unsafe") && unsafe.extractDir.empty() && !fs::exists(fs::path(cache) / "evil.txt", ec),
          "a package with an unsafe entry name is refused, and nothing is created outside the cache root");

    // Final review, item 4: only candidates with the original's extension
    // (case-insensitive) are probed. Both files here are one-frame sequences,
    // like the media; neither is an image, so a probe would be wasted I/O.
    {
        const fs::path root = dir / "extsearch";
        fs::create_directories(root, ec);
        writeText((root / "ext010.0001.txt").string(), "not an image");
        writeText((root / "ext010.0001.EXR").string(), "not an image either");
        ManifestMedia extMedia;
        extMedia.originalPath = (dir / "gone" / "ext010.####.exr").string();
        extMedia.fingerprint = "fp1:ext";
        extMedia.frames = {"ext010.0001.exr"};
        extMedia.width = 1;
        extMedia.height = 1;
        candidateProbes = 0;
        check(findByFingerprint(extMedia, {root.string()}, false).empty() && candidateProbes == 1,
              "a candidate with a different extension is not probed; a different-case one is");
    }

    // Final review, item 4: two unresolved media under one search root walk
    // that root once, not once per media.
    {
        const fs::path root = dir / "walkroot";
        fs::create_directories(root, ec);
        writeText((root / "unrelated.0001.exr").string(), "not the media");
        ExportInput twoLean;
        twoLean.outPath = (dir / "twolean.jcreview").string();
        twoLean.includeMedia = false;
        twoLean.appVersion = "1.7.0";
        twoLean.createdIso = "2026-09-14T20:10:00Z";
        const std::string wa = (dir / "walk_src" / "wa010.0001.exr").string();   // never created
        const std::string wb = (dir / "walk_src" / "wb010.0001.exr").string();   // never created
        twoLean.sessionXml = std::string("<?xml version=\"1.0\"?>\n<root><plates/><tracks>") +
                             "<track trackID=\"0\" filename=\"" + wa + "\"/>" +
                             "<track trackID=\"1\" filename=\"" + wb + "\"/></tracks><playlist/></root>\n";
        for (const std::string& frame : {wa, wb}) {
            ExportMedia wm;
            wm.mediaPath = gfcNoteStore::normalisePath(frame);
            wm.frames = {frame};
            wm.notesXml = "<jefecheckNotes version=\"1\"/>";
            wm.fingerprint = "fp1:" + fs::path(frame).stem().string();
            wm.width = 1;
            wm.height = 1;
            twoLean.media.push_back(wm);
        }
        check(exportAll(twoLean, &err), "two-media lean fixture package exported");
        OpenServices searching = services;
        searching.searchPaths = {root.string()};
        searching.searchRecursive = true;
        OpenResult twoResult;
        searchRootWalks = 0;
        check(openPackage(twoLean.outPath, cache, searching, twoResult, &err) && twoResult.missing == 2 &&
              searchRootWalks == 1,
              "two unresolved media under one search root walk it once");
    }

    fs::remove_all(dir, ec);
    std::printf("NOTE-PACKAGE-OPEN: pass=%d fail=%d\n", pass, fail);
    return fail;
}

}  // namespace jefe::qt::package

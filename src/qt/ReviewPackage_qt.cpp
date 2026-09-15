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
        fs::remove(outPath_, ec);
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
}  // namespace

std::vector<std::string> findByFingerprint(const ManifestMedia& media, const std::vector<std::string>& roots,
                                           bool recursive) {
    namespace fs = std::filesystem;
    if (media.fingerprint.empty() || media.frames.empty()) return {};
    const std::string wantedName = fs::path(media.originalPath).filename().string();
    std::vector<std::vector<std::string>> sameName;
    std::vector<std::vector<std::string>> others;
    for (const std::string& root : roots) {
        for (const auto& [key, frames] : gfcMediaFingerprint::sequencesIn(root, recursive)) {
            if (frames.size() != media.frames.size()) continue;
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

namespace {
// Frames of the media, in order: packaged -> original path -> fingerprint
// search -> interactive locate. Empty when unresolved.
std::vector<std::string> resolveMedia(const ManifestMedia& media, bool mediaIncluded,
                                      const std::filesystem::path& extractDir, const OpenServices& services) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (media.frames.empty()) return {};

    std::vector<std::string> frames;
    if (mediaIncluded && !media.packagedPath.empty()) {
        for (const std::string& f : media.frames) {
            frames.push_back((extractDir / indexDir("media", media.index) / f).string());
        }
        return fs::exists(frames.front(), ec) ? frames : std::vector<std::string>{};
    }

    const fs::path originalDir = fs::path(media.originalPath).parent_path();
    bool allThere = true;
    for (const std::string& f : media.frames) {
        frames.push_back((originalDir / f).string());
        if (!fs::exists(frames.back(), ec)) allThere = false;
    }
    if (allThere) return frames;

    frames = findByFingerprint(media, services.searchPaths, services.searchRecursive);
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
}  // namespace

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
    std::string manifestBytes;
    if (!reader.readBytes(reader.entries()[0], manifestBytes, &e)) return fail("Cannot read the package: " + e);
    if (!manifestFromJson(QByteArray::fromStdString(manifestBytes), result.manifest, err)) return false;
    const Manifest& manifest = result.manifest;

    std::error_code ec;
    const std::string absolute = fs::absolute(packagePath, ec).string();
    const uint64_t size = fs::file_size(packagePath, ec);
    const long long mtime = static_cast<long long>(fs::last_write_time(packagePath, ec).time_since_epoch().count());
    const fs::path dir = fs::path(cacheRoot) / gfcSha1::hex(absolute + "|" + std::to_string(size) + "|" + std::to_string(mtime));
    result.extractDir = dir.string();
    if (!fs::exists(dir / ".complete", ec)) {
        fs::remove_all(dir, ec);
        for (const gfcTar::Entry& entry : reader.entries()) {
            // Reader::open has already refused absolute and ".." names.
            if (!reader.extractTo(entry, (dir / entry.name).string(), &e)) return fail("Cannot extract the package: " + e);
        }
        std::ofstream marker((dir / ".complete").string());
        marker << "ok\n";
        if (!marker) return fail("Cannot write to " + dir.string());
    }

    for (const ManifestLut& lut : manifest.luts) {
        if (services.loadLut) services.loadLut((dir / lut.file).string());
    }

    const std::string sessionXml = readText((dir / manifest.session).string());
    std::vector<gfcSessionPaths::MediaRef> refs;
    if (!gfcSessionPaths::listMedia(sessionXml, refs, &e)) return fail("The package's session is unreadable: " + e);

    std::map<std::string, std::string> mapping;
    for (const ManifestMedia& media : manifest.media) {
        const std::vector<std::string> frames = resolveMedia(media, manifest.mediaIncluded, dir, services);
        if (frames.empty()) {
            ++result.missing;
            result.missingMedia.push_back(fs::path(media.originalPath).filename().string());
            continue;
        }
        ++result.resolved;

        // Every session reference to this media points at the same frame of the
        // resolved sequence (matched by position, since a relinked sequence may
        // be named differently).
        const std::string packagedPrefix = indexDir("media", media.index) + "/";
        for (const gfcSessionPaths::MediaRef& ref : refs) {
            const bool ours = ref.path.rfind(packagedPrefix, 0) == 0 ||
                              gfcNoteStore::normalisePath(ref.path) == media.originalPath;
            if (!ours) continue;
            const std::string name = fs::path(ref.path).filename().string();
            const auto at = std::find(media.frames.begin(), media.frames.end(), name);
            const size_t index = (at == media.frames.end()) ? 0 : size_t(at - media.frames.begin());
            mapping[ref.path] = frames[std::min(index, frames.size() - 1)];
        }

        // Notes: union into whatever the resolved media already has.
        const std::string resolvedMedia = gfcNoteStore::normalisePath(frames.front());
        gfcReview incoming;
        if (gfcNoteStore::fromXmlString(readText((dir / media.notes).string()), incoming)) {
            gfcReview local;
            local.mediaPath = resolvedMedia;
            gfcNoteStore::load(resolvedMedia, local);
            local.mediaPath = resolvedMedia;   // a loaded sidecar may name an older path
            gfcNoteMerge::mergeInto(local, std::move(incoming));
            if (local.fingerprint.empty()) local.fingerprint = media.fingerprint;
            gfcNoteStore::save(local);
        }
        if (services.reloadReview) services.reloadReview(resolvedMedia);
    }

    std::string rewritten;
    if (!gfcSessionPaths::rewriteMedia(sessionXml, mapping, rewritten, &e)) return fail(e);
    const fs::path sessionOut = dir / "session.opened.jcs";
    writeText(sessionOut.string(), rewritten);
    if (readText(sessionOut.string()) != rewritten) return fail("Cannot write " + sessionOut.string());
    result.sessionPath = sessionOut.string();
    gfcSessionPaths::listFxNames(rewritten, result.fxNames, nullptr);
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

    OpenResult again;
    check(openPackage(in.outPath, cache, services, again, &err) && again.extractDir == result.extractDir,
          "reopening reuses the extraction");
    gfcReview placedAgain;
    check(gfcNoteStore::load(extractedMedia, placedAgain) && placedAgain.revisions.size() == 1 &&
          placedAgain.revisions[0].notes.size() == 1,
          "reopening does not duplicate notes");

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

    fs::remove_all(dir, ec);
    std::printf("NOTE-PACKAGE-OPEN: pass=%d fail=%d\n", pass, fail);
    return fail;
}

}  // namespace jefe::qt::package

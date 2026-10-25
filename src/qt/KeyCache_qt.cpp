#include "KeyCache_qt.h"

#include <cstdio>
#include <filesystem>
#include <system_error>

#include "../gfcSha1.h"

namespace jefe::qt::keycache {

namespace {

// A cache directory is named by the SHA-1 of every parameter that changes the
// pixels. Anything else under the root was not put there by the keyer, so the
// cache functions leave it alone rather than measuring or deleting whatever a
// misconfigured root happens to contain — the same rule the package cache uses.
bool looksLikeCacheId(const std::string& name) {
    if (name.size() != 40) return false;
    for (char c : name) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex) return false;
    }
    return true;
}

uint64_t directoryBytes(const std::filesystem::path& dir, int* frames) {
    namespace fs = std::filesystem;
    uint64_t total = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc) || fileEc) continue;
        const auto size = it->file_size(fileEc);
        if (!fileEc) total += uint64_t(size);
        if (frames && it->path().extension() == ".exr") ++*frames;
    }
    return total;
}

}  // namespace

std::string cacheId(const KeyParams& params) {
    // Field order is fixed and every field is separated, so no two different
    // parameter sets can concatenate into the same string.
    const std::string material = params.modelId + "|" + params.fingerprint + "|" + params.hintHash + "|" +
                                 std::to_string(params.backboneSize) + "|" + std::to_string(params.despill) + "|" +
                                 std::to_string(params.refiner) + "|" + (params.linearInput ? "lin" : "srgb") + "|" +
                                 params.screenColor;
    return gfcSha1::hex(material);
}

std::vector<CacheEntry> entries(const std::string& cacheRoot) {
    namespace fs = std::filesystem;
    std::vector<CacheEntry> found;
    std::error_code ec;
    for (fs::directory_iterator it(cacheRoot, ec), end; it != end && !ec; it.increment(ec)) {
        std::error_code dirEc;
        if (!it->is_directory(dirEc) || dirEc) continue;
        const std::string name = it->path().filename().string();
        if (!looksLikeCacheId(name)) continue;
        CacheEntry entry;
        entry.id = name;
        entry.bytes = directoryBytes(it->path(), &entry.frames);
        found.push_back(entry);
    }
    return found;
}

uint64_t totalBytes(const std::string& cacheRoot) {
    uint64_t total = 0;
    for (const CacheEntry& e : entries(cacheRoot)) total += e.bytes;
    return total;
}

std::string framePath(const std::string& cacheRoot, const std::string& id, const std::string& frameName) {
    return (std::filesystem::path(cacheRoot) / id / (frameName + ".exr")).string();
}

bool hasFrame(const std::string& cacheRoot, const std::string& id, const std::string& frameName) {
    std::error_code ec;
    const std::filesystem::path path = framePath(cacheRoot, id, frameName);
    // A zero-byte file is a half-written frame from an interrupted run, not a
    // cache hit: treating it as one would show the reviewer an empty plate.
    return std::filesystem::is_regular_file(path, ec) && !ec && std::filesystem::file_size(path, ec) > 0 && !ec;
}

uint64_t clear(const std::string& cacheRoot, QString* err) {
    namespace fs = std::filesystem;
    uint64_t freed = 0;
    for (const CacheEntry& e : entries(cacheRoot)) {
        std::error_code ec;
        fs::remove_all(fs::path(cacheRoot) / e.id, ec);
        if (ec) {
            if (err) {
                *err = QString("Could not remove %1: %2")
                           .arg(QString::fromStdString(e.id))
                           .arg(QString::fromStdString(ec.message()));
            }
            return freed;
        }
        freed += e.bytes;
    }
    return freed;
}

int selfTest() {
    namespace fs = std::filesystem;
    int pass = 0;
    int fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) {
            ++pass;
            std::printf("NOTE-KEYCACHE ok   %s\n", msg);
        } else {
            ++fail;
            std::printf("NOTE-KEYCACHE FAIL: %s\n", msg);
        }
    };

    KeyParams base;
    base.modelId = "CorridorKey_v1.0";
    base.fingerprint = "fp1:abc123";
    base.hintHash = "0123456789abcdef";
    base.screenColor = "green";

    const std::string id = cacheId(base);
    check(id.size() == 40 && looksLikeCacheId(id), "a cache id is 40 hex characters");
    check(cacheId(base) == id, "the same parameters give the same id");

    // Every field has to move the id, or a stale key would be served after a
    // change the reviewer can see.
    auto differs = [&](KeyParams p, const char* msg) { check(cacheId(p) != id, msg); };
    { KeyParams p = base; p.modelId = "CorridorKey_v2.0"; differs(p, "a new model gives a new id"); }
    { KeyParams p = base; p.fingerprint = "fp1:other"; differs(p, "different media gives a new id"); }
    { KeyParams p = base; p.hintHash = "ff"; differs(p, "a changed hint gives a new id"); }
    { KeyParams p = base; p.backboneSize = 2048; differs(p, "a different backbone size gives a new id"); }
    { KeyParams p = base; p.despill = 5; differs(p, "a different despill gives a new id"); }
    { KeyParams p = base; p.refiner = 1.5; differs(p, "a different refiner gives a new id"); }
    { KeyParams p = base; p.linearInput = true; differs(p, "a different input colourspace gives a new id"); }
    { KeyParams p = base; p.screenColor = "blue"; differs(p, "a different screen colour gives a new id"); }

    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "jefecheck_keycache_selftest";
    fs::remove_all(dir, ec);
    fs::create_directories(dir / id, ec);
    const std::string other(40, 'b');
    fs::create_directories(dir / other, ec);
    fs::create_directories(dir / "not-a-key", ec);

    auto write = [](const fs::path& path, size_t bytes) {
        std::FILE* f = std::fopen(path.string().c_str(), "wb");
        if (!f) return;
        const std::string payload(bytes, 'x');
        if (bytes) std::fwrite(payload.data(), 1, bytes, f);
        std::fclose(f);
    };
    write(dir / id / "plate.0001.exr", 400);
    write(dir / id / "plate.0002.exr", 600);
    write(dir / other / "shot.0001.exr", 100);
    write(dir / "not-a-key" / "keep.txt", 70);

    const std::vector<CacheEntry> found = entries(dir.string());
    check(found.size() == 2, "the cache lists one entry per key, ignoring anything else");
    check(totalBytes(dir.string()) == 1100, "the size counts keyed frames and leaves out what is not a key");
    int framesForId = 0;
    for (const CacheEntry& e : found) {
        if (e.id == id) framesForId = e.frames;
    }
    check(framesForId == 2, "an entry counts its keyed frames");

    check(hasFrame(dir.string(), id, "plate.0001"), "a written frame is a cache hit");
    check(!hasFrame(dir.string(), id, "plate.0099"), "a frame that was never keyed is a miss");
    write(dir / id / "plate.0003.exr", 0);
    check(!hasFrame(dir.string(), id, "plate.0003"),
          "a zero-byte frame from an interrupted run is a miss, not an empty plate");

    QString err;
    const uint64_t freed = clear(dir.string(), &err);
    check(freed == 1100 && err.isEmpty(), "clearing reports the bytes it freed");
    check(!fs::exists(dir / id) && !fs::exists(dir / other), "clearing removes every key");
    check(fs::exists(dir / "not-a-key" / "keep.txt"), "clearing leaves everything that is not a key alone");
    check(fs::exists(dir) && totalBytes(dir.string()) == 0, "the cache root survives, empty");
    check(totalBytes((dir / "no_such_root").string()) == 0,
          "a cache root that does not exist measures zero rather than failing");

    fs::remove_all(dir, ec);
    std::printf("NOTE-KEYCACHE: pass=%d fail=%d\n", pass, fail);
    return fail;
}

}  // namespace jefe::qt::keycache

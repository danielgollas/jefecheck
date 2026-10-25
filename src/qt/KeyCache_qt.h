// Neural keyer cache (CorridorKey): where keyed frames live and what they are
// keyed by. docs/superpowers/specs/2026-10-17-neural-keyer-design.md
// QtCore only — no glad, no managers (developer_notes.md §1).
//
// Keying a frame costs seconds, so a frame is keyed once and reused by every
// entry point (this frame, a range, the FX node during playback). The cache id
// is what makes that safe: it folds in everything that changes the result, so a
// different model, plate, hint or parameter set cannot collide with an older
// answer.
#ifndef JEFECHECK_QT_KEY_CACHE_H
#define JEFECHECK_QT_KEY_CACHE_H

#include <QString>

#include <cstdint>
#include <string>
#include <vector>

namespace jefe::qt::keycache {

/** Everything about a key that changes its pixels. */
struct KeyParams {
    std::string modelId;        // e.g. "CorridorKey_v1.0"
    std::string fingerprint;    // the media's "fp1:…" (gfcMediaFingerprint)
    std::string hintHash;       // SHA-1 of the hint image bytes
    int backboneSize = 1024;    // --image-size: inference detail, not output size
    int despill = 0;            // 0–10
    double refiner = 1.0;
    bool linearInput = false;   // --linear vs --srgb
    std::string screenColor;    // "auto", "green" or "blue"
};

/**
 * The directory name for one set of parameters: 40 hex characters, so a cache
 * root can be told apart from whatever else lives beside it. Two frames of the
 * same plate keyed the same way share it; changing any field gives a new one.
 */
std::string cacheId(const KeyParams& params);

/** One keyed sequence under a cache root. */
struct CacheEntry {
    std::string id;         // the directory name (a cache id)
    uint64_t bytes = 0;
    int frames = 0;         // EXRs inside
};

/**
 * Every keyed sequence under @a cacheRoot. Only directories named like a cache
 * id are reported, so a misconfigured root cannot make unrelated files look
 * like cache content.
 */
std::vector<CacheEntry> entries(const std::string& cacheRoot);

/** Total bytes of everything entries() reports. */
uint64_t totalBytes(const std::string& cacheRoot);

/** The keyed frame for one frame of one key, whether or not it exists yet. */
std::string framePath(const std::string& cacheRoot, const std::string& id, const std::string& frameName);

/** True when that frame has been keyed already. */
bool hasFrame(const std::string& cacheRoot, const std::string& id, const std::string& frameName);

/**
 * Deletes every keyed sequence entries() reports and returns the bytes freed.
 * The root itself, and anything in it that is not a cache id, are left alone.
 * On a failure @a err names the entry and nothing further is removed.
 */
uint64_t clear(const std::string& cacheRoot, QString* err);

/** Self-test; prints NOTE-KEYCACHE: pass=N fail=N. */
int selfTest();

}  // namespace jefe::qt::keycache

#endif

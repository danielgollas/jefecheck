// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfcCacheSelfTests.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <random>
#include <thread>
#include <utility>
#include <vector>

#include "gfcStructures.h"
#include "gfcmemorymanager.h"
#include "gfcplaybackmanager.h"
#include "gfctrackmanager.h"
#include "gfcSequence.h"
#include "gfcsequencegui.h"
#include "gfcFrameTable.h"
#include "gfcFrameCache.h"
#include "UIConstants.h"
#include "qt/SequenceLoadBridge_qt.h"
#include "gfcIOSurface.h"
#include "gfcUploadPool.h"
#include "gfcframe.h"
#include "gfcMovieReader.h"
#include "gfcAudioClip.h"
#include "gfcAudioEngine.h"
#include "qt/qt_prefs_persist.h"
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QVariant>
#include <set>
#include <string>

extern gfcPlaybackManager playbackManager;
extern gfcTrackManager trackManager;
extern gfcSettings sett;
extern gfcMemoryManager memoryManager;

namespace {
template <class Pred>
bool waitUntil(Pred pred, int ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}
}  // namespace

int memoryMeasureSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    check(!memoryManager.countInactive, "countInactive is false at startup");
    const uint64_t total = memoryManager.totalBytes();
    const uint64_t used0 = memoryManager.usedBytes();
    check(total > (1ull << 30), "totalBytes > 1 GiB");
    check(used0 > 0 && used0 < total, "0 < usedBytes < totalBytes");

    {   // Touching 256 MiB must show up in usedBytes(). The system may compress or
        // reclaim memory between samples (other processes too), so take the
        // highest of several samples and ask for a quarter of it.
        // The fill is pseudo-random: macOS counts compressed pages at their
        // compressed size, and a constant fill compresses to almost nothing.
        const uint64_t before = memoryManager.usedBytes();
        const size_t n = size_t(256) << 20;
        std::vector<uint64_t> buf(n / sizeof(uint64_t));
        uint64_t x = 0x9e3779b97f4a7c15ull;
        for (uint64_t& w : buf) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; w = x; }
        // macOS rate-limits host_statistics64 per process (past a few calls a
        // second it returns the previous figures), so sample sparsely, for up
        // to 5 s, and stop at the first sample that shows the rise.
        uint64_t seen = 0;
        for (int k = 0; k < 20; ++k) {
            seen = std::max(seen, memoryManager.usedBytes());
            if (seen >= before + (uint64_t(64) << 20)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        volatile uint64_t keep = buf[buf.size() / 2]; (void)keep;
        const bool rose = seen >= before + (uint64_t(64) << 20);
        if (!rose) std::printf("MEMORY-MEASURE: before %.0f MiB, highest after %.0f MiB\n", before / 1048576.0, seen / 1048576.0);
        check(rose, "usedBytes rises by >= 64 MiB for a touched 256 MiB");
    }

    {   // 48 half-float 1080p textures: informational GL visibility, hard frame count.
        const int savedQueue = sett.maximumFramesInQueue;
        sett.maximumFramesInQueue = 100000;
        gfcSequence* seq = trackManager.getSequence(0);
        seq->myGUI->setFilename(dir + "/cache_big_24.mp4");
        seq->myGUI->setCompression(GFC_16HALF);
        const uint64_t before = memoryManager.usedBytes();
        seq->loadPreview();
        trackManager.startLoadingSequence(0);
        waitUntil([&] { trackManager.generateTextures(); return seq->getLoadedFrameCount() == 48; }, 20000);
        const uint64_t after = memoryManager.usedBytes();
        const uint64_t expected = uint64_t(48) * 1920 * 1080 * 8;
        const bool visible = after > before && (after - before) >= expected / 2;
        std::printf("MEMORY-MEASURE: gl_visible=%s (delta %.0f MiB, expected %.0f MiB)\n", visible ? "yes" : "no",
                    (after > before ? after - before : 0) / 1048576.0, expected / 1048576.0);
        check(seq->getLoadedFrameCount() == 48, "cache_big_24.mp4 loads 48 frames");
        trackManager.stopLoadingSequence(0);
        seq->unloadAndClear();
        seq->myGUI->setFilename("");
        sett.maximumFramesInQueue = savedQueue;
    }

    std::printf("MEMORY-MEASURE: pass=%d fail=%d\n", pass, fail);
    return fail;
}

int frameStateSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    using Segs = std::vector<std::pair<int, int>>;

    {   // The table on its own.
        gfcFrameTable t;
        t.reset(10);
        uint64_t v = t.version();
        check(t.size() == 10 && t.state(0) == gfcFrameState::Unloaded && t.residentCount() == 0, "reset(10): ten Unloaded frames");
        check(t.firstResident() == -1 && t.lastResident() == -1 && t.onScreen() == -1, "nothing resident or on screen after reset");
        check(t.setDecoding(3, 100) && t.state(3) == gfcFrameState::Decoding && t.reservedBytes() == 100, "setDecoding reserves bytes");
        t.setResident(3, 120);
        check(t.residentBytes() == 120 && t.reservedBytes() == 0 && t.residentCount() == 1, "setResident moves reserved to resident bytes");
        check(t.version() > v, "setResident moves the version"); v = t.version();
        t.setResident(4, 120); t.setResident(7, 120);
        Segs segs; t.segments(segs);
        check(segs == Segs{{3, 4}, {7, 7}}, "segments are {(3,4),(7,7)}");
        check(t.firstResident() == 3 && t.lastResident() == 7, "first/last resident");
        check(t.version() > v, "more resident frames move the version"); v = t.version();
        check(t.setEvicting(4) && t.state(4) == gfcFrameState::Evicting, "setEvicting from Resident");
        check(t.residentBytes() == 240 && t.residentCount() == 2, "an evicting frame's bytes are released at once");
        t.segments(segs);
        check(segs == Segs{{3, 4}, {7, 7}}, "an evicting frame stays in its segment");
        check(!t.setEvicting(5), "setEvicting from Unloaded is refused");
        const uint64_t vBeforeUnload = t.version();
        t.setUnloaded(4);
        t.segments(segs);
        check(segs == Segs{{3, 3}, {7, 7}}, "after setUnloaded(4) segments are {(3,3),(7,7)}");
        check(t.version() > vBeforeUnload, "unloading an evicting frame moves the version");
        const uint64_t vIdle = t.version();
        t.setUnloaded(5);
        check(t.version() == vIdle, "unloading an unloaded frame leaves the version");
        check(!t.setDecoding(3, 10), "setDecoding from Resident is refused");
        t.setFailed(5);
        check(t.state(5) == gfcFrameState::Unloaded, "setFailed from Unloaded is ignored");
        check(t.setDecoding(5, 50), "setDecoding(5)");
        t.setFailed(5);
        check(t.state(5) == gfcFrameState::Failed && t.reservedBytes() == 0, "setFailed from Decoding releases the reservation");
        t.setPinned(2, true); t.setOnScreen(7);
        check(t.pinned(2) && !t.pinned(3) && t.onScreen() == 7, "pins and the on-screen frame");
        t.setPinned(2, false);
        check(!t.pinned(2), "unpin");
        t.reset(4);
        check(t.size() == 4 && t.residentCount() == 0 && t.residentBytes() == 0 && t.reservedBytes() == 0 && !t.pinned(2) && t.onScreen() == -1,
              "reset clears states, bytes and pins");
        check(t.version() > vIdle, "reset moves the version");
        check(t.state(-1) == gfcFrameState::Unloaded && t.state(99) == gfcFrameState::Unloaded && !t.setDecoding(99, 1), "out-of-range indices are harmless");
    }

    {   // A sequence (track B): 120 PNG frames, full load, then an eviction.
        const int savedQueue = sett.maximumFramesInQueue;
        const bool savedThumbs = sett.showThumbnails;
        sett.maximumFramesInQueue = 100000;
        sett.showThumbnails = true;
        gfcSequence* seq = trackManager.getSequence(1);
        seq->myGUI->setFilename(dir + "/cache_seq.0001.png");
        seq->myGUI->setCompression(GFC_8BPC);
        seq->loadPreview();
        trackManager.startLoadingSequence(1);
        waitUntil([&] { trackManager.generateTextures(); return seq->getLoadedFrameCount() == 120; }, 30000);
        trackManager.stopLoadingSequence(1);
        check(seq->getLoadedFrameCount() == 120, "cache_seq loads 120 frames");
        Segs segs; seq->loadedSegments(segs);
        check(segs == Segs{{0, 119}}, "loadedSegments is {(0,119)}");
        check(seq->getRangeStart() == 0 && seq->getRangeEnd() == 119, "range is the trim 0..119");
        check(seq->trackIndex() == 1, "trackIndex of track B");
        check(seq->estimatedFrameBytes() == size_t(320) * 180 * 4, "estimatedFrameBytes for 8-bit 320x180");
        check(seq->frameTable().residentBytes() == size_t(120) * 320 * 180 * 4, "residentBytes = 120 frames of 8-bit 320x180");
        check(seq->getFrame(1, false).textureBytes == size_t(320) * 180 * 4, "a frame records its textureBytes");
        check(!seq->framePending(1) && !seq->framePending(120), "resident frames are not pending");

        const uint64_t v0 = seq->loadedVersion();
        check(seq->freeFrames(5), "freeFrames(5)");
        seq->loadedSegments(segs);
        check(segs == Segs{{5, 119}}, "after freeFrames(5) segments are {(5,119)}");
        check(seq->getLoadedFrameCount() == 115, "115 frames resident");
        check(seq->loadedVersion() > v0, "eviction moves the version");
        check(seq->getThumbnail(0).w > 0, "an evicted frame keeps its thumbnail");
        check(!seq->getFrame(1, false).loaded && seq->getFrame(1, false).textureBytes == 0, "frame 1's texture is gone");
        check(seq->framePending(1) && !seq->framePending(6) && !seq->framePending(0) && !seq->framePending(121), "framePending: evicted yes, resident/outside no");
        check(seq->getRangeStart() == 0 && seq->getRangeEnd() == 119, "eviction leaves the trim range");

        seq->setHoldMode(2);
        check(seq->frameTable().pinned(0) && seq->frameTable().pinned(119), "hold-edge pins both edges");
        const gfcFrame below = seq->getFrame(0, false);
        check(!below.loaded && below.fileName == dir + "/cache_seq.0001.png", "hold-edge below the range returns frame 0 (evicted)");
        const gfcFrame above = seq->getFrame(200, false);
        check(above.loaded && above.fileName == dir + "/cache_seq.0120.png", "hold-edge above the range returns frame 119");
        seq->setHoldMode(0);
        check(!seq->frameTable().pinned(0) && !seq->frameTable().pinned(119), "leaving hold-edge unpins");

        // Bridge: version and 1-based timeline segments with the offset folded in.
        jefe::qt::TrackTimelineState st = jefe::qt::getTrackTimelineState(1);
        check(st.loadedVersion == seq->loadedVersion(), "TrackTimelineState.loadedVersion matches");
        check(st.loadedCount == 115 && st.firstLoadedFrame == 6, "TrackTimelineState count and first loaded frame");
        seq->setOffset(10);
        check(jefe::qt::getTrackLoadedSegments(1) == Segs{{16, 130}}, "getTrackLoadedSegments with offset 10 is {(16,130)}");
        check(jefe::qt::getTrackTimelineState(1).firstLoadedFrame == 16, "firstLoadedFrame folds the offset");
        seq->setOffset(0);

        const uint64_t v1 = seq->loadedVersion();
        seq->unloadAndClear();
        check(seq->isEmpty() && seq->getLoadedFrameCount() == 0, "unloadAndClear empties the track");
        check(seq->loadedVersion() > v1, "clearing moves the version");
        seq->loadedSegments(segs);
        check(segs.empty() && jefe::qt::getTrackLoadedSegments(1).empty(), "no segments after clear");
        seq->myGUI->setFilename("");
        sett.maximumFramesInQueue = savedQueue;
        sett.showThumbnails = savedThumbs;
    }

    std::printf("FRAME-STATE: pass=%d fail=%d\n", pass, fail);
    return fail;
}

int cachePolicySelfTest() {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    using Job = gfcFrameCache::Job;
    using ms = std::chrono::milliseconds;
    const int K = std::max(4, (int)std::lround(24.0f));
    const int M = std::max(4, (int)std::ceil(0.25f * 24.0f));
    auto seq = [](int from, int to) { std::vector<int> v; for (int i = from; i <= to; ++i) v.push_back(i); return v; };
    // Pulls jobs on one track until nextJob times out (at most `max`), committing each as decoded.
    auto pull = [](gfcFrameCache& c, int track, int max, size_t bytes) {
        std::vector<int> out;
        Job j{};
        while ((int)out.size() < max && c.nextJob(track, j, ms(20))) { out.push_back(j.timelineFrame); c.commit(j, bytes, true); }
        return out;
    };

    {   // Window, advance, unload order. ONCE from the start limit: no frame is
        // behind yet, so the whole budget is the window.
        const int N = 141;
        gfcFrameCache c; gfcFrameTable t0; t0.reset(240);
        c.setTestPooledBytes(0);
        c.attachTrack(0, &t0);
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        c.setTestBudget(10 * 1000);
        c.setTransport(100, +1, LOOPMODEONCE_ID, 100, 240, 24);
        std::vector<int> tl; bool fieldsOk = true;
        Job j{};
        for (int i = 0; i < 10 && c.nextJob(0, j, ms(20)); ++i) {
            tl.push_back(j.timelineFrame);
            fieldsOk &= j.track == 0 && j.sourceIndex == j.timelineFrame - 1 && j.reservedBytes == 1000 && j.generation == c.generation();
            fieldsOk &= t0.state(j.sourceIndex) == gfcFrameState::Decoding;
            c.commit(j, 1000, true);
        }
        check(tl == seq(100, 109), "window: ten jobs are timeline 100..109");
        check(fieldsOk, "window: job fields (track, sourceIndex = timeline-1, reservation, generation) and Decoding state");
        check(t0.state(99) == gfcFrameState::Resident && t0.residentCount() == 10, "window: committed frames are Resident");
        check(c.cacheBytes() == 10000, "window: cacheBytes counts the resident frames");
        check(!c.nextJob(0, j, ms(20)), "window: the 11th job is denied at a 10-frame budget");
        check(c.stats().denied >= 1 && c.stats().admitted == 10, "window: stats admitted=10, denied>=1");

        c.advance(105);
        check(c.score(0, 99) == 2 * N + (K - 5), "advance: timeline 100 is near-behind, score 2N+K-5");
        const bool got = c.nextJob(0, j, ms(20));
        check(got && j.timelineFrame == 110, "advance: next job is timeline 110");
        check(c.score(0, 109) == 3 * N + (N - 5), "advance: timeline 110 scores 3N+N-5");
        if (got) c.commit(j, 1000, true);
        gfcFrameCache::Victims v = c.drainVictims();
        check(v.frames.size() == 1 && v.frames[0].track == 0 && v.frames[0].sourceIndex == 99, "advance: the only victim is source 99 (timeline 100)");
        check(t0.state(99) == gfcFrameState::Evicting, "advance: the victim is Evicting");
        check(!v.trimPools, "advance: no pool trim with no pooled bytes");
        check(c.drainVictims().frames.empty(), "advance: a second drain is empty");
        t0.setUnloaded(99);   // what the GL thread does after freeing the texture

        t0.setOnScreen(108);
        c.retarget(200);
        std::vector<int> victims;
        for (int i = 0; i < 12 && c.nextJob(0, j, ms(20)); ++i) {
            c.commit(j, 1000, true);
            for (const auto& x : c.drainVictims().frames) { victims.push_back(x.sourceIndex); t0.setUnloaded(x.sourceIndex); }
        }
        std::vector<int> want = seq(100, 107); want.push_back(109);
        check(victims == want, "order: far-behind victims furthest first (sources 100..107, 109), the on-screen 108 skipped");
        check(t0.state(108) == gfcFrameState::Resident, "order: the on-screen frame stays Resident");
        check(c.score(0, 108) == INT_MAX, "order: the on-screen frame is never a victim (score INT_MAX)");
        check(c.cacheBytes() <= 10000 + 1000, "order: cacheBytes stays within one frame of the budget");
    }

    {   // Loop wrap.
        gfcFrameCache c; gfcFrameTable t; t.reset(20);
        c.setTestPooledBytes(0);
        c.attachTrack(0, &t);
        c.setTrackGeometry(0, 0, 0, 19, 1000);
        c.setTestBudget(40 * 1000);
        c.setTransport(15, +1, LOOPMODELOOP_ID, 1, 20, 24);
        std::vector<int> want = seq(15, 20);
        for (int i = 1; i <= 5; ++i) want.push_back(i);      // ahead after the wrap (a <= b)
        for (int i = 14; i >= 6; --i) want.push_back(i);     // then behind, nearest first
        check(pull(c, 0, 30, 1000) == want, "loop: jobs 15..20, 1..5 (ahead, wrapped), then 14..6 (behind)");
        check(c.score(0, 13) > c.score(0, 7), "loop: timeline 14 (behind 1) outranks timeline 8 (behind 7)");
    }

    {   // Bounce and reverse.
        gfcFrameCache c; gfcFrameTable t; t.reset(20);
        c.setTestPooledBytes(0);
        c.attachTrack(0, &t);
        c.setTrackGeometry(0, 0, 0, 19, 1000);
        c.setTestBudget(40 * 1000);
        c.setTransport(19, +1, LOOPMODEBOUNCE_ID, 1, 20, 24);
        std::vector<int> got = pull(c, 0, 4, 1000);
        check(got == std::vector<int>({19, 20, 18, 17}), "bounce: jobs 19, 20, 18, 17 (the walk turns at 20)");

        gfcFrameCache r; gfcFrameTable tr; tr.reset(20);
        r.setTestPooledBytes(0);
        r.attachTrack(0, &tr);
        r.setTrackGeometry(0, 0, 0, 19, 1000);
        r.setTestBudget(20 * 1000);   // the window and the ten frames behind
        r.setTransport(10, -1, LOOPMODEONCE_ID, 1, 20, 24);
        std::vector<int> back; for (int i = 10; i >= 1; --i) back.push_back(i);
        for (int i = 11; i <= 20; ++i) back.push_back(i);
        check(pull(r, 0, 30, 1000) == back, "reverse: jobs 10, 9 .. 1 (ahead in reverse), then behind 11 .. 20, then none");
    }

    {   // Minimum ahead, pins, wake-up.
        gfcFrameCache c; gfcFrameTable t; t.reset(240);
        c.setTestPooledBytes(0);
        c.attachTrack(0, &t);
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        c.setTestBudget(0);
        c.setTransport(100, +1, LOOPMODELOOP_ID, 1, 240, 24);
        const uint64_t a0 = c.stats().admitted;
        check(pull(c, 0, 50, 1000) == seq(100, 100 + M - 1), "minimum ahead: budget 0 still yields the playhead and M-1 more");
        check(c.stats().admitted == a0 + (uint64_t)M, "minimum ahead: admitted grows by M");
        check(c.drainVictims().frames.empty(), "minimum ahead: nothing evicted");

        c.pin(0, 239, true);
        Job j{};
        const bool got = c.nextJob(0, j, ms(20));
        check(got && j.sourceIndex == 239, "pins: a pinned frame loads first at budget 0");
        if (got) c.commit(j, 1000, true);
        check(t.pinned(239) && c.score(0, 239) == INT_MAX, "pins: pinned on the table and never a victim");
        check(c.drainVictims().frames.empty(), "pins: the pinned load evicted nothing");
        t.setPinned(200, true);   // straight on the table, as hold-edge does
        const bool got2 = c.nextJob(0, j, ms(20));
        check(got2 && j.sourceIndex == 200, "pins: a pin set on the table directly loads first");
        if (got2) c.commit(j, 1000, true);
        check(c.score(0, 200) == INT_MAX, "pins: a table pin is never a victim");

        Job w{}; bool wok = false;
        const auto t0 = std::chrono::steady_clock::now();
        std::thread th([&] { wok = c.nextJob(0, w, ms(3000)); });
        std::this_thread::sleep_for(ms(30));
        c.setTestBudget(100 * 1000);
        th.join();
        const auto took = std::chrono::duration_cast<ms>(std::chrono::steady_clock::now() - t0).count();
        check(wok && w.timelineFrame == 100 + M && took < 1000, "wake: a waiting nextJob gets a job soon after the budget grows");
    }

    {   // Two tracks with offsets; no footage.
        gfcFrameCache c; gfcFrameTable t0, t1; t0.reset(240); t1.reset(240);
        c.setTestPooledBytes(0);
        c.attachTrack(0, &t0); c.attachTrack(1, &t1);
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        c.setTrackGeometry(1, 10, 0, 239, 1000);
        c.setTestBudget(10 * 1000);
        c.setTransport(100, +1, LOOPMODELOOP_ID, 1, 240, 24);
        Job j{};
        const bool got = c.nextJob(1, j, ms(20));
        check(got && j.track == 1 && j.sourceIndex == 89 && j.timelineFrame == 100, "offsets: track 1 (offset 10) first job is source 89 at timeline 100");
        check(c.costPerTimelineFrame() == 2000, "offsets: costPerTimelineFrame sums both tracks");
        if (got) c.commit(j, 1000, true);
        c.setTrackGeometry(1, 0, -1, -1, 0);
        const auto s = std::chrono::steady_clock::now();
        check(!c.nextJob(1, j, ms(500)), "no footage: nextJob is false");
        check(std::chrono::steady_clock::now() - s < ms(100), "no footage: ... immediately");
        check(c.costPerTimelineFrame() == 1000, "no footage: costPerTimelineFrame drops the empty track");
    }

    {   // Mixed sizes, timeline-frame victims, pool trim.
        gfcFrameCache c; gfcFrameTable t0, t1; t0.reset(240); t1.reset(240);
        c.setTestPooledBytes(0);
        c.attachTrack(0, &t0); c.attachTrack(1, &t1);
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        c.setTrackGeometry(1, 0, 0, 239, 1000);
        c.setTrackFrameBytes(1, 4000);
        c.setTestBudget(20 * 1000);
        c.setTransport(100, +1, LOOPMODELOOP_ID, 1, 240, 24);
        check(pull(c, 0, 4, 1000) == seq(100, 103), "mixed: track 0 loads 100..103");
        check(pull(c, 1, 4, 4000) == seq(100, 103), "mixed: track 1 loads 100..103");
        check(c.cacheBytes() == 20000, "mixed: cacheBytes is 20000");
        c.advance(103);
        Job j{};
        const bool got = c.nextJob(0, j, ms(20));
        check(got && j.timelineFrame == 104, "mixed: track 0's next job (timeline 104) is admitted");
        if (got) c.commit(j, 1000, true);
        gfcFrameCache::Victims v = c.drainVictims();
        check(!v.frames.empty(), "mixed: victims are named");
        bool lower = true; size_t bytes = 0;
        for (const auto& x : v.frames) {
            lower &= c.score(x.track, x.sourceIndex) < c.score(0, 103);
            bytes += x.track == 0 ? 1000 : 4000;
        }
        check(lower, "mixed: every victim scores below the playhead frame");
        check(bytes >= 1000, "mixed: the victims cover the 1000-byte shortfall");
        check(v.frames.size() == 2 && v.frames[0].sourceIndex == 99 && v.frames[1].sourceIndex == 99, "mixed: timeline 100 leaves both tracks together");
        check(!v.trimPools, "mixed: no trim while pooled bytes are 0");
        c.setTestPooledBytes(c.costPerTimelineFrame() + 1);
        check(c.drainVictims().trimPools, "mixed: trimPools once pooled bytes exceed costPerTimelineFrame");
        check(c.cacheBytes() == (int64_t)(t0.residentBytes() + t1.residentBytes() + t0.reservedBytes() + t1.reservedBytes() + c.costPerTimelineFrame() + 1), "mixed: pooled bytes count in cacheBytes");
        c.setTestPooledBytes(c.costPerTimelineFrame());
        check(!c.drainVictims().trimPools, "mixed: no trim at exactly costPerTimelineFrame");
    }

    {   // A timeline frame one track holds is finished on the others even when the
        // pools grew in between (no lower victims left): the tracks stay identical.
        gfcFrameCache c; gfcFrameTable t0, t1; t0.reset(240); t1.reset(240);
        c.setTestPooledBytes(0);
        c.attachTrack(0, &t0); c.attachTrack(1, &t1);
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        c.setTrackGeometry(1, 0, 0, 239, 1000);
        c.setTestBudget(20 * 1000);
        c.setTransport(100, +1, LOOPMODEONCE_ID, 100, 240, 24);   // nothing behind: the budget is the window
        check(pull(c, 1, 10, 1000) == seq(100, 109), "finish: track 1 loads 100..109");
        check(pull(c, 0, 9, 1000) == seq(100, 108), "finish: track 0 loads 100..108");
        c.setTestPooledBytes(500);   // a staging buffer appeared
        Job j{};
        const bool got = c.nextJob(0, j, ms(20));
        check(got && j.timelineFrame == 109, "finish: track 0 still gets timeline 109, which track 1 holds");
        if (got) c.commit(j, 1000, true);
        check(!c.nextJob(0, j, ms(20)) && !c.nextJob(1, j, ms(20)), "finish: nothing further is admitted over the budget");
        check(c.cacheBytes() <= 20 * 1000 + 1000, "finish: at most one frame over the budget");
    }

    {   // Finishing the playhead's own frame (score 4N) takes victims only below
        // the window band: never a window frame another track still has to finish.
        gfcFrameCache c; gfcFrameTable t0, t1; t0.reset(240); t1.reset(240);
        c.setTestPooledBytes(0);
        c.attachTrack(0, &t0); c.attachTrack(1, &t1);
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        c.setTrackGeometry(1, 0, 0, 239, 1000);
        c.setTestBudget(20 * 1000);
        c.setTransport(100, +1, LOOPMODEONCE_ID, 100, 240, 24);
        check(pull(c, 1, 10, 1000) == seq(100, 109) && pull(c, 0, 10, 1000) == seq(100, 109), "playhead finish: both tracks load 100..109");
        t0.setEvicting(99); t0.setUnloaded(99);   // track 0 lost the playhead frame
        c.setTestPooledBytes(500);                 // and the pools grew
        Job j{};
        const bool got = c.nextJob(0, j, ms(20));
        check(got && j.timelineFrame == 100, "playhead finish: track 0 gets timeline 100 back");
        if (got) c.commit(j, 1000, true);
        const gfcFrameCache::Victims v = c.drainVictims();
        check(v.frames.empty(), "playhead finish: no window frame is evicted for it");
    }

    {   // A finish refused outside the window unloads the other track's copy.
        gfcFrameCache c; gfcFrameTable t0, t1; t0.reset(240); t1.reset(240);
        c.setTestPooledBytes(0);
        c.attachTrack(0, &t0); c.attachTrack(1, &t1);
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        c.setTrackGeometry(1, 0, 0, 239, 1000);
        c.setTestBudget(20 * 1000);
        c.setTransport(100, +1, LOOPMODELOOP_ID, 91, 109, 24);   // ahead 100..105 (window 5), behind 99..96
        std::vector<int> want = seq(100, 105);
        for (int i = 99; i >= 96; --i) want.push_back(i);
        check(pull(c, 1, 10, 1000) == want && pull(c, 0, 10, 1000) == want, "drop: both tracks load 100..105 and 99..96");
        t1.setDecoding(94, 1000); t1.setResident(94, 1000);   // track 1 alone holds timeline 95 (near behind)
        Job j{};
        check(!c.nextJob(0, j, ms(20)), "drop: track 0's finish of 95 is refused (over budget, nothing lower)");
        const gfcFrameCache::Victims v = c.drainVictims();
        check(v.frames.size() == 1 && v.frames[0].track == 1 && v.frames[0].sourceIndex == 94 &&
              t1.state(94) == gfcFrameState::Evicting, "drop: track 1's copy of 95 becomes the victim");
    }

    {   // Four tracks, pools grown by more than a frame while track 0 lags:
        // every timeline frame the others hold is still finished on track 0.
        gfcFrameCache c; gfcFrameTable tt[4];
        c.setTestPooledBytes(0);
        for (int k = 0; k < 4; ++k) {
            tt[k].reset(240);
            c.attachTrack(k, &tt[k]);
            c.setTrackGeometry(k, 0, 0, 239, 1000);
        }
        c.setTestBudget(40 * 1000);   // 10 timeline frames of 4 tracks
        c.setTransport(100, +1, LOOPMODEONCE_ID, 100, 240, 24);   // nothing behind: the budget is the window
        for (int k = 3; k >= 1; --k) check(pull(c, k, 10, 1000) == seq(100, 109), "four: tracks 3..1 load 100..109");
        check(pull(c, 0, 6, 1000) == seq(100, 105), "four: track 0 loads 100..105");
        c.setTestPooledBytes(3500);   // pools grew by three and a half frames
        {   const std::vector<int> got = pull(c, 0, 10, 1000);
            std::printf("CACHE-POLICY: four: track 0 then got");
            for (int f : got) std::printf(" %d", f);
            std::printf(" (cache %lld, budget %lld)\n", (long long)c.cacheBytes(), (long long)c.budgetBytes());
            check(got == seq(106, 109), "four: track 0 still finishes 106..109"); }
        Job j{};
        bool more = false;
        for (int k = 0; k < 4; ++k) more |= c.nextJob(k, j, ms(20));
        check(!more, "four: nothing new is admitted over the budget");
        bool same = true;
        for (int i = 0; i < 240; ++i) {
            const auto s0 = tt[0].state(i);
            for (int k = 1; k < 4; ++k) same &= tt[k].state(i) == s0;
        }
        check(same, "four: all tracks hold the same timeline frames");
        size_t frames = 0;
        for (int k = 0; k < 4; ++k) frames += tt[k].residentBytes() + tt[k].reservedBytes();
        check(frames <= 40 * 1000 + 1000, "four: frame bytes stay within the budget plus one frame");
    }

    {   // One for one: far over budget, an always-admitted frame frees about one frame, not every lower one.
        gfcFrameCache c; gfcFrameTable t; t.reset(240);
        c.setTestPooledBytes(0);
        c.attachTrack(0, &t);
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        c.setTestBudget(10 * 1000);
        c.setTransport(100, +1, LOOPMODEONCE_ID, 100, 240, 24);   // nothing behind yet: the budget is the window
        check(pull(c, 0, 10, 1000) == seq(100, 109), "one-for-one: 100..109 resident");
        c.setTestBudget(1000);   // far below the 10000 resident
        c.advance(105);          // 100..104 now behind, 110 is within the minimum ahead
        Job j{};
        bool got = c.nextJob(0, j, ms(20));
        check(got && j.timelineFrame == 110, "one-for-one: timeline 110 is always admitted");
        if (got) c.commit(j, 1000, true);
        gfcFrameCache::Victims v = c.drainVictims();
        check(v.frames.size() == 1 && v.frames[0].sourceIndex == 99, "one-for-one: a single victim (timeline 100), not all five behind");
        for (const auto& x : v.frames) t.setUnloaded(x.sourceIndex);
        c.advance(106);          // advance by one more
        got = c.nextJob(0, j, ms(20));
        check(got && j.timelineFrame == 111, "one-for-one: timeline 111 admitted after advancing by 1");
        if (got) c.commit(j, 1000, true);
        v = c.drainVictims();
        size_t vb = 0; for (const auto& x : v.frames) { vb += 1000; t.setUnloaded(x.sourceIndex); }
        check(v.frames.size() == 1 && vb == 1000 && v.frames[0].sourceIndex == 100, "one-for-one: advancing by 1 frees one frame (timeline 101)");
        check(t.residentCount() == 10, "one-for-one: the resident count holds at 10");
        check(!c.nextJob(0, j, ms(20)), "one-for-one: then nothing more is admitted");
    }

    {   // Generation, commit outcomes, cancel, derived budget.
        gfcFrameCache c; gfcFrameTable t; t.reset(240);
        c.setTestPooledBytes(0);
        c.attachTrack(0, &t);
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        c.setTestBudget(10 * 1000);
        c.setTransport(100, +1, LOOPMODELOOP_ID, 1, 240, 24);
        const uint64_t g0 = c.generation();
        c.advance(100);
        check(c.generation() == g0, "generation: advance does not bump it");
        c.retarget(100);
        check(c.generation() == g0 + 1 && c.stats().retargets == 1, "generation: retarget bumps it");

        Job j{};
        c.nextJob(0, j, ms(20));
        c.commit(j, 0, false);
        check(t.state(99) == gfcFrameState::Failed, "commit: a current failed decode is Failed");
        c.nextJob(0, j, ms(20));
        check(j.timelineFrame == 101, "commit: a Failed frame is not retried");
        c.retarget(200);
        c.commit(j, 0, false);
        check(t.state(100) == gfcFrameState::Unloaded, "commit: a stale failed decode (no longer a candidate) is Unloaded");
        c.nextJob(0, j, ms(20));
        check(j.timelineFrame == 200, "commit: the retarget moves the next job");
        c.cancel(0);
        c.commit(j, 0, false);
        check(t.state(199) == gfcFrameState::Unloaded, "commit: a cancelled track's failed decode is Unloaded");
        const auto s = std::chrono::steady_clock::now();
        check(!c.nextJob(0, j, ms(500)) && std::chrono::steady_clock::now() - s < ms(100), "cancel: nextJob is false at once");
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        check(c.nextJob(0, j, ms(20)) && j.timelineFrame == 200, "cancel: setTrackGeometry lifts the cancel");
        c.commit(j, 1000, true);

        c.setTestBudget(-1);
        const int64_t b = c.budgetBytes();
        check(b > 0 && b != 10 * 1000 && b <= (int64_t)memoryManager.totalBytes(), "budget: -1 restores a positive derived budget");
    }

    {   // The job latency estimate starts over for a new clip and after a jump
        // while paused; a jump while Keep time is skipping keeps it.
        gfcFrameCache c; gfcFrameTable t; t.reset(240);
        c.setTestPooledBytes(0);
        c.attachTrack(0, &t);
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        c.setTestBudget(100 * 1000);
        c.setTransport(100, +1, LOOPMODELOOP_ID, 1, 240, 24);
        auto oneJob = [&] {
            Job j{};
            if (!c.nextJob(0, j, ms(20))) return;
            std::this_thread::sleep_for(ms(30));
            c.commitUploaded(t, j.sourceIndex, 1000);
        };
        oneJob();
        check(c.jobLatencySeconds() > 0.02, "latency: measured from issue to upload");
        c.setLeadSkip(true);
        c.retarget(150);
        check(c.jobLatencySeconds() > 0.02, "latency: kept over a jump while skipping (playing)");
        c.setLeadSkip(false);
        c.retarget(50);
        check(c.jobLatencySeconds() == 0.0, "latency: reset by a jump while paused");
        oneJob();
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        check(c.jobLatencySeconds() == 0.0, "latency: reset by a clip load");
        oneJob();
        c.clearTrack(0);
        check(c.jobLatencySeconds() == 0.0, "latency: reset by a clear");
    }

    {   // The GL thread's upload commit: lock-free, only from Decoding.
        gfcFrameCache c; gfcFrameTable t; t.reset(4);
        t.setDecoding(1, 100);
        check(c.commitUploaded(t, 1, 120) && t.state(1) == gfcFrameState::Resident && t.residentBytes() == 120 && t.reservedBytes() == 0,
              "commitUploaded: Decoding -> Resident with the texture's bytes, reservation released");
        check(!c.commitUploaded(t, 2, 120) && t.state(2) == gfcFrameState::Unloaded && t.residentBytes() == 120,
              "commitUploaded: a frame that is not Decoding is left alone");
        t.setDecoding(3, 100);
        t.reset(4);
        check(!c.commitUploaded(t, 3, 50) && t.residentBytes() == 0 && t.residentCount() == 0,
              "commitUploaded: a frame whose table was reset while it waited is ignored");
    }

    {   // A playhead outside the loop limits (a shorter track sets them): its own
        // frame and the minimum ahead on this track load anyway, before the window.
        const int N = 100;
        gfcFrameCache c; gfcFrameTable t; t.reset(240);
        c.setTestPooledBytes(0);
        c.attachTrack(0, &t);
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        c.setTestBudget(0);
        c.setTransport(150, +1, LOOPMODELOOP_ID, 1, 100, 24);
        const std::vector<int> got = pull(c, 0, 20, 1000);
        std::printf("CACHE-POLICY: outside the limits got");
        for (int f : got) std::printf(" %d", f);
        std::printf("\n");
        check(got.size() >= (size_t)M && std::vector<int>(got.begin(), got.begin() + M) == seq(150, 150 + M - 1),
              "outside: the playhead (150) and M-1 ahead load first at budget 0, though outside the limits 1..100");
        check(c.score(0, 149) >= 3 * N, "outside: the playhead frame scores in the window band");
        check(c.score(0, 199) == 0, "outside: a frame outside the limits and away from the playhead is in the lowest band");
    }

    {   // ONCE, mid-clip, a budget that holds the clip: every frame loads, far
        // behind included, and nothing is evicted.
        gfcFrameCache c; gfcFrameTable t; t.reset(240);
        c.setTestPooledBytes(0);
        c.attachTrack(0, &t);
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        c.setTestBudget(300 * 1000);
        c.setTransport(120, +1, LOOPMODEONCE_ID, 1, 240, 24);
        pull(c, 0, 400, 1000);
        check(t.residentCount() == 240, "once fits: all 240 frames resident from a mid-clip playhead");
        check(c.stats().evicted == 0 && c.drainVictims().frames.empty(), "once fits: nothing evicted");
        const uint64_t a0 = c.stats().admitted;
        Job j{};
        check(!c.nextJob(0, j, ms(50)) && c.stats().admitted == a0, "once fits: then nothing more");
    }

    {   // The lowest band never evicts: a budget smaller than the clip fills the
        // window and near-behind, then the far-behind fill is refused, not swapped.
        gfcFrameCache c; gfcFrameTable t; t.reset(240);
        c.setTestPooledBytes(0);
        c.attachTrack(0, &t);
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        c.setTestBudget(100 * 1000);
        c.setTransport(120, +1, LOOPMODEONCE_ID, 1, 240, 24);
        pull(c, 0, 400, 1000);
        check(t.residentCount() == 100 && c.stats().evicted == 0, "fill: 100 frames at a 100-frame budget, no evictions");
        const gfcFrameCache::Stats s0 = c.stats();
        Job j{};
        for (int k = 0; k < 5; ++k) c.nextJob(0, j, ms(20));
        check(c.stats().admitted == s0.admitted && c.stats().evicted == s0.evicted, "fill: settled (no admissions, no evictions)");
    }

    {   // The derived budget with a fake system memory source. Limit L = 50 % of
        // 1 MB = 500 000 B, other processes 200 000 B, frames 2 000 B (240 of
        // them would be 480 000 B). Whether the system figure sees the cache or
        // not, the cache stays within L - other plus one frame.
        const int savedPct = sett.memoryLimitPercent;
        sett.memoryLimitPercent = 50;
        auto run = [&](bool visible) {
            gfcFrameCache c; gfcFrameTable t; t.reset(240);
            c.setTestPooledBytes(0);
            c.attachTrack(0, &t);
            c.setTrackGeometry(0, 0, 0, 239, 2000);
            if (visible) c.setTestSystemMemory([](int64_t cache) { return uint64_t(200000 + cache); }, 1000000);
            else c.setTestSystemMemory([](int64_t) { return uint64_t(200000); }, 1000000);
            c.setTransport(1, +1, LOOPMODELOOP_ID, 1, 240, 24);
            pull(c, 0, 400, 2000);
            const int64_t bytes = c.cacheBytes();
            std::printf("CACHE-POLICY: system memory %s the cache: cache %lld B, budget %lld B\n",
                        visible ? "sees" : "misses", (long long)bytes, (long long)c.budgetBytes());
            return bytes;
        };
        const int64_t hidden = run(false), seen = run(true);
        check(hidden <= 300000 + 2000, "budget: a cache the system figure misses stays within L - other + one frame");
        check(seen <= 300000 + 2000 && seen >= 300000 - 2000, "budget: a cache the system figure sees fills to L - other, as before");

        // Another app holds 450 000 B when the clip loads (over the limit), then
        // quits: the budget grows back to L - 200 000 without a reload.
        auto regrow = [&](bool visible) {
            auto other = std::make_shared<int64_t>(450000);
            gfcFrameCache c; gfcFrameTable t; t.reset(240);
            c.setTestPooledBytes(0);
            c.attachTrack(0, &t);
            c.setTrackGeometry(0, 0, 0, 239, 2000);
            c.setTestSystemMemory([other, visible](int64_t cache) { return uint64_t(*other + (visible ? cache : 0)); }, 1000000);
            c.setTransport(1, +1, LOOPMODELOOP_ID, 1, 240, 24);
            pull(c, 0, 400, 2000);
            *other = 200000;
            pull(c, 0, 400, 2000);
            const int64_t bytes = c.cacheBytes();
            std::printf("CACHE-POLICY: other app quit, system memory %s the cache: cache %lld B\n",
                        visible ? "sees" : "misses", (long long)bytes);
            return bytes;
        };
        const int64_t hiddenRegrow = regrow(false), seenRegrow = regrow(true);
        check(hiddenRegrow >= 300000 - 2000 && hiddenRegrow <= 300000 + 2000, "budget: grows back when another app frees memory (system misses the cache)");
        // When the system figure sees the cache, the floor can only drop to
        // other + the cache at the moment the app quit (here the 50 000 B
        // minimum), so the budget grows back to within that of L - other.
        check(seenRegrow >= 250000 - 2000 && seenRegrow <= 300000 + 2000, "budget: grows back when another app frees memory (system sees the cache)");
        sett.memoryLimitPercent = savedPct;
    }

    std::printf("CACHE-POLICY: pass=%d fail=%d\n", pass, fail);
    return fail;
}

// ---- live loaders (Task 5) ----

namespace {
constexpr size_t kFrame8 = size_t(320) * 180 * 4;   // one 8-bit 320x180 frame: 230 400 B

// Sets filename / bit depth / scale on the track's GUI, loads the preview and starts the loader.
gfcSequence* loadTrack(int track, const std::string& path, int compression, int scalePct) {
    gfcSequence* s = trackManager.getSequence(track);
    s->myGUI->setFilename(path);
    s->myGUI->setCompression(compression);
    s->myGUI->setScale(std::to_string(scalePct));
    s->loadPreview();
    trackManager.startLoadingSequence(track);
    return s;
}

// The GL half of the app's tick: victims freed, frames uploaded.
void pump(int ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        trackManager.generateTextures();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
template <class F>
void pump(int ms, F&& each) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        trackManager.generateTextures();
        each();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
template <class Pred>
bool pumpUntil(Pred pred, int ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        trackManager.generateTextures();
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return pred();
}

void clearAllTracks() {
    for (int t = 0; t < 4; ++t) {
        gfcSequence* s = trackManager.getSequence(t);
        s->setHoldMode(0);
        s->unloadAndClear();
        s->myGUI->setFilename("");
    }
    // What the app's idle trim does after 2 s: pool bytes count against the
    // budget, so a suite starts without the last suite's pooled memory.
    gfcUpload::trimIdle(0);
    gfcSurface::trimIdle(0);
}

// Timeline frames (offset folded in) resident on a track.
std::set<int> residentTimeline(gfcSequence* s) {
    std::vector<std::pair<int, int>> segs;
    s->loadedSegments(segs);
    std::set<int> out;
    for (const auto& r : segs)
        for (int i = r.first; i <= r.second; ++i) out.insert(i + 1 + s->getOffset());
    return out;
}

size_t poolBytes() {
    const gfcUpload::Stats up = gfcUpload::stats();
    return (up.stagingBytes - up.stagingHeldBytes) + up.pooledTextureBytes + gfcSurface::poolStats().pooledBytes;
}

struct SuiteGuard {   // the queue cap and thumbnails, restored on exit
    int queue = sett.maximumFramesInQueue;
    bool thumbs = sett.showThumbnails;
    SuiteGuard() { sett.maximumFramesInQueue = 100000; }
    ~SuiteGuard() {
        clearAllTracks();
        frameCache.setTestBudget(-1);
        sett.maximumFramesInQueue = queue;
        sett.showThumbnails = thumbs;
    }
};
}  // namespace

int cacheBudgetSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    SuiteGuard guard;
    clearAllTracks();
    sett.showThumbnails = true;
    const std::string beep = dir + "/cache_beep_24.mp4";

    {   // One track, 20 frames.
        const int64_t budget = int64_t(20 * kFrame8);
        frameCache.setTestBudget(budget);
        gfcSequence* a = loadTrack(0, beep, GFC_8BPC, 100);
        pump(3000);
        const int rc = trackManager.residentFrameCount();
        std::printf("CACHE-BUDGET: one track resident=%d cacheBytes=%lld budget=%lld pools=%zu\n", rc,
                    (long long)frameCache.cacheBytes(), (long long)budget, poolBytes());
        check(rc >= 19 && rc <= 21, "one track: 20 +/- 1 frames resident at a 20-frame budget");
        const int64_t bound = budget + 2 * (int64_t)frameCache.costPerTimelineFrame();
        check(frameCache.cacheBytes() <= bound, "one track: cacheBytes <= budget + 2 frames");
        bool boundOk = true; int maxResident = 0;
        auto sample = [&] {
            boundOk &= frameCache.cacheBytes() <= bound;
            maxResident = std::max(maxResident, trackManager.residentFrameCount());
        };
        frameCache.retarget(120); pump(2000, sample);
        frameCache.retarget(230); pump(2000, sample);
        std::printf("CACHE-BUDGET: retargets maxResident=%d\n", maxResident);
        check(boundOk, "retargets: every sample within budget + 2 frames");
        check(maxResident <= 22, "retargets: never more than 22 frames resident");
        bool tail = true;   // the window (230..235) and the nearest frames behind
        for (int i = 224; i <= 234; ++i) tail &= a->frameTable().state(i) == gfcFrameState::Resident;
        check(tail, "retargets: frames 225..235 resident after retarget(230)");
        check(a->frameReadyForRender(5) && a->frameReadyForRender(200), "every frame has load params (savedParams), resident or not");
        check(a->estimatedFrameBytes() == kFrame8, "estimatedFrameBytes for 8-bit 320x180");

        // A reload that captures no thumbnail keeps the old one.
        std::vector<int> reload;
        for (int i = 229; i <= 233; ++i) if (a->getThumbnail(i).w > 0) reload.push_back(i);
        check(!reload.empty(), "thumbnails captured while loading");
        sett.showThumbnails = false;
        for (int i : reload) a->evictFrame(i);
        bool back = pumpUntil([&] {
            for (int i : reload) if (a->frameTable().state(i) != gfcFrameState::Resident) return false;
            return true;
        }, 3000);
        bool thumbs = true;
        for (int i : reload) thumbs &= a->getThumbnail(i).w > 0;
        check(back, "evicted frames near the playhead are reloaded");
        check(thumbs, "a reloaded frame keeps its thumbnail when the reload captured none");
        sett.showThumbnails = true;
        clearAllTracks();
    }

    {   // Mixed sizes: A full size, B at 50 %; the budget is 12 frames of A.
        const int64_t budget = int64_t(12 * kFrame8);
        frameCache.setTestBudget(budget);
        gfcSequence* a = loadTrack(0, beep, GFC_8BPC, 100);
        gfcSequence* b = loadTrack(1, beep, GFC_8BPC, 50);
        check(b->estimatedFrameBytes() == kFrame8 / 4, "track B at 50 % estimates 57 600 B/frame");
        const int64_t bound = budget + 2 * (int64_t)(kFrame8 + kFrame8 / 4);
        bool boundOk = true;
        pump(3000, [&] { boundOk &= frameCache.cacheBytes() <= bound; });
        const std::set<int> ta = residentTimeline(a), tb = residentTimeline(b);
        std::printf("CACHE-BUDGET: mixed A=%zu B=%zu cacheBytes=%lld\n", ta.size(), tb.size(), (long long)frameCache.cacheBytes());
        check(boundOk, "mixed: every sample within budget + 2 timeline frames");
        if (ta != tb) {
            std::printf("CACHE-BUDGET: mixed A:");
            for (int f : ta) std::printf(" %d", f);
            std::printf(" B:");
            for (int f : tb) std::printf(" %d", f);
            std::printf(" playhead %d\n", playbackManager.getCurrentFrame());
        }
        check(!ta.empty() && ta == tb, "mixed: both tracks hold the same timeline frames");
        trackManager.getSequence(1)->unloadAndClear();
        trackManager.getSequence(1)->myGUI->setFilename("");
    }

    {   // Lowering the budget evicts nothing by itself; a retarget swaps one for one (R11).
        gfcSequence* a = trackManager.getSequence(0);
        pump(500);
        const int before = trackManager.residentFrameCount();
        frameCache.setTestBudget(int64_t(5 * kFrame8));
        pump(1000);
        check(trackManager.residentFrameCount() == before, "lowered budget: nothing evicted while idle");
        frameCache.retarget(50);
        int maxResident = 0;
        pump(2000, [&] { maxResident = std::max(maxResident, trackManager.residentFrameCount()); });
        bool head = true;
        for (int i = 49; i <= 54; ++i) head &= a->frameTable().state(i) == gfcFrameState::Resident;
        std::printf("CACHE-BUDGET: lowered before=%d max=%d after=%d\n", before, maxResident, trackManager.residentFrameCount());
        check(maxResident <= before, "lowered budget: the retarget never grows the cache");
        check(head, "lowered budget: the playhead and the minimum ahead (50..55) are resident");
    }

    std::printf("CACHE-BUDGET: pass=%d fail=%d\n", pass, fail);
    return fail;
}

int cacheSettleSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    SuiteGuard guard;
    clearAllTracks();

    {   // Paused on a full budget.
        frameCache.setTestBudget(int64_t(40 * kFrame8));
        loadTrack(0, dir + "/cache_beep_24.mp4", GFC_8BPC, 100);
        const bool full = pumpUntil([] { return trackManager.residentFrameCount() >= 40; }, 10000);
        check(full, "40 frames resident at a 40-frame budget");
        pump(300);
        const gfcFrameCache::Stats s0 = frameCache.stats();
        pump(2000);
        const gfcFrameCache::Stats s1 = frameCache.stats();
        check(s1.admitted == s0.admitted && s1.evicted == s0.evicted, "paused on a full budget: no loads, no evictions over 2 s");
    }

    {   // A loop that fits.
        frameCache.setTransport(1, +1, LOOPMODELOOP_ID, 1, 30, 24);
        pump(1500);
        const gfcFrameCache::Stats s0 = frameCache.stats();
        for (int round = 0; round < 10; ++round)
            for (int f = 1; f <= 30; ++f) { frameCache.advance(f); pump(5); }
        const gfcFrameCache::Stats s1 = frameCache.stats();
        check(s1.evicted == s0.evicted, "a loop that fits: no evictions over 10 loops");
        bool all = true;
        for (int i = 0; i < 30; ++i) all &= trackManager.getSequence(0)->frameTable().state(i) == gfcFrameState::Resident;
        check(all, "a loop that fits: frames 1..30 resident");
        clearAllTracks();
    }

    {   // The whole clip fits: an explicit 1 GiB budget (240 frames are 55 MB),
        // so the result doesn't depend on the host's free memory.
        frameCache.setTestBudget(int64_t(1) << 30);
        const gfcFrameCache::Stats s0 = frameCache.stats();
        gfcSequence* a = loadTrack(0, dir + "/cache_silent_24.mp4", GFC_8BPC, 100);
        const bool all = pumpUntil([&] { return a->residentCount() == 240; }, 20000);
        check(all, "whole clip: all 240 frames resident");
        check(frameCache.stats().evicted == s0.evicted, "whole clip: nothing evicted");
        bool none = true;
        for (int k = 1; k <= 240; ++k) none &= !a->framePending(k);
        check(none, "whole clip: no frame pending");
        check(trackManager.timelineFramesResident(1, 240, +1) && trackManager.timelineFrameResident(500),
              "timelineFramesResident(1, 240) and a frame with no footage count as resident");
        a->evictFrame(9);
        check(!trackManager.timelineFrameResident(10) && !trackManager.timelineFramesResident(5, 10, +1), "an evicted frame is not resident");
    }

    clearAllTracks();

    {   // Two tracks of different sizes, paused on a full budget: the finish /
        // drop-partial path settles too (no loads, no evictions over 2 s).
        frameCache.setTestBudget(int64_t(30 * (kFrame8 + kFrame8 / 4)));
        gfcSequence* a = loadTrack(0, dir + "/cache_beep_24.mp4", GFC_8BPC, 100);
        gfcSequence* b = loadTrack(1, dir + "/cache_beep_24.mp4", GFC_8BPC, 50);
        pumpUntil([&] { return a->residentCount() >= 25 && b->residentCount() >= 25; }, 10000);
        pump(1500);
        const gfcFrameCache::Stats s0 = frameCache.stats();
        pump(2000);
        const gfcFrameCache::Stats s1 = frameCache.stats();
        std::printf("CACHE-SETTLE: two tracks A=%d B=%d, over 2 s admitted %llu evicted %llu\n", a->residentCount(), b->residentCount(),
                    (unsigned long long)(s1.admitted - s0.admitted), (unsigned long long)(s1.evicted - s0.evicted));
        check(s1.admitted == s0.admitted && s1.evicted == s0.evicted, "two tracks paused on a full budget: no loads, no evictions over 2 s");
        check(residentTimeline(a) == residentTimeline(b), "two tracks paused: the same timeline frames");
        clearAllTracks();
    }

    std::printf("CACHE-SETTLE: pass=%d fail=%d\n", pass, fail);
    return fail;
}

int cacheLeaksSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    SuiteGuard guard;
    clearAllTracks();
    sett.showThumbnails = true;
    gfcUpload::trimIdle(0); gfcSurface::trimIdle(0);
    const long tex0 = gfcFrame::liveTextures(), surf0 = gfcSurface::liveSurfaces();
    const long ann0 = gfcSequence::loadAnnouncements();
    frameCache.setTestBudget(int64_t(10 * kFrame8));

    {   // Load / retarget / clear cycles: a movie (textures) and a PNG sequence (IOSurfaces on macOS).
        bool loadedEach = true, counted = true;
        for (int cycle = 0; cycle < 5; ++cycle) {
            gfcSequence* a = loadTrack(0, dir + "/cache_beep_24.mp4", GFC_8BPC, 100);
            gfcSequence* c = loadTrack(2, dir + "/cache_seq.0001.png", GFC_8BPC, 100);
            pump(1500);
            loadedEach &= a->residentCount() > 0 && c->residentCount() > 0;
            // The counters see the resident frames (PNG frames are IOSurface-backed on macOS).
            counted &= gfcFrame::liveTextures() >= tex0 + a->residentCount() + c->residentCount();
            if (gfcSurface::available() && gfcSurface::boundCount() > 0) counted &= gfcSurface::liveSurfaces() > surf0;
            frameCache.retarget(200);
            pump(1000);
            a->unloadAndClear();
            c->unloadAndClear();
        }
        check(loadedEach, "every cycle loads frames on both tracks");
        check(counted, "liveTextures (and liveSurfaces where IOSurfaces bind) count the resident frames");
        check(gfcSequence::loadAnnouncements() - ann0 == 10, "one LOAD announcement per track load, none per retarget");
    }

    {   // A broken movie: Failed frames, the loader keeps running and stops asking.
        frameCache.setTestBudget(-1);
        gfcSequence* d = loadTrack(3, dir + "/truncated.mov", GFC_8BPC, 100);
        const std::thread::id id0 = d->loaderThreadId();
        pump(3000);
        int failed = 0;
        for (int i = 0; i < d->frameTable().size(); ++i) failed += d->frameTable().state(i) == gfcFrameState::Failed;
        std::printf("CACHE-LEAKS: truncated failed=%d resident=%d\n", failed, d->residentCount());
        check(failed > 0 && d->residentCount() > 0, "truncated: some frames Failed, some resident");
        const uint64_t d1 = frameCache.stats().denied;
        pump(500);
        check(frameCache.stats().denied == d1, "truncated: no more denials once settled");
        check(id0 != std::thread::id() && d->loaderThreadId() == id0, "truncated: the loader thread keeps running");

        // R8: a decode failure that stops loading marks the frame Failed.
        d->setContinueLoadingOnError(false);
        d = loadTrack(3, dir + "/truncated.mov", GFC_8BPC, 100);
        pump(3000);
        int failedStop = 0;
        for (int i = 0; i < d->frameTable().size(); ++i) failedStop += d->frameTable().state(i) == gfcFrameState::Failed;
        check(failedStop == 1, "stop on error: exactly one frame Failed");
        check(d->loadingCanceled, "stop on error: loading stopped");
        d->setContinueLoadingOnError(true);
        d->unloadAndClear();
        d->myGUI->setFilename("");
    }

    gfcUpload::trimIdle(0); gfcSurface::trimIdle(0);
    std::printf("CACHE-LEAKS: textures %ld -> %ld, surfaces %ld -> %ld, pools %zu\n", tex0, gfcFrame::liveTextures(),
                surf0, gfcSurface::liveSurfaces(), poolBytes());
    check(gfcFrame::liveTextures() == tex0, "live textures back at the baseline");
    check(gfcSurface::liveSurfaces() == surf0, "live surfaces back at the baseline");
    check(trackManager.residentFrameCount() == 0, "nothing resident");
    check(frameCache.cacheBytes() == 0, "cacheBytes 0");
    check(poolBytes() == 0, "pool bytes 0 after the trim");

    std::printf("CACHE-LEAKS: pass=%d fail=%d\n", pass, fail);
    return fail;
}

int cacheHoldSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    SuiteGuard guard;
    clearAllTracks();
    frameCache.setTestBudget(int64_t(10 * kFrame8));
    gfcSequence* c = loadTrack(2, dir + "/cache_seq.0001.png", GFC_8BPC, 100);
    frameCache.setTransport(60, +1, LOOPMODELOOP_ID, 1, 120, 24);
    c->setHoldMode(2);
    auto edges = [&] {
        return c->frameTable().state(0) == gfcFrameState::Resident && c->frameTable().state(119) == gfcFrameState::Resident;
    };
    check(pumpUntil(edges, 3000), "hold-edge: frames 0 and 119 resident outside the window");
    frameCache.retarget(60);
    pump(2000);
    check(edges(), "hold-edge: the edges stay resident after a retarget");
    frameCache.retarget(90);   // the window moves: admissions now take victims
    const bool moved = pumpUntil([&] { return c->frameTable().state(89) == gfcFrameState::Resident; }, 3000);
    pump(500);
    check(moved && edges(), "hold-edge: the edges survive a window that evicts to move");
    check(c->residentCount() <= 12 + 2, "hold-edge: the budget still holds (10 frames, the two pins, one over)");
    const gfcFrame f = c->getFrame(200, false);
    check(f.loaded && f.fileName == dir + "/cache_seq.0120.png", "hold-edge: frame 200 shows frame 119");

    // Hold a chosen frame (mode 1): the held frame is pinned like the edges.
    c->setHoldMode(0);
    check(!c->frameTable().pinned(0) && !c->frameTable().pinned(119), "hold-edge off: the edges are unpinned");
    frameCache.retarget(90);
    pump(500);
    check(c->frameTable().state(29) != gfcFrameState::Resident, "hold-frame fixture: frame 30 is outside the window");
    c->setHoldMode(1, 30);
    check(c->frameTable().pinned(29), "hold-frame: the held frame (timeline 30) is pinned");
    check(pumpUntil([&] { return c->frameTable().state(29) == gfcFrameState::Resident; }, 3000),
          "hold-frame: the held frame becomes resident outside the window");
    frameCache.retarget(100);   // away from it: admissions take victims
    const bool movedAway = pumpUntil([&] { return c->frameTable().state(99) == gfcFrameState::Resident; }, 3000);
    pump(500);
    check(movedAway && c->frameTable().state(29) == gfcFrameState::Resident, "hold-frame: it stays resident after a retarget away");
    c->setHoldMode(1, 40);
    check(c->frameTable().pinned(39) && !c->frameTable().pinned(29), "hold-frame: changing the held frame unpins the old one");
    c->setHoldMode(0);
    check(!c->frameTable().pinned(39) && !c->frameTable().pinned(29), "hold-frame: leaving hold mode 1 unpins it");
    std::printf("CACHE-HOLD: pass=%d fail=%d\n", pass, fail);
    return fail;
}

// ---- seeks move the window (Task 6) ----

namespace {
// The transport the suites below change, put back on exit.
struct PlaybackGuard {
    int mode = playbackManager.getPlaybackMode();
    int priority = playbackManager.getLoopPriority();
    int in = playbackManager.getInPoint(), out = playbackManager.getOutPoint();
    ~PlaybackGuard() {
        if (playbackManager.isPlaying()) playbackManager.pause();
        playbackManager.setPlaybackMode(mode);
        playbackManager.setLoopPriority(priority);
        playbackManager.setDirection(1);
        for (int t = 0; t < 4; ++t) trackManager.getSequence(t)->setOffset(0);
        // Out first: setOutPoint pulls a later in point down to it.
        playbackManager.setOutPoint(out);
        playbackManager.setInPoint(in);
        playbackManager.setCurrentFrame(1);
    }
};

// Timeline frames from..to (inclusive) all resident on a track.
bool timelineRangeResident(gfcSequence* s, int from, int to) {
    const std::set<int> r = residentTimeline(s);
    for (int t = from; t <= to; ++t)
        if (!r.count(t)) return false;
    return true;
}

bool anyFailed(gfcSequence* s) {
    for (int i = 0; i < s->frameTable().size(); ++i)
        if (s->frameTable().state(i) == gfcFrameState::Failed) return true;
    return false;
}

// Size and modification time of a file; {-1, -1} when it does not exist.
std::pair<long long, long long> fileSignature(const std::string& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return {-1, -1};
    const auto when = std::filesystem::last_write_time(path, ec);
    if (ec) return {-1, -1};
    return {(long long)size, (long long)when.time_since_epoch().count()};
}
}  // namespace

int cacheRetargetSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    SuiteGuard guard;
    PlaybackGuard playback;
    clearAllTracks();
    gfcUpload::trimIdle(0); gfcSurface::trimIdle(0);
    const long tex0 = gfcFrame::liveTextures(), surf0 = gfcSurface::liveSurfaces();
    const std::string beep = dir + "/cache_beep_24.mp4";
    const int64_t budget = int64_t(20 * kFrame8);
    frameCache.setTestBudget(budget);
    playbackManager.setCurrentFrame(1);
    gfcSequence* a = loadTrack(0, beep, GFC_8BPC, 100);
    check(pumpUntil([&] { return a->residentCount() >= 20; }, 10000), "20 frames resident at a 20-frame budget");

    std::mt19937 rng(6);
    std::uniform_int_distribution<int> anyFrame(1, 240);

    {   // Random jumps: the frame jumped to arrives.
        int arrived = 0;
        for (int k = 0; k < 30; ++k) {
            const int f = anyFrame(rng);
            playbackManager.setCurrentFrame(f);
            arrived += pumpUntil([&] { return trackManager.timelineFrameResident(f); }, 1000) ? 1 : 0;
            pump(300);
        }
        std::printf("CACHE-RETARGET: random jumps arrived=%d/30\n", arrived);
        check(arrived == 30, "random jumps: every jumped-to frame is resident within 1 s");
    }

    {   // Seek storm: 200 jumps, every tenth a load-from-here (a jump too).
        const int savedCrash = sett.enableCrashRecoverySession;
        sett.enableCrashRecoverySession = 1;   // so a crash-session write would show
        const std::string crash = jefe::qt::recoverySessionPath();
        const auto sig0 = fileSignature(crash);
        const std::thread::id tid0 = a->loaderThreadId();
        const gfcAudioClip* clip0 = a->audioClip().get();
        const long ann0 = gfcSequence::loadAnnouncements();
        const uint64_t gen0 = frameCache.generation();
        const auto t0 = std::chrono::steady_clock::now();
        bool failed = false;
        for (int k = 0; k < 200; ++k) {
            const int f = anyFrame(rng);
            if (k % 10 == 9) trackManager.startLoadingSequenceAt(0, f - 1);
            else playbackManager.setCurrentFrame(f);
            pump(5);
            failed |= anyFailed(a);
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        pump(500);
        failed |= anyFailed(a);
        std::printf("CACHE-RETARGET: storm %.2f s, generation +%llu\n", secs,
                    (unsigned long long)(frameCache.generation() - gen0));
        check(secs < 2.0, "storm: 200 jumps within 2 s (no jump blocks)");
        check(tid0 != std::thread::id() && a->loaderThreadId() == tid0, "storm: the same loader thread (no join)");
        check(clip0 && a->audioClip().get() == clip0, "storm: the audio clip is never replaced");
        check(gfcSequence::loadAnnouncements() == ann0, "storm: no load announcements");
        check(fileSignature(crash) == sig0, "storm: no crash-session write");
        check(frameCache.generation() - gen0 == 200, "storm: generation grew by exactly 200");
        check(!failed, "storm: no frame Failed");
        sett.enableCrashRecoverySession = savedCrash;

        a->unloadAndClear();
        gfcUpload::trimIdle(0); gfcSurface::trimIdle(0);
        check(gfcFrame::liveTextures() == tex0 && gfcSurface::liveSurfaces() == surf0,
              "storm: live textures and surfaces back at the baseline after clear");
    }

    {   // Sequential play-through: the window follows the playhead.
        a = loadTrack(0, beep, GFC_8BPC, 100);
        playbackManager.setPlaybackMode(LOOPMODEONCE_ID);
        playbackManager.setCurrentFrame(1);
        pumpUntil([&] { return a->residentCount() >= 20; }, 5000);
        const int64_t bound = budget + 2 * (int64_t)frameCache.costPerTimelineFrame();
        std::vector<char> seen(240, 0);
        bool boundOk = true;
        std::vector<std::pair<int, int>> segs;
        playbackManager.startPlayFwd();
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (std::chrono::steady_clock::now() < end) {
            jefe::qt::tickPlaybackTiming();
            trackManager.generateTextures();
            a->loadedSegments(segs);
            for (const auto& r : segs)
                for (int i = r.first; i <= r.second && i < 240; ++i) seen[i] = 1;
            boundOk &= frameCache.cacheBytes() <= bound;
            if (playbackManager.getCurrentFrame() == 240) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const int last = playbackManager.getCurrentFrame();
        if (playbackManager.isPlaying()) playbackManager.pause();
        int missed = 0;
        for (char s : seen) missed += s ? 0 : 1;
        std::printf("CACHE-RETARGET: play-through reached %d, never resident %d\n", last, missed);
        check(last == 240, "play-through: playback reaches frame 240");
        check(missed == 0, "play-through: every frame 1..240 was resident at some point");
        check(boundOk, "play-through: every sample within budget + 2 frames");
    }

    std::printf("CACHE-RETARGET: pass=%d fail=%d\n", pass, fail);
    return fail;
}

int cacheDirectionSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    SuiteGuard guard;
    PlaybackGuard playback;
    clearAllTracks();
    frameCache.setTestBudget(int64_t(20 * kFrame8));
    playbackManager.setCurrentFrame(1);
    gfcSequence* a = loadTrack(0, dir + "/cache_beep_24.mp4", GFC_8BPC, 100);
    pumpUntil([&] { return a->residentCount() >= 20; }, 10000);
    pump(300);

    {   // Reverse, once: the window lies below the playhead.
        playbackManager.setPlaybackMode(LOOPMODEONCE_ID);
        playbackManager.setDirection(-1);
        const std::set<int> before = residentTimeline(a);
        const gfcFrameCache::Stats s0 = frameCache.stats();
        playbackManager.setCurrentFrame(200);
        playbackManager.startPlayRev();
        pump(2000);
        const std::set<int> after = residentTimeline(a);
        // At a 20-frame budget: the window (200 down to 195) and the frames
        // just behind it in reverse, above 200.
        bool within = !after.empty();
        size_t fresh = 0;
        for (int t : after) {
            within &= t >= 176 && t <= 224;
            if (!before.count(t)) ++fresh;
        }
        for (int t = 195; t <= 201; ++t) within &= after.count(t) > 0;
        const uint64_t admitted = frameCache.stats().admitted - s0.admitted;
        std::printf("CACHE-DIRECTION: reverse resident=%zu [%d..%d] admitted=%llu fresh=%zu\n", after.size(),
                    after.empty() ? 0 : *after.begin(), after.empty() ? 0 : *after.rbegin(),
                    (unsigned long long)admitted, fresh);
        check(within, "reverse: 195..200 (ahead in reverse) and 201 (just behind) resident, nothing outside [176, 224]");
        check(admitted == fresh, "reverse: every admission after the jump landed in [176, 224]");
        playbackManager.pause();
    }

    {   // Bounce inside 230..240: everything fits, nothing is evicted while it plays.
        playbackManager.setPlaybackMode(LOOPMODEBOUNCE_ID);
        playbackManager.setInPoint(230);
        playbackManager.setOutPoint(240);
        playbackManager.setDirection(1);
        playbackManager.setCurrentFrame(238);
        pump(2000);
        check(timelineRangeResident(a, 230, 240), "bounce: frames 230..240 resident");
        const uint64_t e0 = frameCache.stats().evicted;
        int lo = INT_MAX, hi = INT_MIN, turns = 0, lastDir = 1;
        playbackManager.startPlayFwd();
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < end) {
            jefe::qt::tickPlaybackTiming();
            trackManager.generateTextures();
            const int f = playbackManager.getCurrentFrame();
            lo = std::min(lo, f); hi = std::max(hi, f);
            const int d = playbackManager.getPlayPauseInfo().direction;
            if (d != lastDir) { ++turns; lastDir = d; }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        playbackManager.pause();
        std::printf("CACHE-DIRECTION: bounce frames %d..%d turns=%d evicted +%llu\n", lo, hi, turns,
                    (unsigned long long)(frameCache.stats().evicted - e0));
        check(lo == 230 && hi == 240 && turns >= 3, "bounce: playback went round the range twice");
        check(frameCache.stats().evicted == e0 && timelineRangeResident(a, 230, 240), "bounce: no evictions while it plays");
    }

    {   // Shrink the loop to 100..104, then widen it and jump to 150 (forward:
        // the bounce may have paused heading back down).
        playbackManager.setPlaybackMode(LOOPMODELOOP_ID);
        playbackManager.setDirection(1);
        playbackManager.setInPoint(100);
        playbackManager.setOutPoint(104);
        check(pumpUntil([&] { return timelineRangeResident(a, 100, 104); }, 2000), "shrink: frames 100..104 resident");
        playbackManager.setInPoint(1);
        playbackManager.setOutPoint(240);
        playbackManager.setCurrentFrame(150);
        // At a 20-frame budget: the window 150..155 and the nearest frames behind.
        const bool widened = pumpUntil([&] { return timelineRangeResident(a, 140, 155); }, 2000);
        if (!widened) {
            std::printf("CACHE-DIRECTION: widen resident:");
            for (int t : residentTimeline(a)) std::printf(" %d", t);
            const gfcFrameCache::Stats st = frameCache.stats();
            std::printf("\n  playhead=%d in=%d out=%d admitted=%llu evicted=%llu denied=%llu cache=%lld budget=%lld\n",
                        playbackManager.getCurrentFrame(), playbackManager.getInPoint(), playbackManager.getOutPoint(),
                        (unsigned long long)st.admitted, (unsigned long long)st.evicted, (unsigned long long)st.denied,
                        (long long)frameCache.cacheBytes(), (long long)frameCache.budgetBytes());
        }
        check(widened, "widen: frames 140..155 resident (100..104 do not starve them)");
    }

    std::printf("CACHE-DIRECTION: pass=%d fail=%d\n", pass, fail);
    return fail;
}

int cacheTracksSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    SuiteGuard guard;
    PlaybackGuard playback;
    clearAllTracks();
    frameCache.setTestBudget(int64_t(60 * kFrame8));
    // The loop spans every track (the shortest track, C, ends at timeline 20).
    playbackManager.setLoopPriority(GFC_LOOPPRIORITY_LONGEST);
    playbackManager.setPlaybackMode(LOOPMODELOOP_ID);
    playbackManager.setDirection(1);
    playbackManager.setCurrentFrame(60);
    // Offsets before the loads, so nothing loads at the wrong timeline place.
    jefe::qt::setTrackOffset(1, 50);
    jefe::qt::setTrackOffset(2, -100);
    gfcSequence* a = loadTrack(0, dir + "/cache_beep_24.mp4", GFC_8BPC, 100);
    gfcSequence* b = loadTrack(1, dir + "/cache_silent_24.mp4", GFC_8BPC, 100);
    gfcSequence* c = loadTrack(2, dir + "/cache_seq.0001.png", GFC_8BPC, 100);
    jefe::qt::setTrackOffset(1, 50);
    jefe::qt::setTrackOffset(2, -100);
    playbackManager.setCurrentFrame(60);
    pump(3000);
    {
        const std::set<int> ta = residentTimeline(a), tb = residentTimeline(b);
        std::printf("CACHE-TRACKS: at 60 A=%zu [%d..%d] B=%zu [%d..%d] C=%d\n", ta.size(),
                    ta.empty() ? 0 : *ta.begin(), ta.empty() ? 0 : *ta.rbegin(), tb.size(),
                    tb.empty() ? 0 : *tb.begin(), tb.empty() ? 0 : *tb.rbegin(), c->residentCount());
        // The window and the near-behind frames reach below B's first frame (51).
        std::set<int> taB;
        for (int t : ta) if (t >= 51) taB.insert(t);
        check(!tb.empty() && taB == tb, "at 60: A and B hold the same timeline frames where both have footage");
        check(ta.size() >= 25 && tb.size() >= 10 && ta.count(60) && ta.count(65) && tb.count(60), "at 60: A holds 25+ frames, B 10+, both 60");
        // C's footage ends at 20; what it holds in free room (fill) inside the
        // limits, A holds too (B has no footage there).
        bool cShared = true;
        for (int t : residentTimeline(c)) cShared &= t <= 0 || ta.count(t) > 0;
        check(cShared, "at 60: every frame C holds inside the limits A holds too");
    }
    playbackManager.setCurrentFrame(5);
    // The window 5..10 and the frames just behind it (1..4).
    const bool both = pumpUntil([&] { return timelineRangeResident(a, 1, 10) && timelineRangeResident(c, 1, 10); }, 2000);
    check(both, "at 5: A and C hold timeline frames 1..10");
    bool bNone = true;
    for (int t : residentTimeline(b)) bNone &= t > 14;
    check(bNone, "at 5: B holds nothing at 5..14 (its footage starts at 51)");

    std::printf("CACHE-TRACKS: pass=%d fail=%d\n", pass, fail);
    return fail;
}

// ---- preferences: memory limit and lag policy (Task 7) ----

int cachePrefsSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    SuiteGuard guard;
    clearAllTracks();
    // The harness touches the user's real QSettings: remember both keys.
    const char* kMem = "Engine/memoryLimitPercent";
    const char* kLag = "Engine/lagPolicy";
    QVariant savedMem, savedLag;
    { QSettings s; savedMem = s.value(kMem); savedLag = s.value(kLag); }
    const gfcSettings settSnapshot = sett;

    { gfcSettings fresh; check(fresh.memoryLimitPercent == 90 && fresh.lagPolicy == 0, "defaults: 90 % and Automatic"); }
    sett.memoryLimitPercent = 75; sett.lagPolicy = 2;
    jefe::qt::writePreferences();
    sett.memoryLimitPercent = 90; sett.lagPolicy = 0;
    jefe::qt::loadPreferences();
    check(sett.memoryLimitPercent == 75 && sett.lagPolicy == 2, "both settings round-trip through QSettings");

    // keepTime(): Automatic follows the sound; 1 and 2 force it.
    sett.lagPolicy = 0;
    loadTrack(0, dir + "/cache_beep_24.mp4", GFC_8BPC, 100);
    check(trackManager.anyTrackHasAudio() && playbackManager.keepTime(), "Automatic with a sound track: keep time");
    sett.lagPolicy = 2;
    check(!playbackManager.keepTime(), "Show every frame: never keep time");
    clearAllTracks();
    loadTrack(1, dir + "/cache_silent_24.mp4", GFC_8BPC, 100);
    sett.lagPolicy = 0;
    check(!trackManager.anyTrackHasAudio() && !playbackManager.keepTime(), "Automatic with only a silent track: show every frame");
    sett.lagPolicy = 1;
    check(playbackManager.keepTime(), "Keep time: always");
    sett.lagPolicy = 2;
    check(!playbackManager.keepTime(), "Show every frame, silent: not keep time");

    // Lowering the limit live unloads nothing by itself.
    clearAllTracks();
    loadTrack(0, dir + "/cache_beep_24.mp4", GFC_8BPC, 100);
    {   // Settle: resident count stable for 600 ms.
        int last = -1;
        auto stable = [&] { const int n = trackManager.residentFrameCount();
                            const bool same = n == last && n > 0; last = n; pump(150); return same; };
        int ok = 0;
        for (int i = 0; i < 100 && ok < 4; ++i) ok = stable() ? ok + 1 : 0;
        check(ok >= 4, "the window settles before the limit is lowered");
    }
    const int before = trackManager.residentFrameCount();
    frameCache.setTestBudget(1);   // far below current usage (bytes)
    sett.memoryLimitPercent = 50;
    pump(1000);
    check(before > 0 && trackManager.residentFrameCount() == before, "lowering the limit unloads nothing");
    frameCache.setTestBudget(-1);

    sett = settSnapshot;
    clearAllTracks();
    { QSettings s;
      if (savedMem.isValid()) s.setValue(kMem, savedMem); else s.remove(kMem);
      if (savedLag.isValid()) s.setValue(kLag, savedLag); else s.remove(kLag); }
    std::printf("CACHE-PREFS: pass=%d fail=%d\n", pass, fail);
    return fail;
}

// ---- what an unloaded frame shows (Task 8) ----

int cacheDisplaySelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    SuiteGuard guard;
    PlaybackGuard playback;
    clearAllTracks();
    const int savedLag = sett.lagPolicy;
    auto stop = [] { if (playbackManager.isPlaying()) playbackManager.pause(); };

    frameCache.setTestBudget(int64_t(10 * kFrame8));
    sett.lagPolicy = 1;   // Keep time
    playbackManager.setPlaybackMode(LOOPMODEONCE_ID);
    playbackManager.setCurrentFrame(1);
    gfcSequence* a = loadTrack(0, dir + "/cache_beep_24.mp4", GFC_8BPC, 100);
    check(pumpUntil([&] { return a->frameTable().state(0) == gfcFrameState::Resident; }, 5000), "frame 1 resident");

    {   // Keep time: the last drawn frame stays while playing; a jump shows nothing until it loads.
        const gfcFrame f1 = a->getFrame(1);
        check(f1.loaded && a->frameTable().onScreen() == 0, "a drawn frame is the on-screen frame");
        check(a->frameTable().state(149) != gfcFrameState::Resident, "fixture: frame 150 is not resident");
        playbackManager.startPlayFwd();
        const gfcFrame held = a->getFrame(150);
        check(held.loaded && held.indexNumber == 0, "keep time, playing: an unloaded frame shows the last drawn one");
        stop();
        check(!a->getFrame(150).loaded, "paused: an unloaded frame is not substituted");
        playbackManager.setCurrentFrame(200);   // a jump: generation bump
        check(!a->getFrame(200).loaded && a->framePending(200), "after a jump: nothing shown, the frame is pending");
        const bool arrived = pumpUntil([&] { return a->getFrame(200).loaded; }, 2000);
        check(arrived && !a->framePending(200), "the jumped-to frame arrives within 2 s and is no longer pending");
        check(!a->framePending(500) && !a->framePending(0) && !a->framePending(241), "no footage at the timeline frame: not pending (no Loading label)");
        // A render reading a resident frame does not move the pin or the last drawn frame.
        const bool next = pumpUntil([&] { return a->frameTable().state(200) == gfcFrameState::Resident; }, 2000);
        const int pin = a->frameTable().onScreen();
        const gfcFrame r = a->getFrame(201, true);
        check(next && pin == 199 && r.loaded && r.indexNumber == 200 && a->frameTable().onScreen() == 199,
              "a forced read of a resident frame leaves the on-screen pin alone");
        playbackManager.startPlayFwd();
        const gfcFrame h = a->getFrame(150);
        check(h.loaded && h.indexNumber == 199, "keep time still holds the frame the plate drew, not the rendered one");
        stop();
    }

    {   // Show every frame never substitutes.
        sett.lagPolicy = 2;
        check(a->frameTable().state(229) != gfcFrameState::Resident, "fixture: frame 230 is not resident");
        playbackManager.startPlayFwd();
        check(!a->getFrame(230).loaded, "show every frame: an unloaded frame is not substituted");
        stop();
    }

    {   // Forced loads (render/export) work anywhere and leave the cache alone.
        check(a->frameTable().state(119) == gfcFrameState::Unloaded, "fixture: frame 120 is unloaded");
        const gfcFrame forced = a->getFrame(120, true);
        check(forced.loaded && forced.fileName == a->framePathAt(120), "a forced load returns frame 120");
        a->cleanForcedLoad();
        check(a->frameTable().state(119) == gfcFrameState::Unloaded, "a forced load does not touch the frame table");
        check(a->frameReadyForRender(120) && a->frameReadyForRender(1) && !a->frameReadyForRender(500) && !a->frameReadyForRender(0),
              "frameReadyForRender: any frame in the clip, nothing outside");
        check(jefe::qt::isTrackFrameReady(0, 120) && !jefe::qt::isTrackFrameReady(0, 500), "isTrackFrameReady follows");
    }

    {   // The on-screen frame is never a victim.
        frameCache.setTestBudget(int64_t(3 * kFrame8));
        playbackManager.setCurrentFrame(1);
        check(pumpUntil([&] { return a->frameTable().state(0) == gfcFrameState::Resident; }, 3000), "frame 1 resident again");
        check(a->getFrame(1).loaded && a->frameTable().onScreen() == 0, "frame 1 drawn");
        playbackManager.setCurrentFrame(100);
        const bool moved = pumpUntil([&] { return a->frameTable().state(99) == gfcFrameState::Resident; }, 3000);
        pump(2000);
        check(moved && a->frameTable().state(0) == gfcFrameState::Resident, "the on-screen frame survives a window that evicts to move");
    }

    {   // Hold a chosen frame (mode 1): pending until the held frame is resident.
        check(a->frameTable().state(59) != gfcFrameState::Resident, "fixture: frame 60 is not resident");
        a->setHoldMode(1, 60);
        const gfcFrame forcedHeld = a->getFrame(100, true);
        check(forcedHeld.loaded && forcedHeld.fileName == a->framePathAt(60), "hold-frame: a render force-loads the held frame");
        a->cleanForcedLoad();
        check(!a->getFrame(100).loaded && a->framePending(100), "hold-frame: the held frame not yet resident is pending");
        const bool held = pumpUntil([&] { return a->getFrame(100).loaded; }, 3000);
        check(held && a->getFrame(100).indexNumber == 59 && !a->framePending(100), "hold-frame: the held frame arrives and is not pending");
        a->setHoldMode(0);
    }

    {   // A victim found on screen when the GL thread drains it goes back to Resident.
        gfcFrameTable t; t.reset(4);
        t.setResident(2, 100);
        const uint64_t v0 = t.version();
        check(t.setEvicting(2) && t.residentBytes() == 0 && t.residentCount() == 0, "fixture: frame 2 Evicting, its bytes released");
        check(t.setResidentFromEvicting(2) && t.state(2) == gfcFrameState::Resident && t.residentBytes() == 100 &&
              t.residentCount() == 1 && t.bytes(2) == 100 && t.version() != v0,
              "setResidentFromEvicting: Resident again with its old bytes");
        check(!t.setResidentFromEvicting(2) && !t.setResidentFromEvicting(1) && t.residentBytes() == 100,
              "setResidentFromEvicting: ignored unless Evicting");
    }

    sett.lagPolicy = savedLag;
    std::printf("CACHE-DISPLAY: pass=%d fail=%d\n", pass, fail);
    return fail;
}

// ---- renders through the cache (Task 8 fix round 1) ----

namespace {
// Mean of R, G and B over the image, 0..255; -1 when it cannot be read.
double meanLevel(const QString& path) {
    QImage img(path);
    if (img.isNull()) return -1.0;
    img = img.convertToFormat(QImage::Format_RGB32);
    double sum = 0;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) sum += qRed(row[x]) + qGreen(row[x]) + qBlue(row[x]);
    }
    const double n = double(img.width()) * img.height() * 3;
    return n > 0 ? sum / n : -1.0;
}
}  // namespace

int cacheRenderSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    SuiteGuard guard;
    PlaybackGuard playback;
    clearAllTracks();
    jefe::qt::setAllPlatesShowPreview(false);

    const int64_t budget = int64_t(10 * kFrame8);
    frameCache.setTestBudget(budget);
    playbackManager.setPlaybackMode(LOOPMODEONCE_ID);
    playbackManager.setCurrentFrame(200);
    gfcSequence* a = loadTrack(0, dir + "/cache_beep_24.mp4", GFC_8BPC, 100);
    check(pumpUntil([&] { return timelineRangeResident(a, 200, 205); }, 10000), "the window settles at 200");
    {   // Settled: the resident set is stable for 600 ms.
        int same = 0; uint64_t v = a->loadedVersion();
        for (int i = 0; i < 100 && same < 4; ++i) { pump(150); const uint64_t n = a->loadedVersion(); same = n == v ? same + 1 : 0; v = n; }
    }
    check(a->getFrame(200).loaded, "the plate's frame at 200 is drawn");
    const int pin0 = a->frameTable().onScreen();
    std::vector<gfcFrameState> before(240);
    for (int i = 0; i < 240; ++i) before[i] = a->frameTable().state(i);
    const uint64_t gen0 = frameCache.generation();
    const gfcFrameCache::Stats s0 = frameCache.stats();

    const QString outDir = QDir(QDir::tempPath()).filePath(
        QStringLiteral("jefe_cache_render_%1").arg(QCoreApplication::applicationPid()));
    QDir(outDir).removeRecursively();
    QDir().mkpath(outDir);
    constexpr int kN = 30;
    jefe::qt::RenderParams p;
    p.quadrant = 0;
    p.format = 5;   // PNG
    p.formatString = "png";
    p.from = 1;
    p.to = kN;
    p.padding = 4;
    p.scale = 1.0f;
    p.path = outDir.toStdString();
    p.prefix = "cache_render_";
    const int64_t bound = budget + (int64_t)kFrame8;
    bool boundOk = true;
    size_t maxResident = 0;
    // The dialog renders a frame per event-loop turn, and the app's tick runs
    // in between: pump the GL half of the tick after every frame.
    const int rendered = jefe::qt::triggerSyncRender(p, [&](int, int) {
        trackManager.generateTextures();
        const size_t rb = a->frameTable().residentBytes();
        maxResident = std::max(maxResident, rb);
        boundOk &= (int64_t)rb <= bound;
    });
    pump(1000, [&] {
        const size_t rb = a->frameTable().residentBytes();
        maxResident = std::max(maxResident, rb);
        boundOk &= (int64_t)rb <= bound;
    });
    std::printf("CACHE-RENDER: rendered=%d maxResidentBytes=%zu bound=%lld\n", rendered, maxResident, (long long)bound);
    check(rendered == kN, "30 frames rendered");

    int readable = 0, black = 0;
    double first = -1, last = -1;
    for (int f = 1; f <= kN; ++f) {
        p.from = p.to = f;
        const QString path = QString::fromStdString(jefe::qt::previewRenderFilename(p));
        const double m = meanLevel(path);
        if (m >= 0) ++readable;
        if (m >= 0 && m < 10.0) { ++black; std::printf("  frame %d mean level %.1f\n", f, m); }
        if (f == 1) first = m;
        if (f == kN) last = m;
    }
    {   // Frames 1 and 30 are different pictures (testsrc2 moves), so the render did not repeat one frame.
        p.from = p.to = 1;
        const QImage i1(QString::fromStdString(jefe::qt::previewRenderFilename(p)));
        p.from = p.to = kN;
        const QImage iN(QString::fromStdString(jefe::qt::previewRenderFilename(p)));
        check(!i1.isNull() && !iN.isNull() && i1 != iN, "frames 1 and 30 differ");
    }
    std::printf("CACHE-RENDER: readable=%d black=%d level1=%.1f level30=%.1f\n", readable, black, first, last);
    check(readable == kN && black == 0, "every rendered frame has a picture (none black)");

    bool same = true, outsideUnloaded = true;
    for (int i = 0; i < 240; ++i) {
        const gfcFrameState st = a->frameTable().state(i);
        same &= st == before[i];
        if (before[i] == gfcFrameState::Unloaded) outsideUnloaded &= st == gfcFrameState::Unloaded;
    }
    check(outsideUnloaded, "every frame outside the window is still Unloaded");
    check(same, "the resident set is the same before and after");
    check(a->frameTable().onScreen() == pin0 && pin0 == 199, "the on-screen pin is the same before and after");
    check(frameCache.generation() == gen0, "the render did not retarget the cache");
    check(frameCache.stats().evicted == s0.evicted && frameCache.stats().admitted == s0.admitted, "the render loaded and evicted nothing through the cache");
    check(playbackManager.getCurrentFrame() == 200, "the playhead is back at 200");
    check(boundOk, "resident bytes stayed within budget + one frame");

    QDir(outDir).removeRecursively();
    jefe::qt::setAllPlatesShowPreview(false);
    std::printf("CACHE-RENDER: pass=%d fail=%d\n", pass, fail);
    return fail;
}

int audioHolesSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    auto fss = [](int64_t frame) { return gfcAudioClip::frameStartSample(frame, 24, 1); };
    // The first sample after `from` (within 1 s) louder than 0.05: a beep's onset.
    auto onsetAfter = [](const gfcAudioClip& c, int64_t from) -> int64_t {
        std::vector<float> s(48000 * 2);
        c.read(from, 48000, s.data());
        for (int64_t k = 0; k < 48000; ++k)
            if (std::fabs(s[2 * k]) > 0.05f || std::fabs(s[2 * k + 1]) > 0.05f) return from + k;
        return -1;
    };
    auto rmsAt = [&](const gfcAudioClip& c, int64_t frame) {
        std::vector<float> s(2000 * 2);
        c.read(fss(frame), 2000, s.data());
        double e = 0;
        for (float v : s) e += double(v) * v;
        return std::sqrt(e / double(s.size()));
    };

    {   // The clip: written anywhere, holes read as silence.
        gfcAudioClip c;
        c.reset(0);
        std::vector<float> half(48000 * 2, 0.5f);
        c.writeAt(96000, half.data(), 48000);
        c.writeAt(0, half.data(), 48000);
        check(c.covers(0, 48000) && !c.covers(48000, 48000) && c.covers(96000, 48000), "clip: written ranges covered, the gap is not");
        check(c.firstHoleFrom(0) == 48000 && c.firstHoleFrom(96000) == -1, "clip: firstHoleFrom finds the gap");
        check(c.startSample() == 0 && c.endSample() == 144000, "clip: start 0, end 144000");
        std::vector<float> out(16000 * 2, 1.f);
        c.read(40000, 16000, out.data());
        check(out[0] == 0.5f && out[2 * 7999] == 0.5f && out[2 * 8000] == 0.f && out[2 * 15999 + 1] == 0.f, "clip: a read across the hole gives zeros there");
        gfcAudioClip e;
        e.reset(1000);
        e.append(half.data(), 100);
        check(e.startSample() == 1000 && e.endSample() == 1100 && e.covers(1000, 100), "clip: append on an empty clip lands at its reset start");
        gfcAudioPeaks pk;
        pk.reset(0);
        std::vector<float> sig(480 * 2, 0.f);
        sig[0] = 0.9f;
        pk.accumulateAt(4800, sig.data(), 480);
        check(pk.peaks.size() == 11 && pk.peaks[10] > 0.89f && pk.peaks[0] == 0.f, "peaks: accumulateAt writes the bin at its absolute position");
    }

    const std::string beep = dir + "/cache_beep_24.mp4";
    {   // The audio-only reader.
        gfcMovieReader r;
        std::string err;
        r.setAudioOnly(true);
        const bool opened = r.open(beep, &err);
        check(opened && r.info().hasAudio && r.info().frameCount == 240, "reader: audio-only open");
        std::vector<float> a;
        int64_t first = -1;
        const bool got = opened && r.readAudioUntil(fss(48), &err) && r.takeAudio(a, first);
        check(got && int64_t(a.size() / 2) >= 96000 && first <= 0, "reader: readAudioUntil covers 2 s from the start");
        const bool sought = opened && r.seekAudio(200, &err) && r.readAudioUntil(fss(210), &err) && r.takeAudio(a, first);
        check(sought && first <= fss(200) && first + int64_t(a.size() / 2) >= fss(210), "reader: seekAudio lands at or before frame 200");
        gfcMovieFrame f;
        check(!r.readFrame(0, f, &err), "reader: audio-only reads no picture");
    }

    SuiteGuard guard;
    PlaybackGuard playback;
    clearAllTracks();
    frameCache.setTestBudget(int64_t(10 * kFrame8));
    playbackManager.setPlaybackMode(LOOPMODEONCE_ID);
    playbackManager.setCurrentFrame(1);

    int64_t straightOnset = -1;   // the beep at 9 s in a clip filled straight through
    {   // Sound completes although the picture never does.
        gfcSequence* a = loadTrack(0, beep, GFC_8BPC, 100);
        const bool full = pumpUntil([&] { auto c = a->audioClip(); return c && c->endSample() >= fss(239) && c->firstHoleFrom(0) == -1; }, 6000);
        std::shared_ptr<gfcAudioClip> c = a->audioClip();
        std::printf("AUDIO-HOLES: full=%d resident=%d end=%lld\n", int(full), a->residentCount(), c ? (long long)c->endSample() : -1LL);
        check(full && a->residentCount() <= 12, "the clip fills completely while the picture holds ~10 frames");
        bool beeps = c != nullptr, quiet = c != nullptr;
        for (int k = 0; c && k < 10; ++k) {
            beeps &= rmsAt(*c, 24 * k) > 0.3;
            quiet &= rmsAt(*c, 24 * k + 12) < 0.01;
        }
        check(beeps && quiet, "beeps at every second, silence between, across unloaded picture");
        straightOnset = c ? onsetAfter(*c, fss(216) - 2000) : -1;
        a->forceLoad(100); a->cleanForcedLoad();
        a->forceLoad(5); a->cleanForcedLoad();
        const std::shared_ptr<gfcMovieReader> loader = a->loaderMovieReader();
        check(loader && loader->queuedAudioFrames() == 0, "forced loads leave nothing queued in the loader's reader");
        pump(100);
        const gfcAudioPeaks pk = a->audioPeaksSnapshot();
        float b = 0;
        for (size_t i = 100; i < 104 && i < pk.peaks.size(); ++i) b = std::max(b, pk.peaks[i]);
        check(pk.peaks.size() >= 995 && b > 0.7f && pk.firstSample == 0, "peaks span the clip with the beep at bin 100");
        clearAllTracks();
    }

    {   // A jump past the fill point: sound arrives there first. Each 1 s audio
        // block takes 400 ms, so a straight fill would reach the jump only after
        // ~3.7 s, covering 3 s after ~1.2 s: arriving with 3 s still missing proves
        // the target was filled first.
        gfcSequence::setTestAudioBlockDelayMs(400);
        playbackManager.setCurrentFrame(1);
        gfcSequence* a = loadTrack(0, beep, GFC_8BPC, 100);
        playbackManager.setCurrentFrame(212);   // source frame 211; the beep at frame 216 is filled after the seek
        const int64_t at = fss(211);
        bool earlyHole = false;
        const auto jumpedAt = std::chrono::steady_clock::now();
        const bool there = pumpUntil([&] {
            auto c = a->audioClip();
            if (!c || !c->covers(at, fss(239) - at)) return false;   // the source's sound ends just short of frame 240
            earlyHole = !c->covers(fss(72), 1);
            return true;
        }, 4000);   // the jump takes ~3-5 blocks (1.2-2 s); a straight fill has 3 s covered long before it reaches 211
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - jumpedAt).count();
        std::printf("AUDIO-HOLES: jump there=%d earlyHole=%d after %.0f ms\n", int(there), int(earlyHole), ms);
        check(there && earlyHole, "jump to 212: its sound is there within 4 s while 3 s is still a hole (filled first)");
        const int64_t jumpOnset = [&] { auto c = a->audioClip(); return c ? onsetAfter(*c, fss(216) - 2000) : -1; }();
        std::printf("AUDIO-HOLES: beep onset straight=%lld jump=%lld\n", (long long)straightOnset, (long long)jumpOnset);
        check(straightOnset > 0 && jumpOnset > 0 && std::llabs(jumpOnset - straightOnset) <= 2,
              "jump: the beep onset after a seek is sample-accurate (within 2 samples of a straight fill)");
        gfcSequence::setTestAudioBlockDelayMs(0);
        const bool full = pumpUntil([&] { auto c = a->audioClip(); return c && c->endSample() >= fss(239) && c->firstHoleFrom(0) == -1; }, 6000);
        check(full, "jump: the clip is backfilled with no hole within 6 s");
        std::shared_ptr<gfcAudioClip> c = a->audioClip();
        check(c && rmsAt(*c, 216) > 0.3 && rmsAt(*c, 24) > 0.3 && rmsAt(*c, 228) < 0.01, "jump: beeps in place after the backfill");
        clearAllTracks();
    }

    {   // A read error mid-file (injected for reads starting in 3..4 s): the
        // thread skips past it and fills the rest instead of going silent.
        gfcSequence::setTestAudioReadErrorRange(fss(72), fss(96));
        playbackManager.setCurrentFrame(1);
        gfcSequence* a = loadTrack(0, beep, GFC_8BPC, 100);
        const bool rest = pumpUntil([&] { auto c = a->audioClip(); return c && c->covers(fss(120), fss(239) - fss(120)); }, 5000);
        std::shared_ptr<gfcAudioClip> c = a->audioClip();
        std::printf("AUDIO-HOLES: read error at 3 s: rest filled=%d, 0..3 s=%d, first hole %lld\n", int(rest),
                    int(c && c->covers(0, fss(72))), c ? (long long)c->firstHoleFrom(0) : -1LL);
        check(rest && c && c->covers(0, fss(72)), "read error at 3 s: the sound before it and from 5 s on is filled");
        check(c && rmsAt(*c, 168) > 0.3, "read error at 3 s: the beep at 7 s is there");
        gfcSequence::setTestAudioReadErrorRange(0, 0);
        clearAllTracks();
    }

    {   // A load into an empty track from frame 100: samples line up with the source.
        playbackManager.setCurrentFrame(1);
        gfcSequence* s = trackManager.getSequence(0);
        s->myGUI->setFilename(beep);
        s->myGUI->setCompression(GFC_8BPC);
        s->myGUI->setScale("100");
        s->loadPreview();
        trackManager.startLoadingSequenceAt(0, 100);
        const bool full = pumpUntil([&] { auto c = s->audioClip(); return c && c->endSample() >= fss(239) && c->firstHoleFrom(fss(100)) == -1; }, 6000);
        std::shared_ptr<gfcAudioClip> c = s->audioClip();
        check(full && c && c->startSample() == fss(100), "load from 100: the clip starts at frame 100's sample and fills");
        check(c && rmsAt(*c, 120) > 0.3 && rmsAt(*c, 144) > 0.3 && rmsAt(*c, 216) > 0.3 && rmsAt(*c, 108) < 0.01 && rmsAt(*c, 132) < 0.01,
              "load from 100: beeps at the source's 1 s positions");
        check(c && rmsAt(*c, 96) == 0.0, "load from 100: nothing before the clip's start");
        clearAllTracks();
    }

    std::printf("AUDIO-HOLES: pass=%d fail=%d\n", pass, fail);
    return fail;
}

// ---- when loading can't keep up (Task 10) ----

namespace {
// The app's playback tick with a fake audio device that plays in real time:
// each tick pumps the samples the wall clock says a device would have played.
struct LagRig {
    gfcFakeAudioOutput out;
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    int64_t pumped = 0;
    LagRig() { audioEngine.setOutput(&out); out.open(&audioEngine); }
    ~LagRig() { audioEngine.setOutput(nullptr); }
    void tick() {
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const int64_t want = int64_t(secs * gfcAudioEngine::kRate);
        if (want > pumped) { out.pump(int(want - pumped)); pumped = want; }
        jefe::qt::tickPlaybackTiming();
        trackManager.generateTextures();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
};

// Loads cache_beep_24.mp4 on A at a 20-frame budget with an ~12 fps loader,
// once mode from frame 1; waits for frame 1 and 5 s of sound in the engine.
gfcSequence* lagFixture(const std::string& dir, LagRig& rig) {
    frameCache.setTestBudget(int64_t(20 * kFrame8));
    playbackManager.setPlaybackMode(LOOPMODEONCE_ID);
    playbackManager.setCurrentFrame(1);
    gfcSequence::setTestDecodeDelayMs(80);
    gfcSequence* a = loadTrack(0, dir + "/cache_beep_24.mp4", GFC_8BPC, 100);
    const int64_t fiveSecs = gfcAudioClip::frameStartSample(120, 24, 1);
    const bool ready = pumpUntil([&] {
        auto c = a->audioClip();
        return trackManager.timelineFrameResident(1) && audioEngine.trackHasAudio(0) && c && c->covers(0, fiveSecs);
    }, 5000);
    if (!ready) std::printf("  note: lag fixture not ready (frame 1 / sound)\n");
    rig.t0 = std::chrono::steady_clock::now();
    rig.pumped = 0;
    return a;
}
}  // namespace

namespace {
// One Keep-time play of `secs` seconds from `from` (sound leading, plate drawing every tick).
struct KeepTimeRun {
    int start = 0, end = 0, shown = 0;
    uint64_t admitted = 0;
    bool monotonic = true, led = false, ledInTime = false, stayed = true;
    int lead = 0;   // the cache's lead skip at the end
    double skippedPct() const { return end > start ? 100.0 * double((end - start) - shown) / double(end - start) : 0.0; }
};
KeepTimeRun keepTimeRun(gfcSequence* a, LagRig& rig, int from, double secs) {
    KeepTimeRun r;
    playbackManager.setCurrentFrame(from);
    r.start = playbackManager.getCurrentFrame();
    const uint64_t admitted0 = frameCache.stats().admitted;
    playbackManager.startPlayFwd();
    const auto t0 = std::chrono::steady_clock::now();
    int last = r.start;
    std::set<int> shown;
    for (;;) {
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (s >= secs) break;
        rig.tick();
        const int f = playbackManager.getCurrentFrame();
        r.monotonic &= f >= last;
        last = f;
        const gfcFrame drawn = a->getFrame(f);   // what the plate draws this tick
        if (drawn.loaded && drawn.indexNumber == f - 1) shown.insert(f);
        const bool leading = audioEngine.isLeading();
        if (leading && !r.led) { r.led = true; r.ledInTime = s <= 0.6; }
        else if (r.led && !leading) r.stayed = false;
    }
    r.end = playbackManager.getCurrentFrame();
    r.shown = int(shown.size());
    r.admitted = frameCache.stats().admitted - admitted0;
    r.lead = frameCache.leadSkipFrames();
    playbackManager.pause();
    return r;
}

// Play from `from` with the decoder at `delayMs`; returns when the playhead first moved (s), -1 if not within 1 s.
// `held`/`silent`: the frame held and the sound waited for the first 150 ms.
double prePlayStart(LagRig& rig, int from, int delayMs, bool& held, bool& silent) {
    gfcSequence::setTestDecodeDelayMs(delayMs);
    playbackManager.setCurrentFrame(from);
    playbackManager.startPlayFwd();
    const auto t0 = std::chrono::steady_clock::now();
    held = silent = true;
    double movedAt = -1;
    for (;;) {
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (s >= 1.0 || movedAt >= 0) break;
        rig.tick();
        const int f = playbackManager.getCurrentFrame();
        if (s < 0.15) { held &= f == from; silent &= !audioEngine.isLeading(); }
        if (f != from) movedAt = s;
    }
    playbackManager.pause();
    return movedAt;
}
}  // namespace

int cacheKeepTimeSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    SuiteGuard guard;
    PlaybackGuard playback;
    clearAllTracks();
    const int savedLag = sett.lagPolicy;
    sett.lagPolicy = 1;   // Keep time
    LagRig rig;
    gfcSequence* a = lagFixture(dir, rig);

    {   // 5 s with a loader at half the rate (80 ms a frame): sound leads, frames are skipped,
        // and what the loader makes is mostly shown (it works past the frames the sound passes first).
        const KeepTimeRun r = keepTimeRun(a, rig, 1, 5.0);
        std::printf("CACHE-KEEPTIME: 80 ms: frames %d..%d, shown %d of %d (skipped %.0f%%), admitted %llu (shown/admitted %.2f), "
                    "lead %d, latency %.0f ms, led=%d inTime=%d stayed=%d\n",
                    r.start, r.end, r.shown, r.end - r.start, r.skippedPct(), (unsigned long long)r.admitted,
                    r.admitted ? double(r.shown) / double(r.admitted) : 0.0, r.lead, frameCache.jobLatencySeconds() * 1000.0,
                    int(r.led), int(r.ledInTime), int(r.stayed));
        check(r.monotonic, "keep time: the frame sequence never goes back");
        check(r.led && r.ledInTime && r.stayed, "keep time: the sound leads within 0.6 s and keeps leading");
        check(r.shown < r.end - r.start, "keep time: frames were skipped (fewer shown than elapsed)");
        check(r.end >= 100, "keep time: time was kept (frame >= 100 after 5 s)");
        check(r.admitted > 0 && double(r.shown) >= 0.6 * double(r.admitted), "keep time: at least 60% of the frames loaded were shown");
        auto c = a->audioClip();
        check(c && c->covers(0, gfcAudioClip::frameStartSample(120, 24, 1)), "keep time: the sound has no hole in the first 5 s");
    }

    {   // ~16 fps loader (60 ms a frame): at most half the frames are skipped.
        gfcSequence::setTestDecodeDelayMs(60);
        const KeepTimeRun r = keepTimeRun(a, rig, 121, 4.5);
        std::printf("CACHE-KEEPTIME: 60 ms: frames %d..%d, shown %d of %d (skipped %.0f%%), admitted %llu (shown/admitted %.2f), lead %d, latency %.0f ms\n",
                    r.start, r.end, r.shown, r.end - r.start, r.skippedPct(), (unsigned long long)r.admitted,
                    r.admitted ? double(r.shown) / double(r.admitted) : 0.0, r.lead, frameCache.jobLatencySeconds() * 1000.0);
        check(r.monotonic && r.led && r.stayed, "keep time 60 ms: monotonic, the sound leads throughout");
        check(r.end - r.start >= 85, "keep time 60 ms: time was kept");
        check(r.skippedPct() <= 50.0, "keep time 60 ms: at most 50% of the elapsed frames skipped");
        check(r.admitted > 0 && double(r.shown) >= 0.6 * double(r.admitted), "keep time 60 ms: at least 60% of the frames loaded were shown");
        check(!frameCache.leadSkip(), "paused: the lead skip is off");
    }

    {   // Reverse, where the sound never leads (FPS pacing): Keep time still skips
        // ahead of the playhead in the play direction, so what loads is shown.
        gfcSequence::setTestDecodeDelayMs(80);
        playbackManager.setCurrentFrame(230);
        pump(300);
        const int start = playbackManager.getCurrentFrame();
        const std::set<int> before = residentTimeline(a);   // shown for free: not counted
        const uint64_t admitted0 = frameCache.stats().admitted;
        playbackManager.startPlayRev();
        const auto t0 = std::chrono::steady_clock::now();
        std::set<int> shown;
        bool skipOn = false, ledEver = false;
        for (;;) {
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (secs >= 4.0) break;
            rig.tick();
            const int f = playbackManager.getCurrentFrame();
            const gfcFrame drawn = a->getFrame(f);
            if (drawn.loaded && drawn.indexNumber == f - 1 && !before.count(f)) shown.insert(f);
            if (secs > 1.0) skipOn |= frameCache.leadSkip();
            ledEver |= audioEngine.isLeading();
        }
        const int end = playbackManager.getCurrentFrame();
        const uint64_t admitted = frameCache.stats().admitted - admitted0;
        playbackManager.pause();
        playbackManager.setDirection(1);
        std::printf("CACHE-KEEPTIME: reverse 80 ms: frames %d..%d, shown (loaded during the run) %zu, admitted %llu (shown/admitted %.2f), lead skip seen %d, led %d\n",
                    start, end, shown.size(), (unsigned long long)admitted,
                    admitted ? double(shown.size()) / double(admitted) : 0.0, int(skipOn), int(ledEver));
        check(!ledEver && skipOn, "keep time reverse: the sound never leads, the lead skip is on all the same");
        check(start - end >= 70, "keep time reverse: time was kept (70+ frames back in 4 s)");
        check(admitted > 0 && double(shown.size()) >= 0.6 * double(admitted), "keep time reverse: at least 60% of the frames loaded were shown");
        check(!frameCache.leadSkip(), "keep time reverse: paused, the lead skip is off");
    }

    {   // Skipped frames still load once the loader can.
        gfcSequence::setTestDecodeDelayMs(0);
        frameCache.setTestBudget(int64_t(60 * kFrame8));
        playbackManager.setCurrentFrame(60);
        check(pumpUntil([&] { return timelineRangeResident(a, 60, 79); }, 3000), "skipped frames load: 60..79 resident within 3 s");
        // The plate draws the playhead (as the app does), so the frame shown
        // before the jump is no longer pinned on screen and can make room.
        const bool behind = pumpUntil([&] { a->getFrame(60); return timelineRangeResident(a, 36, 59); }, 3000);
        if (!behind) {
            std::printf("CACHE-KEEPTIME: near behind missing:");
            const std::set<int> r = residentTimeline(a);
            for (int t = 30; t <= 100; ++t) if (!r.count(t)) std::printf(" %d", t);
            std::printf(" (resident %zu, cache %lld, budget %lld, pools %zu)\n", r.size(), (long long)frameCache.cacheBytes(),
                        (long long)frameCache.budgetBytes(), poolBytes());
        }
        check(behind, "near behind: 36..59 resident too (the window leaves K frames for them)");
    }

    {   // The pre-play buffer: play waits (silent) until the next ~0.25 s is in, at most 0.5 s.
        frameCache.setTestBudget(int64_t(20 * kFrame8));
        playbackManager.setCurrentFrame(1);
        pump(1000);   // settle at 1..20: the frames below are not resident
        // A start frame with none of the next six resident (earlier runs leave some behind).
        auto coldStart = [](int from, int to) {
            for (int f = from; f <= to; ++f) {
                bool cold = true;
                for (int k = 0; k < 6 && cold; ++k) cold = !trackManager.timelineFrameResident(f + k);
                if (cold) return f;
            }
            return -1;
        };
        const int slowAt = coldStart(150, 230);
        check(slowAt > 0, "fixture: a start frame in 150..230 with nothing resident");
        bool held = false, silent = false;
        const double slow = prePlayStart(rig, slowAt, 200, held, silent);   // 6 frames need ~1.2 s: the cap ends the wait
        std::printf("CACHE-KEEPTIME: pre-play 200 ms/frame from %d moved after %.3f s\n", slowAt, slow);
        check(held && silent, "pre-play: the start frame holds and the sound waits for 150 ms");
        check(slow >= 0.45 && slow <= 0.9, "pre-play: a slow loader waits out the 0.5 s cap, then plays");
        const int fastAt = coldStart(40, 140);
        check(fastAt > 0, "fixture: a start frame in 40..140 with nothing resident");
        const double fast = prePlayStart(rig, fastAt, 30, held, silent);    // 6 frames in ~0.2 s
        std::printf("CACHE-KEEPTIME: pre-play 30 ms/frame from %d moved after %.3f s\n", fastAt, fast);
        check(fast > 0 && fast < 0.5, "pre-play: play starts as soon as the buffer is in, before the 0.5 s cap");
    }

    gfcSequence::setTestDecodeDelayMs(0);
    sett.lagPolicy = savedLag;
    std::printf("CACHE-KEEPTIME: pass=%d fail=%d\n", pass, fail);
    return fail;
}

int cacheEveryFrameSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    SuiteGuard guard;
    PlaybackGuard playback;
    clearAllTracks();
    const int savedLag = sett.lagPolicy;
    sett.lagPolicy = 2;   // Show every frame
    LagRig rig;
    gfcSequence* a = lagFixture(dir, rig);

    // Plays `secs` from `from`; the plate draws every tick.
    struct Run { int start, end; float fps; bool stepOne, everLed, allResident; };
    auto run = [&](int from, double secs) {
        Run r{};
        playbackManager.setCurrentFrame(from);
        r.start = playbackManager.getCurrentFrame();
        r.stepOne = true; r.allResident = true;
        playbackManager.startPlayFwd();
        const auto t0 = std::chrono::steady_clock::now();
        bool advanced = false;
        int last = r.start;
        for (;;) {
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >= secs) break;
            rig.tick();
            const int f = playbackManager.getCurrentFrame();
            if (f != last) { r.stepOne &= f == last + 1; advanced = true; last = f; }
            a->getFrame(f);
            r.everLed |= audioEngine.isLeading();
            if (advanced) r.allResident &= trackManager.timelineFrameResident(f);
        }
        r.end = playbackManager.getCurrentFrame();
        r.fps = playbackManager.getCurrentFPS();
        playbackManager.pause();
        return r;
    };

    {   // 5 s with a loader at half the rate: playback slows to the loader.
        const Run r = run(1, 5.0);
        std::printf("CACHE-EVERYFRAME: frames %d..%d, fps %.1f, led=%d\n", r.start, r.end, r.fps, int(r.everLed));
        check(r.stepOne, "every frame: each change is exactly +1");
        check(!r.everLed, "every frame: the sound never leads");
        check(r.end >= 40 && r.end <= 90, "every frame: playback slowed to the loader (frame 40..90 after 5 s)");
        check(r.fps < 20.0f, "every frame: the measured fps shows the slowdown");
        check(r.allResident, "every frame: every frame shown was resident");
    }

    {   // A very slow loader (300 ms a frame, ~3 fps): the readout keeps following it down.
        gfcSequence::setTestDecodeDelayMs(300);
        const Run r = run(150, 8.0);
        std::printf("CACHE-EVERYFRAME: 300 ms: frames %d..%d, fps %.1f\n", r.start, r.end, r.fps);
        check(r.stepOne && !r.everLed && r.allResident, "every frame 300 ms: +1 steps, no lead, every frame resident");
        check(r.fps > 0.0f && r.fps < 6.0f, "every frame 300 ms: the measured fps drops below 6");
    }

    gfcSequence::setTestDecodeDelayMs(0);
    sett.lagPolicy = savedLag;
    std::printf("CACHE-EVERYFRAME: pass=%d fail=%d\n", pass, fail);
    return fail;
}

// ---- a playhead outside the loop limits; a clip that fits in ONCE (final review C3) ----

int cacheLimitsSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    SuiteGuard guard;
    PlaybackGuard playback;
    clearAllTracks();

    {   // A (240 frames) and B (120): the shortest track sets the loop limits to
        // 1..120. The scrubber still reaches 200, past B's end.
        frameCache.setTestBudget(int64_t(2 * 40 * kFrame8));
        playbackManager.setLoopPriority(GFC_LOOPPRIORITY_SHORTEST);
        playbackManager.setPlaybackMode(LOOPMODELOOP_ID);
        playbackManager.setCurrentFrame(1);
        gfcSequence* a = loadTrack(0, dir + "/cache_silent_24.mp4", GFC_8BPC, 100);
        gfcSequence* b = loadTrack(1, dir + "/cache_seq.0001.png", GFC_8BPC, 100);
        pumpUntil([&] { return a->residentCount() >= 30 && b->residentCount() >= 30; }, 8000);
        pump(500);
        const int shortestEnd = trackManager.getFirstLastLoaded() + 1;   // getEndLimit() under SHORTEST
        std::printf("CACHE-LIMITS: shortest track ends at %d\n", shortestEnd);
        check(shortestEnd == 120, "fixture: the loop limits end at B's last frame (120)");
        playbackManager.setCurrentFrame(200);
        const auto t0 = std::chrono::steady_clock::now();
        const bool in = pumpUntil([&] { return a->frameTable().state(199) == gfcFrameState::Resident; }, 1000);
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("CACHE-LIMITS: A's frame 200 resident=%d after %.3f s\n", int(in), took);
        check(in, "outside the limits: A's frame at the playhead (200) is resident within 1 s");
        check(!a->framePending(200) && !b->framePending(200), "outside the limits: nothing pending at 200 on either track");
        clearAllTracks();
    }

    {   // ONCE, a clip that fits, the playhead mid-clip: every frame loads, nothing
        // is evicted, and a paused player then settles.
        frameCache.setTestBudget(int64_t(1) << 30);
        playbackManager.setLoopPriority(GFC_LOOPPRIORITY_LONGEST);
        playbackManager.setPlaybackMode(LOOPMODEONCE_ID);
        playbackManager.setCurrentFrame(120);
        const gfcFrameCache::Stats s0 = frameCache.stats();
        gfcSequence* a = loadTrack(0, dir + "/cache_silent_24.mp4", GFC_8BPC, 100);
        playbackManager.setCurrentFrame(120);
        const bool all = pumpUntil([&] { return a->residentCount() == 240; }, 20000);
        std::printf("CACHE-LIMITS: once from 120: resident %d\n", a->residentCount());
        check(all, "once fits: every frame resident, far behind included (1..95)");
        check(frameCache.stats().evicted == s0.evicted, "once fits: nothing evicted");
        pump(300);
        const gfcFrameCache::Stats s1 = frameCache.stats();
        pump(2000);
        const gfcFrameCache::Stats s2 = frameCache.stats();
        check(s2.admitted == s1.admitted && s2.evicted == s1.evicted, "once fits: paused 2 s, no loads and no evictions");
    }

    std::printf("CACHE-LIMITS: pass=%d fail=%d\n", pass, fail);
    return fail;
}

// ---- just behind is kept once the budget binds (final review I3) ----

int cacheBehindSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    SuiteGuard guard;
    PlaybackGuard playback;
    clearAllTracks();

    {   // Just behind is kept once the budget binds: play forward on a clip longer
        // than the budget (60 frames), pause, and a step back is already loaded.
        const int savedLag = sett.lagPolicy;
        sett.lagPolicy = 2;   // Show every frame: every frame is played in turn
        frameCache.setTestBudget(int64_t(60 * kFrame8));
        playbackManager.setPlaybackMode(LOOPMODEONCE_ID);
        playbackManager.setCurrentFrame(1);
        gfcSequence* a = loadTrack(0, dir + "/cache_silent_24.mp4", GFC_8BPC, 100);
        pumpUntil([&] { return a->residentCount() >= 50; }, 8000);
        playbackManager.startPlayFwd();
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(4000);
        while (std::chrono::steady_clock::now() < end) {
            jefe::qt::tickPlaybackTiming();
            trackManager.generateTextures();
            a->getFrame(playbackManager.getCurrentFrame());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        playbackManager.pause();
        pump(1500);
        const int ph = playbackManager.getCurrentFrame();
        const int K = 24;
        const bool behind = timelineRangeResident(a, ph - K, ph - 1);
        std::printf("CACHE-BEHIND: paused at %d after play, %d..%d resident=%d, resident %d\n", ph, ph - K, ph - 1, int(behind), a->residentCount());
        check(ph > 60, "near behind: playback went past the 60-frame budget");
        check(behind, "near behind: frames playhead-1 .. playhead-K resident after play and pause");
        playbackManager.oneFrameRev();
        const int back = playbackManager.getCurrentFrame();
        check(back == ph - 1 && a->getFrame(back).loaded, "near behind: one frame back shows a loaded frame at once");
        sett.lagPolicy = savedLag;
    }

    std::printf("CACHE-BEHIND: pass=%d fail=%d\n", pass, fail);
    return fail;
}

// ---- a frame that fails to decode (final review C2) ----

int cacheFailedSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    SuiteGuard guard;
    PlaybackGuard playback;
    clearAllTracks();
    const int savedLag = sett.lagPolicy;

    // cache_seq frames 1..40 with frame 15 emptied (a zero-byte file).
    namespace fs = std::filesystem;
    const fs::path broken = fs::path(dir) / "broken";
    std::error_code ec;
    fs::remove_all(broken, ec);
    fs::create_directories(broken, ec);
    for (int f = 1; f <= 40; ++f) {
        char name[64];
        std::snprintf(name, sizeof name, "cache_seq.%04d.png", f);
        if (f == 15) { std::FILE* z = std::fopen((broken / name).string().c_str(), "wb"); if (z) std::fclose(z); }
        else fs::copy_file(fs::path(dir) / name, broken / name, fs::copy_options::overwrite_existing, ec);
    }

    frameCache.setTestBudget(int64_t(1) << 30);
    playbackManager.setPlaybackMode(LOOPMODEONCE_ID);
    playbackManager.setCurrentFrame(1);
    gfcSequence* a = loadTrack(0, (broken / "cache_seq.0001.png").string(), GFC_8BPC, 100);
    const bool failed = pumpUntil([&] { return a->frameTable().state(14) == gfcFrameState::Failed; }, 5000);
    check(failed, "fixture: frame 15 (zero bytes) is Failed");

    {   // Keep time, parked on it: settled, not pending, nothing to wait for.
        sett.lagPolicy = 1;
        playbackManager.setCurrentFrame(15);
        pump(300);
        check(!a->framePending(15), "keep time: a Failed frame is not pending (no endless Loading)");
        check(trackManager.timelineFrameResident(15) && trackManager.timelineFramesResident(13, 6, +1),
              "a Failed frame counts as settled for the residency checks");
    }

    {   // Show every frame across it: playback passes it and carries on.
        sett.lagPolicy = 2;
        playbackManager.setCurrentFrame(10);
        pump(200);
        playbackManager.startPlayFwd();
        const auto t0 = std::chrono::steady_clock::now();
        double passedAt = -1;
        for (;;) {
            const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (s >= 2.0) break;
            jefe::qt::tickPlaybackTiming();
            trackManager.generateTextures();
            a->getFrame(playbackManager.getCurrentFrame());
            if (passedAt < 0 && playbackManager.getCurrentFrame() >= 18) passedAt = s;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const int end = playbackManager.getCurrentFrame();
        playbackManager.pause();
        std::printf("CACHE-FAILED: show every frame from 10: frame 18 after %.3f s, frame %d after 2 s\n", passedAt, end);
        check(passedAt >= 0 && passedAt <= 1.0, "show every frame: playback passes the Failed frame 15 within 1 s");
        check(end >= 25, "show every frame: and carries on past it");
    }

    sett.lagPolicy = savedLag;
    clearAllTracks();
    fs::remove_all(broken, ec);
    std::printf("CACHE-FAILED: pass=%d fail=%d\n", pass, fail);
    return fail;
}

// ---- app quit (final review C1) ----

int cacheShutdownSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    using ms = std::chrono::milliseconds;
    using Job = gfcFrameCache::Job;
    SuiteGuard guard;
    PlaybackGuard playback;
    clearAllTracks();

    {   // A coordinator's waiter returns on shutdown, and nothing is handed out after it.
        gfcFrameCache c; gfcFrameTable t; t.reset(240);
        c.setTestPooledBytes(0);
        c.attachTrack(0, &t);
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        c.setTestBudget(0);
        c.setTransport(100, +1, LOOPMODELOOP_ID, 1, 240, 24);
        Job j{};
        while (c.nextJob(0, j, ms(20))) c.commit(j, 1000, true);   // the minimum ahead, then denied
        bool got = true;
        const auto t0 = std::chrono::steady_clock::now();
        std::thread th([&] { got = c.nextJob(0, j, ms(3000)); });
        std::this_thread::sleep_for(ms(50));
        c.shutdown();
        th.join();
        const auto took = std::chrono::duration_cast<ms>(std::chrono::steady_clock::now() - t0).count();
        check(!got && took < 500, "shutdown: a waiting nextJob returns false soon after");
        c.setTestBudget(1000 * 1000);
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        check(!c.nextJob(0, j, ms(50)), "shutdown: setTrackGeometry does not lift it");
    }

    {   // A movie with sound and an image sequence, playing; then the app's quit path.
        frameCache.setTestBudget(int64_t(30 * kFrame8));
        gfcSequence* a = loadTrack(0, dir + "/cache_beep_24.mp4", GFC_8BPC, 100);
        gfcSequence* b = loadTrack(1, dir + "/cache_seq.0001.png", GFC_8BPC, 100);
        pumpUntil([&] { return a->residentCount() > 0 && b->residentCount() > 0; }, 5000);
        playbackManager.setCurrentFrame(1);
        playbackManager.startPlayFwd();
        const auto end = std::chrono::steady_clock::now() + ms(500);
        while (std::chrono::steady_clock::now() < end) {
            jefe::qt::tickPlaybackTiming();
            trackManager.generateTextures();
            std::this_thread::sleep_for(ms(2));
        }
        check(a->threadsRunning() && b->threadsRunning(), "fixture: both tracks have their threads while playing");
        check(a->audioClip() != nullptr, "fixture: the movie has sound (an audio thread)");
        const auto t0 = std::chrono::steady_clock::now();
        jefe::qt::shutdownTracks();
        const auto took = std::chrono::duration_cast<ms>(std::chrono::steady_clock::now() - t0).count();
        std::printf("CACHE-SHUTDOWN: shutdownTracks took %lld ms\n", (long long)took);
        bool none = true;
        for (int k = 0; k < 4; ++k) none &= !trackManager.getSequence(k)->threadsRunning();
        check(none, "quit: no loader or audio thread left on any track");
        check(a->loaderThreadId() == std::thread::id() && b->loaderThreadId() == std::thread::id(), "quit: loader thread ids are cleared (joined)");
        Job j{};
        const auto s = std::chrono::steady_clock::now();
        check(!frameCache.nextJob(0, j, ms(500)) && std::chrono::steady_clock::now() - s < ms(100), "quit: nextJob returns false at once");
        trackManager.startLoadingSequence(0);
        check(!a->threadsRunning(), "quit: a load asked for after it starts no thread");
        if (playbackManager.isPlaying()) playbackManager.pause();
    }

    std::printf("CACHE-SHUTDOWN: pass=%d fail=%d\n", pass, fail);
    return fail;
}

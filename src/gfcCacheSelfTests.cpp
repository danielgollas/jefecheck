// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfcCacheSelfTests.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

    {   // Touching 256 MiB must show up in usedBytes().
        const uint64_t before = memoryManager.usedBytes();
        const size_t n = size_t(256) << 20;
        std::vector<uint8_t> buf(n);
        std::memset(buf.data(), 0x5a, n);
        uint64_t seen = memoryManager.usedBytes();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        seen = std::max(seen, memoryManager.usedBytes());
        volatile uint8_t keep = buf[n / 2]; (void)keep;
        check(seen >= before + (uint64_t(128) << 20), "usedBytes rises by >= 128 MiB for a touched 256 MiB");
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

    {   // Window, advance, unload order.
        const int N = 240;
        gfcFrameCache c; gfcFrameTable t0; t0.reset(240);
        c.setTestPooledBytes(0);
        c.attachTrack(0, &t0);
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        c.setTestBudget(10 * 1000);
        c.setTransport(100, +1, LOOPMODELOOP_ID, 1, 240, 24);
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
        r.setTestBudget(10 * 1000);
        r.setTransport(10, -1, LOOPMODEONCE_ID, 1, 20, 24);
        std::vector<int> back; for (int i = 10; i >= 1; --i) back.push_back(i);
        check(pull(r, 0, 30, 1000) == back, "reverse: jobs 10, 9 .. 1 and then none");
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

    {   // One for one: far over budget, an always-admitted frame frees about one frame, not every lower one.
        gfcFrameCache c; gfcFrameTable t; t.reset(240);
        c.setTestPooledBytes(0);
        c.attachTrack(0, &t);
        c.setTrackGeometry(0, 0, 0, 239, 1000);
        c.setTestBudget(10 * 1000);
        c.setTransport(100, +1, LOOPMODELOOP_ID, 1, 240, 24);
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

    std::printf("CACHE-POLICY: pass=%d fail=%d\n", pass, fail);
    return fail;
}

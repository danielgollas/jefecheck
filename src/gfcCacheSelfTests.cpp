// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfcCacheSelfTests.h"

#include <algorithm>
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

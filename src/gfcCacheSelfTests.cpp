// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfcCacheSelfTests.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "gfcStructures.h"
#include "gfcmemorymanager.h"
#include "gfcplaybackmanager.h"
#include "gfctrackmanager.h"
#include "gfcSequence.h"
#include "gfcsequencegui.h"

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

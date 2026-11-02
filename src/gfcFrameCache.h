// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// The rolling frame cache's coordinator: one per app, shared by the per-track
// loader threads. It owns the timeline window, the byte budget and the
// priority order; the loaders ask it for work (nextJob), report back (commit),
// and the GL thread frees the frames it names as victims (drainVictims).
//
// Priority (higher = kept longer), all distances in timeline frames along the
// playback walk (gfcPlaybackManager::advanceFrame under the transport's loop
// mode, limits and direction), N = endLimit - startLimit + 1:
//   a(t) = steps from the playhead forward to t, b(t) = steps from t forward
//   to the playhead. t is ahead when a <= b (or only a exists), else behind.
//   K = max(4, round(fps)) capped at N  (just behind)
//   M = max(4, ceil(0.25 * fps))        (always admitted: the playhead and M-1 ahead, a < M)
//   W = max(1, budget / costPerTimelineFrame), window = max(W, M-1) capped at N
//   pinned / on screen       -> never a victim (INT_MAX)
//   ahead, a <= window       -> 3N + (N - a)
//   behind, b <= K           -> 2N + (K - b)
//   ahead, a > window        -> N + (N - a)   (leftovers from an earlier jump)
//   behind, b > K, a <= window -> N + (N - a) (a loop the window reaches all the
//                                              way round: loaded last, after the
//                                              near-behind frames)
//   behind, b > K            -> N - b         (far behind, furthest lowest)
//   outside the limits / unreached -> -1
// Victims are whole timeline frames (every track's frame there), lowest first.
// Admission is by timeline frame too: a track's frame fits when the cache can
// also take the other tracks' unloaded frames at that timeline frame, so all
// tracks hold the same timeline frames whatever their frame sizes.
//
// Plain C++: no GL, Qt or libav. Every method is thread-safe; advance() and
// retarget() take no lock (atomic store + notify), so the playback tick may
// call them.
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

class gfcFrameTable;

class gfcFrameCache {
public:
    struct Job    { int track; int sourceIndex; int timelineFrame; uint64_t generation; size_t reservedBytes; };
    struct Victim { int track; int sourceIndex; };
    struct Victims { std::vector<Victim> frames; bool trimPools = false; };
    struct Stats  { uint64_t admitted, evicted, denied, retargets; };

    static constexpr int kMaxTracks = 4;

    void attachTrack(int track, gfcFrameTable* table);
    // Footage geometry: timeline frame F maps to source index F-1-offset, valid when firstIndex <= index <= lastIndex.
    // firstIndex < 0 means "no footage" (an empty track). frameBytes is the per-frame estimate (0 = unknown yet).
    // Also lifts a cancel().
    void setTrackGeometry(int track, int offset, int firstIndex, int lastIndex, size_t frameBytes);
    void setTrackOffset(int track, int offset);
    void setTrackFrameBytes(int track, size_t frameBytes);   // corrected from the first real upload; lock-free
    void clearTrack(int track);                               // no footage; wakes its loader
    void setTransport(int playhead, int direction, int loopMode, int startLimit, int endLimit, float fps);
    void retarget(int playhead);          // a jump: ++generation, wakes loaders
    // A playback step: no generation bump, wakes loaders. direction (+1/-1)
    // also moves the walk's heading (a BOUNCE turn in the tick); 0 keeps it.
    void advance(int playhead, int direction = 0);
    // Decode thread: the next frame to decode on this track (already set Decoding
    // with its reservation). False on timeout, cancel, no footage.
    bool nextJob(int track, Job& out, std::chrono::milliseconds wait);
    // decoded: the frame becomes Resident with actualBytes. Not decoded: Unloaded
    // when the job is stale (an older generation and no longer a candidate, or
    // the track was cancelled), Failed otherwise (not retried until the table resets).
    void commit(const Job& job, size_t actualBytes, bool decoded);
    // GL thread, lock-free (the playback tick may call it): a decoded frame
    // reached the GPU. Decoding -> Resident with the texture's bytes on the
    // track's table (its own leaf lock), then a notify. A frame that is no
    // longer Decoding (table reset, load cancelled) is left alone; returns false.
    // cacheBytes() is derived from the tables, so nothing else needs updating.
    bool commitUploaded(gfcFrameTable& table, int index, size_t bytes);
    void cancel(int track);               // makes nextJob return false now and until the next setTrackGeometry
    Victims drainVictims();               // GL thread, before uploads: frames marked Evicting since the last drain
    // Lock-free: true when drainVictims() has frames to hand out, so the
    // playback tick drains only when there is something to free.
    bool hasVictims() const { return victimsPending_.load(std::memory_order_acquire); }
    void pin(int track, int index, bool on);
    void setTestBudget(int64_t bytes);    // >= 0 replaces the derived budget; -1 restores it
    void setTestPooledBytes(size_t bytes);  // test only: replaces the pools' figure; size_t(-1) restores it
    int64_t budgetBytes() const;
    int64_t cacheBytes() const;
    uint64_t generation() const;
    Stats stats() const;
    size_t costPerTimelineFrame() const;  // sum of frameBytes over tracks with footage
    int score(int track, int sourceIndex) const;   // the priority above, for tests

private:
    struct Track {
        gfcFrameTable* table = nullptr;
        int offset = 0;
        int first = -1, last = -1;
        std::atomic<size_t> frameBytes{0};   // stored lock-free by setTrackFrameBytes
        bool cancelled = false;   // pins live on the table (the single pin store)
    };
    // The playback walk from the playhead, cached per transport.
    struct Walk {
        int playhead = 0, direction = 1, mode = -1, start = 1, end = 0;
        bool valid = false;
        std::vector<int> a, b;          // per timeline frame (index t - start), -1 = not reached
        std::vector<int> aheadOrder;    // ahead frames by a (playhead first)
        std::vector<int> behindOrder;   // behind frames by b
        std::vector<int> forwardOrder;  // every frame the forward walk reaches, by a (playhead first)
    };
    struct Params { int N, K, M, window; };   // window: ahead frames with a <= window

    // All below: mutex_ held.
    bool validTrack(int track) const { return track >= 0 && track < kMaxTracks; }
    const Walk& walk() const;
    Params params() const;
    int timelineScore(int t, const Params& p) const;   // ignores pins / on-screen
    bool hasFootage(const Track& tr) const { return tr.table && tr.first >= 0 && tr.last >= tr.first; }
    bool isCandidate(int t, const Params& p) const;
    int64_t budgetLocked() const;
    int64_t cacheBytesLocked() const;
    size_t pooledBytesLocked() const;
    size_t costLocked() const;
    enum class Scan { Job, Denied, None };
    Scan scan(int track, Job& out);
    bool admit(int timelineFrame, bool unconditional, size_t bytes);
    size_t timelineNeedLocked(int timelineFrame, int track, size_t bytes) const;
    void wake();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<uint64_t> wakeSeq_{0};
    std::atomic<uint64_t> generation_{0};
    std::atomic<int> playhead_{1};
    std::array<Track, kMaxTracks> tracks_;
    std::atomic<int> direction_{1};      // stored lock-free by advance()
    int loopMode_ = -1, startLimit_ = 1, endLimit_ = 0;
    float fps_ = 24.0f;
    mutable Walk walk_;
    std::vector<Victim> pending_;
    std::atomic<bool> victimsPending_{false};
    int64_t testBudget_ = -1;
    size_t testPooled_ = size_t(-1);
    // System memory, sampled at most every 100 ms.
    mutable uint64_t sysUsed_ = 0, sysTotal_ = 0;
    mutable std::chrono::steady_clock::time_point sysSampled_{};
    mutable bool sysValid_ = false;
    Stats stats_{0, 0, 0, 0};
    std::atomic<uint64_t> retargets_{0};
};

extern gfcFrameCache frameCache;   // defined in gfcFrameCache.cpp

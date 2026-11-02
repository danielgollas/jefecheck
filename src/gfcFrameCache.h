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
//   the playhead outside the limits, its frame and k < M after it in the play
//   direction (still outside)  -> 3N + max(0, N - k), always admitted
//   any other footage frame (outside the limits, unreached) -> 0
// Frames that are not window, near-behind or loop candidates (ahead past the
// window, far behind, the score-0 band) are "fill": loaded into free room
// only, never evicting, so a clip that fits loads entirely.
// Victims are whole timeline frames (every track's frame there), lowest first.
// Admission is by timeline frame too: a track's frame fits when the cache can
// also take the other tracks' unloaded frames at that timeline frame, so all
// tracks hold the same timeline frames whatever their frame sizes.
//
// Plain C++: no GL, Qt or libav. Every method is thread-safe; advance() and
// retarget() take no lock (atomic store + notify), so the playback tick may
// call them.
#include <array>
#include <climits>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
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
    // Playing under Keep time (whichever clock leads): the ahead pass skips the frames the
    // playhead will pass before a job could finish (a < ceil(latency x fps) + 1,
    // at most window - 1). They stay candidates and load later (near-behind,
    // or once the lead is cleared). Lock-free; set by the playback manager.
    void setLeadSkip(bool on) { leadSkip_.store(on, std::memory_order_release); if (on) wake(); }
    bool leadSkip() const { return leadSkip_.load(std::memory_order_acquire); }
    // Average time from nextJob handing a job out to commitUploaded (decode +
    // upload wait), seconds; 0 until a job has finished.
    double jobLatencySeconds() const { return latencyAvg_.load(std::memory_order_acquire); }
    int leadSkipFrames() const;           // the current skip (0 when off), for tests
    void cancel(int track);               // makes nextJob return false now and until the next setTrackGeometry
    // App quit: every nextJob returns false at once, now and from then on
    // (setTrackGeometry no longer lifts it). Wakes the waiting loaders.
    void shutdown();
    // GL thread: a drained victim was kept (it is on screen) and put back to
    // Resident; it no longer counts in stats().evicted.
    void evictionCancelled();
    Victims drainVictims();               // GL thread, before uploads: frames marked Evicting since the last drain
    // Lock-free: true when drainVictims() has frames to hand out, so the
    // playback tick drains only when there is something to free.
    bool hasVictims() const { return victimsPending_.load(std::memory_order_acquire); }
    void pin(int track, int index, bool on);
    void setTestBudget(int64_t bytes);    // >= 0 replaces the derived budget; -1 restores it
    void setTestPooledBytes(size_t bytes);  // test only: replaces the pools' figure; size_t(-1) restores it
    // Test only: a fake system memory source, used(cacheBytes) and total,
    // sampled at every budget computation; an empty function restores the real one.
    void setTestSystemMemory(std::function<uint64_t(int64_t cacheBytes)> used, uint64_t totalBytes);
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
    // t is the playhead's own frame or one of the next M-1 in the play
    // direction, all outside the limits: the step k from the playhead, else -1.
    int playheadRunStep(int t, const Params& p) const;
    bool hasFootage(const Track& tr) const { return tr.table && tr.first >= 0 && tr.last >= tr.first; }
    bool isCandidate(int t, const Params& p) const;
    int64_t budgetLocked() const;
    int64_t cacheBytesLocked() const;
    size_t pooledBytesLocked() const;
    size_t costLocked() const;
    enum class Scan { Job, Denied, None };
    Scan scan(int track, Job& out);
    // victimCeiling: victims must also score below it (finishing a timeline
    // frame another track already holds takes victims from lower bands only).
    bool admit(int timelineFrame, bool unconditional, size_t bytes, int victimCeiling = INT_MAX);
    void dropPartialLocked(int timelineFrame, int track);   // a refused finish: unload the others' copies
    size_t timelineNeedLocked(int timelineFrame, int track, size_t bytes) const;
    // Another track holds (Resident or Decoding) its frame at this timeline frame.
    bool timelineHeldElsewhereLocked(int timelineFrame, int track) const;
    void wake();
    int leadSkipLocked(const Params& p) const;   // frames skipped ahead (0 when off)
    void noteIssued(int track, int index);       // mutex_ held: a job handed out

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
    // The other processes' memory (systemUsed - cacheBytes) from the sample
    // taken with the least cache, below 10 % of the limit, since the last
    // clip load or clear: a floor for that term, so a cache the system figure
    // does not see (GPU memory) cannot grow the budget without bound.
    mutable int64_t otherFloor_ = -1;
    mutable int64_t otherFloorCache_ = INT64_MAX;
    std::function<uint64_t(int64_t)> testUsed_;
    uint64_t testTotal_ = 0;
    Stats stats_{0, 0, 0, 0};
    std::atomic<uint64_t> retargets_{0};
    // Job latency: issue times of the last few jobs per track (the loader runs
    // ahead of the uploads, so more than one can be in flight), read lock-free
    // by commitUploaded on the GL thread, which is the only writer of the average.
    static constexpr int kIssued = 8;
    struct Issued { std::atomic<int> index{-1}; std::atomic<int64_t> ns{0}; };
    std::array<std::array<Issued, kIssued>, kMaxTracks> issued_;
    std::array<std::atomic<int>, kMaxTracks> issuedNext_{};
    std::array<std::atomic<const gfcFrameTable*>, kMaxTracks> issuedTable_{};
    std::atomic<double> latencyAvg_{0.0};
    std::atomic<bool> leadSkip_{false};
    std::atomic<bool> shutdown_{false};   // the app is quitting (shutdown())
};

extern gfcFrameCache frameCache;   // defined in gfcFrameCache.cpp

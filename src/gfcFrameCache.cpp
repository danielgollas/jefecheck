// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfcFrameCache.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <utility>

#include "gfcFrameTable.h"
#include "gfcIOSurface.h"
#include "gfcStructures.h"
#include "gfcUploadPool.h"
#include "gfcmemorymanager.h"
#include "gfcplaybackmanager.h"

extern gfcSettings sett;
extern gfcMemoryManager memoryManager;

gfcFrameCache frameCache;

namespace {
using clock_t_ = std::chrono::steady_clock;
constexpr auto kSampleEvery = std::chrono::milliseconds(100);   // system memory figures
constexpr auto kRescanEvery = std::chrono::milliseconds(100);   // a waiting loader with no wake-up
constexpr auto kWaitSlice = std::chrono::milliseconds(10);      // covers a notify lost by the lock-free advance()
}  // namespace

// ---- configuration ----

void gfcFrameCache::wake() {
    wakeSeq_.fetch_add(1, std::memory_order_acq_rel);
    cv_.notify_all();
}

void gfcFrameCache::attachTrack(int track, gfcFrameTable* table) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validTrack(track)) return;
    tracks_[track].table = table;
    wake();
}

void gfcFrameCache::setTrackGeometry(int track, int offset, int firstIndex, int lastIndex, size_t frameBytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validTrack(track)) return;
    Track& tr = tracks_[track];
    tr.offset = offset;
    tr.first = firstIndex < 0 ? -1 : firstIndex;
    tr.last = firstIndex < 0 ? -1 : lastIndex;
    tr.frameBytes = frameBytes;
    tr.cancelled = false;
    wake();
}

void gfcFrameCache::setTrackOffset(int track, int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validTrack(track)) return;
    tracks_[track].offset = offset;
    wake();
}

// Lock-free: called from the GL thread on a load's first upload.
void gfcFrameCache::setTrackFrameBytes(int track, size_t frameBytes) {
    if (!validTrack(track)) return;
    tracks_[track].frameBytes.store(frameBytes, std::memory_order_release);
    wake();
}

bool gfcFrameCache::commitUploaded(gfcFrameTable& table, int index, size_t bytes) {
    const bool done = table.setResidentIfDecoding(index, bytes);
    if (done) {   // the job's latency, issue to upload (lock-free)
        const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t_::now().time_since_epoch()).count();
        for (int k = 0; k < kMaxTracks; ++k) {
            if (issuedTable_[k].load(std::memory_order_acquire) != &table) continue;
            for (Issued& e : issued_[k]) {
                if (e.index.load(std::memory_order_acquire) != index) continue;
                const int64_t at = e.ns.load(std::memory_order_acquire);
                e.index.store(-1, std::memory_order_release);
                const double secs = std::min(1.0, std::max(0.0, double(now - at) * 1e-9));
                const double avg = latencyAvg_.load(std::memory_order_acquire);
                latencyAvg_.store(avg <= 0.0 ? secs : avg + 0.2 * (secs - avg), std::memory_order_release);
                break;
            }
            break;
        }
    }
    wake();
    return done;
}

void gfcFrameCache::noteIssued(int track, int index) {
    issuedTable_[track].store(tracks_[track].table, std::memory_order_release);
    const int slot = issuedNext_[track].fetch_add(1, std::memory_order_relaxed) % kIssued;
    Issued& e = issued_[track][slot];
    e.index.store(-1, std::memory_order_release);
    e.ns.store(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t_::now().time_since_epoch()).count(),
               std::memory_order_release);
    e.index.store(index, std::memory_order_release);
}

int gfcFrameCache::leadSkipLocked(const Params& p) const {
    if (!leadSkip_.load(std::memory_order_acquire)) return 0;
    const double fps = fps_ > 0.0f ? fps_ : 24.0f;
    const int lead = (int)std::ceil(latencyAvg_.load(std::memory_order_acquire) * fps) + 1;
    return std::max(0, std::min(p.window - 1, lead));
}

int gfcFrameCache::leadSkipFrames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    walk();
    return leadSkipLocked(params());
}

void gfcFrameCache::clearTrack(int track) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validTrack(track)) return;
    Track& tr = tracks_[track];
    tr.first = tr.last = -1;
    tr.frameBytes = 0;
    // Victims named on this track belong to the footage being cleared.
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                  [track](const Victim& v) { return v.track == track; }),
                   pending_.end());
    victimsPending_.store(!pending_.empty(), std::memory_order_release);
    wake();
}

void gfcFrameCache::setTransport(int playhead, int direction, int loopMode, int startLimit, int endLimit, float fps) {
    std::lock_guard<std::mutex> lock(mutex_);
    direction_.store(direction < 0 ? -1 : 1, std::memory_order_release);
    loopMode_ = loopMode;
    startLimit_ = startLimit;
    endLimit_ = endLimit;
    fps_ = fps;
    playhead_.store(playhead, std::memory_order_release);
    wake();
}

// Lock-free: the playback tick may call these.
void gfcFrameCache::retarget(int playhead) {
    playhead_.store(playhead, std::memory_order_release);
    generation_.fetch_add(1, std::memory_order_acq_rel);
    retargets_.fetch_add(1, std::memory_order_relaxed);
    wake();
}

void gfcFrameCache::advance(int playhead, int direction) {
    if (direction != 0) direction_.store(direction < 0 ? -1 : 1, std::memory_order_release);
    playhead_.store(playhead, std::memory_order_release);
    wake();
}

void gfcFrameCache::cancel(int track) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validTrack(track)) return;
    tracks_[track].cancelled = true;
    wake();
}

void gfcFrameCache::pin(int track, int index, bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validTrack(track)) return;
    Track& tr = tracks_[track];
    if (tr.table) tr.table->setPinned(index, on);
    wake();
}

void gfcFrameCache::setTestBudget(int64_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    testBudget_ = bytes < 0 ? -1 : bytes;
    wake();
}

void gfcFrameCache::setTestPooledBytes(size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    testPooled_ = bytes;
    wake();
}

// ---- figures ----

size_t gfcFrameCache::costLocked() const {
    size_t sum = 0;
    for (const Track& tr : tracks_)
        if (hasFootage(tr)) sum += tr.frameBytes.load(std::memory_order_acquire);
    return sum;
}

size_t gfcFrameCache::pooledBytesLocked() const {
    if (testPooled_ != size_t(-1)) return testPooled_;
    const gfcUpload::Stats up = gfcUpload::stats();
    // A staging buffer held by a decoded frame waiting for upload is that
    // frame's memory, already counted by its Decoding reservation: only the
    // idle (or still in-flight after upload) buffers are pool bytes.
    return (up.stagingBytes - up.stagingHeldBytes) + up.pooledTextureBytes + gfcSurface::poolStats().pooledBytes;
}

int64_t gfcFrameCache::cacheBytesLocked() const {
    size_t sum = pooledBytesLocked();
    for (const Track& tr : tracks_)
        if (tr.table) sum += tr.table->residentBytes() + tr.table->reservedBytes();
    return (int64_t)sum;
}

// budget = limit% x total - (systemUsed - cacheBytes). A used figure of 0 means
// the system query failed: keep the previous sample.
int64_t gfcFrameCache::budgetLocked() const {
    if (testBudget_ >= 0) return testBudget_;
    const auto now = clock_t_::now();
    if (sysSampled_ == clock_t_::time_point{} || now - sysSampled_ >= kSampleEvery) {
        sysSampled_ = now;
        const uint64_t used = memoryManager.usedBytes();
        const uint64_t total = memoryManager.totalBytes();
        if (used > 0 && total > 0) { sysUsed_ = used; sysTotal_ = total; sysValid_ = true; }
    }
    const int64_t cache = cacheBytesLocked();
    const int pct = std::clamp(sett.memoryLimitPercent, 1, 100);
    if (!sysValid_) return 0;   // nothing known yet: only the minimum ahead and pins load
    const int64_t limit = (int64_t)((double)sysTotal_ * pct / 100.0);
    return limit - ((int64_t)sysUsed_ - cache);
}

int64_t gfcFrameCache::budgetBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return budgetLocked();
}

int64_t gfcFrameCache::cacheBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cacheBytesLocked();
}

uint64_t gfcFrameCache::generation() const {
    return generation_.load(std::memory_order_acquire);
}

gfcFrameCache::Stats gfcFrameCache::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats s = stats_;
    s.retargets = retargets_.load(std::memory_order_relaxed);
    return s;
}

size_t gfcFrameCache::costPerTimelineFrame() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return costLocked();
}

// ---- policy ----

// The walk forward from the playhead (a) and the walk that reaches it (b, the
// same walk run with the direction flipped), at most N steps each. A walk stops
// when it comes back to the playhead heading the same way, or stops moving
// (ONCE at a limit).
const gfcFrameCache::Walk& gfcFrameCache::walk() const {
    const int ph = playhead_.load(std::memory_order_acquire);
    const int dir = direction_.load(std::memory_order_acquire);
    Walk& w = walk_;
    if (w.valid && w.playhead == ph && w.direction == dir && w.mode == loopMode_ &&
        w.start == startLimit_ && w.end == endLimit_)
        return w;
    w.valid = true;
    w.playhead = ph; w.direction = dir; w.mode = loopMode_; w.start = startLimit_; w.end = endLimit_;
    const int N = std::max(0, w.end - w.start + 1);
    w.a.assign(N, -1);
    w.b.assign(N, -1);
    w.aheadOrder.clear();
    w.behindOrder.clear();
    w.forwardOrder.clear();
    if (N == 0) return w;
    const int p = std::clamp(ph, w.start, w.end);
    std::vector<int> fwd, rev;
    auto run = [&](int d0, std::vector<int>& dist, std::vector<int>& order) {
        int cur = p, d = d0;
        for (int k = 1; k <= N; ++k) {
            const int nx = gfcPlaybackManager::advanceFrame(cur, d, w.mode, w.start, w.end);
            if (nx == cur || nx < w.start || nx > w.end) break;
            if (nx == p && d == d0) break;
            if (nx != p && dist[nx - w.start] < 0) { dist[nx - w.start] = k; order.push_back(nx); }
            cur = nx;
        }
    };
    w.a[p - w.start] = 0;
    run(w.direction, w.a, fwd);
    run(-w.direction, w.b, rev);
    w.aheadOrder.push_back(p);
    w.forwardOrder.push_back(p);
    w.forwardOrder.insert(w.forwardOrder.end(), fwd.begin(), fwd.end());
    for (int t : fwd) {
        const int b = w.b[t - w.start];
        if (b < 0 || w.a[t - w.start] <= b) w.aheadOrder.push_back(t);
    }
    for (int t : rev) {
        const int a = w.a[t - w.start];
        if (a < 0 || a > w.b[t - w.start]) w.behindOrder.push_back(t);
    }
    return w;
}

gfcFrameCache::Params gfcFrameCache::params() const {
    Params p{};
    p.N = std::max(0, endLimit_ - startLimit_ + 1);
    const float fps = fps_ > 0.0f ? fps_ : 24.0f;
    p.K = std::min(std::max(4, (int)std::lround(fps)), p.N);
    p.M = std::max(4, (int)std::ceil(0.25f * fps));
    const size_t cost = costLocked();
    const int64_t budget = budgetLocked();
    int64_t W;
    if (cost == 0) W = p.N;
    else W = std::max<int64_t>(1, budget / (int64_t)cost);
    p.window = (int)std::min<int64_t>(p.N, std::max<int64_t>(W, p.M - 1));
    return p;
}

int gfcFrameCache::timelineScore(int t, const Params& p) const {
    const Walk& w = walk_;
    if (p.N == 0 || t < w.start || t > w.end) return -1;
    const int a = w.a[t - w.start], b = w.b[t - w.start];
    const int N = p.N;
    if (a >= 0 && (b < 0 || a <= b)) return a <= p.window ? 3 * N + (N - a) : N + (N - a);
    if (b >= 0) {
        if (b <= p.K) return 2 * N + (p.K - b);
        if (a >= 0 && a <= p.window) return N + (N - a);   // the window reaches round the loop
        return N - b;
    }
    return -1;
}

bool gfcFrameCache::isCandidate(int t, const Params& p) const {
    const Walk& w = walk_;
    if (p.N == 0 || t < w.start || t > w.end) return false;
    const int a = w.a[t - w.start], b = w.b[t - w.start];
    if (a >= 0 && (b < 0 || a <= b)) return a <= p.window;
    return (b >= 0 && b <= p.K) || (a >= 0 && a <= p.window);
}

int gfcFrameCache::score(int track, int sourceIndex) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validTrack(track)) return -1;
    const Track& tr = tracks_[track];
    if (tr.table && (tr.table->pinned(sourceIndex) || tr.table->onScreen() == sourceIndex)) return INT_MAX;
    walk();
    return timelineScore(sourceIndex + 1 + tr.offset, params());
}

// Another track with footage at t holds its frame there (Resident or Decoding).
bool gfcFrameCache::timelineHeldElsewhereLocked(int t, int track) const {
    for (int k = 0; k < kMaxTracks; ++k) {
        if (k == track) continue;
        const Track& tr = tracks_[k];
        if (!hasFootage(tr) || tr.cancelled) continue;
        const int src = t - 1 - tr.offset;
        if (src < tr.first || src > tr.last) continue;
        const gfcFrameState st = tr.table->state(src);
        if (st == gfcFrameState::Resident || st == gfcFrameState::Decoding) return true;
    }
    return false;
}

// A finish at t was refused: the other tracks' resident copies there (not
// pinned, not on screen) become victims, so no track holds t alone.
void gfcFrameCache::dropPartialLocked(int t, int track) {
    for (int k = 0; k < kMaxTracks; ++k) {
        if (k == track) continue;
        Track& tr = tracks_[k];
        if (!hasFootage(tr) || tr.cancelled) continue;
        const int src = t - 1 - tr.offset;
        if (src < tr.first || src > tr.last) continue;
        if (src == tr.table->onScreen() || tr.table->pinned(src)) continue;
        if (tr.table->state(src) == gfcFrameState::Resident && tr.table->setEvicting(src)) {
            pending_.push_back({k, src});
            victimsPending_.store(true, std::memory_order_release);
            ++stats_.evicted;
        }
    }
}

// The bytes timeline frame t still needs: this track's frame plus every other
// track's frame there that has footage and is not loaded yet (Unloaded).
size_t gfcFrameCache::timelineNeedLocked(int t, int track, size_t bytes) const {
    size_t need = bytes;
    for (int k = 0; k < kMaxTracks; ++k) {
        if (k == track) continue;
        const Track& tr = tracks_[k];
        if (!hasFootage(tr) || tr.cancelled) continue;
        const int src = t - 1 - tr.offset;
        if (src < tr.first || src > tr.last) continue;
        if (tr.table->state(src) == gfcFrameState::Unloaded) need += tr.frameBytes.load(std::memory_order_acquire);
    }
    return need;
}

// Admits `bytes` at timeline frame t (what t still needs on every track, see
// timelineNeedLocked), naming victims (whole timeline
// frames, every track's resident frame there, lowest score first) when the
// cache would go over budget. Only frames scoring strictly below t are taken,
// and only enough to cover min(shortfall, bytes). An always-admitted frame
// (pinned, or a < M) loads even when that is not covered.
bool gfcFrameCache::admit(int t, bool unconditional, size_t bytes, int victimCeiling) {
    const Params p = params();
    const int64_t cache = cacheBytesLocked();
    const int64_t budget = budgetLocked();
    const int64_t shortfall = cache + (int64_t)bytes - budget;
    if (shortfall <= 0) return true;

    const int candScore = std::min(timelineScore(t, p), victimCeiling);   // victims score below this
    struct Group { int score = 0; size_t bytes = 0; std::vector<Victim> frames; };
    std::map<int, Group> groups;   // by timeline frame
    std::vector<std::pair<int, int>> runs;
    for (int k = 0; k < kMaxTracks; ++k) {
        const Track& tr = tracks_[k];
        if (!tr.table) continue;
        const int onScreen = tr.table->onScreen();
        tr.table->segments(runs);
        for (const auto& run : runs) {
            for (int i = run.first; i <= run.second; ++i) {
                if (i == onScreen || tr.table->state(i) != gfcFrameState::Resident || tr.table->pinned(i)) continue;
                const int tl = i + 1 + tr.offset;
                const int sc = timelineScore(tl, p);
                if (sc >= candScore) continue;
                Group& g = groups[tl];
                g.score = sc;
                g.bytes += tr.table->bytes(i);
                g.frames.push_back({k, i});
            }
        }
    }
    std::vector<Group*> order;
    for (auto& kv : groups) order.push_back(&kv.second);
    std::stable_sort(order.begin(), order.end(), [](const Group* x, const Group* y) { return x->score < y->score; });

    // One for one: free at most this frame's bytes, even when the cache is
    // further over budget than that (the owner's "swap one for one" rule).
    const int64_t need = std::min<int64_t>(shortfall, (int64_t)bytes);
    size_t taken = 0, covered = 0;
    while (taken < order.size() && (int64_t)covered < need) covered += order[taken++]->bytes;
    if ((int64_t)covered < need && !unconditional) return false;

    for (size_t g = 0; g < taken; ++g) {
        for (const Victim& v : order[g]->frames) {
            if (tracks_[v.track].table->setEvicting(v.sourceIndex)) {
                pending_.push_back(v);
                victimsPending_.store(true, std::memory_order_release);
                ++stats_.evicted;
            }
        }
    }
    return true;
}

gfcFrameCache::Scan gfcFrameCache::scan(int track, Job& out) {
    if (!validTrack(track)) return Scan::None;
    Track& tr = tracks_[track];
    if (tr.cancelled || !hasFootage(tr)) return Scan::None;
    const Walk& w = walk();
    const Params p = params();
    const int size = tr.table->size();

    // 0 = skip, 1 = job, 2 = denied
    auto attempt = [&](int src, bool pinned) -> int {
        if (src < tr.first || src > tr.last || src >= size) return 0;
        if (tr.table->state(src) != gfcFrameState::Unloaded) return 0;
        const int t = src + 1 + tr.offset;
        bool unconditional = pinned;
        if (!unconditional && p.N > 0 && t >= w.start && t <= w.end) {
            const int a = w.a[t - w.start], b = w.b[t - w.start];
            unconditional = a >= 0 && (b < 0 || a <= b) && a < p.M;   // the playhead and M-1 more
        }
        const size_t bytes = tr.frameBytes.load(std::memory_order_acquire);
        // Finishing a timeline frame another track already holds. It was
        // admitted with this track's bytes counted, but other admissions or
        // pool growth may have used that room since. Inside the ahead window
        // it is always admitted: the window is budget / cost per timeline
        // frame, so every track's window frames fit and the overshoot stays
        // about one timeline frame. Victims for a finish come only from lower
        // bands, so it never takes a same-band frame another track holds and
        // this one has yet to finish. A finish that is refused outside the
        // window unloads the other tracks' copies instead: a timeline frame is
        // held on every track or on none.
        const bool finishing = timelineHeldElsewhereLocked(t, track);
        int victimCeiling = INT_MAX;
        if (finishing) {
            const int sc = timelineScore(t, p);
            if (sc >= 3 * p.N) unconditional = true;
            if (p.N > 0) victimCeiling = sc < 0 ? -1 : (sc / p.N) * p.N;
        }
        if (!admit(t, unconditional, timelineNeedLocked(t, track, bytes), victimCeiling)) {
            if (finishing) dropPartialLocked(t, track);
            ++stats_.denied;
            return 2;
        }
        if (!tr.table->setDecoding(src, bytes)) return 0;
        out = Job{track, src, t, generation_.load(std::memory_order_acquire), bytes};
        noteIssued(track, src);
        ++stats_.admitted;
        return 1;
    };

    std::vector<int> pinned;
    tr.table->pinnedIndices(pinned);   // pins set here or straight on the table (hold-edge)
    for (int src : pinned) {
        if (attempt(src, true) == 1) return Scan::Job;
    }
    // Candidates come in falling score order, so the first refusal ends the scan.
    // Keep time with the sound leading: frames nearer than the lead cannot
    // arrive before the playhead passes them; they are skipped, not dropped.
    const int lead = leadSkipLocked(p);
    for (int t : w.aheadOrder) {
        if (w.a[t - w.start] > p.window) break;
        if (w.a[t - w.start] < lead) continue;
        const int r = attempt(t - 1 - tr.offset, false);
        if (r == 1) return Scan::Job;
        if (r == 2) return Scan::Denied;
    }
    for (int t : w.behindOrder) {
        if (w.b[t - w.start] > p.K) break;
        const int r = attempt(t - 1 - tr.offset, false);
        if (r == 1) return Scan::Job;
        if (r == 2) return Scan::Denied;
    }
    // Behind frames past K that the window still reaches going forward (a loop
    // that fits, or nearly): without this a clip longer than 2K never fully
    // loads in LOOP mode, whatever the budget.
    for (int t : w.forwardOrder) {
        const int a = w.a[t - w.start], b = w.b[t - w.start];
        if (a > p.window) break;
        if (b < 0 || a <= b || b <= p.K) continue;   // ahead or near-behind: tried above
        const int r = attempt(t - 1 - tr.offset, false);
        if (r == 1) return Scan::Job;
        if (r == 2) return Scan::Denied;
    }
    return Scan::Denied;
}

// ---- loader side ----

bool gfcFrameCache::nextJob(int track, Job& out, std::chrono::milliseconds wait) {
    std::unique_lock<std::mutex> lock(mutex_);
    const auto deadline = clock_t_::now() + wait;
    auto tablesVersion = [&] {
        uint64_t v = 0;
        for (const Track& tr : tracks_) if (tr.table) v += tr.table->version();
        return v;
    };
    uint64_t lastSeq = ~uint64_t(0), lastVer = ~uint64_t(0);
    clock_t_::time_point lastScan{};
    for (;;) {
        const uint64_t seq = wakeSeq_.load(std::memory_order_acquire);
        const uint64_t ver = tablesVersion();
        auto now = clock_t_::now();
        if (seq != lastSeq || ver != lastVer || now - lastScan >= kRescanEvery) {
            lastSeq = seq; lastVer = ver; lastScan = now;
            const Scan r = scan(track, out);
            if (r == Scan::Job) return true;
            if (r == Scan::None) return false;
            now = clock_t_::now();
        }
        if (now >= deadline) return false;
        const auto slice = std::min<clock_t_::duration>(kWaitSlice, deadline - now);
        cv_.wait_for(lock, slice, [&] { return wakeSeq_.load(std::memory_order_acquire) != seq; });
    }
}

void gfcFrameCache::commit(const Job& job, size_t actualBytes, bool decoded) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validTrack(job.track)) return;
    Track& tr = tracks_[job.track];
    if (!tr.table || tr.table->state(job.sourceIndex) != gfcFrameState::Decoding) { wake(); return; }
    if (decoded) {
        tr.table->setResident(job.sourceIndex, actualBytes);
    } else {
        bool stale = tr.cancelled;
        if (!stale && job.generation < generation_.load(std::memory_order_acquire)) {
            walk();
            const bool pinned = tr.table->pinned(job.sourceIndex);
            stale = !pinned && !isCandidate(job.sourceIndex + 1 + tr.offset, params());
        }
        if (stale) tr.table->setUnloaded(job.sourceIndex);
        else tr.table->setFailed(job.sourceIndex);
    }
    wake();
}

void gfcFrameCache::evictionCancelled() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stats_.evicted > 0) --stats_.evicted;
}

gfcFrameCache::Victims gfcFrameCache::drainVictims() {
    std::lock_guard<std::mutex> lock(mutex_);
    Victims v;
    v.frames.swap(pending_);
    victimsPending_.store(false, std::memory_order_release);
    v.trimPools = pooledBytesLocked() > costLocked();
    wake();
    return v;
}

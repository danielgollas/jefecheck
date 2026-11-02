// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// A track's decoded movie sound, held at one fixed format (48 kHz, stereo,
// float32 interleaved) so slice lengths are exact whatever the source was.
// Plain C++ (no libav, no Qt): safe to include from any translation unit.
#include <cstdint>
#include <memory>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

// Hole-tolerant and thread-safe: the audio thread writes blocks anywhere
// (a jump fills ahead of the playhead first, then backfills) while the mixer
// reads. Samples live in fixed-size chunk slots indexed by sample / chunk size
// (null = never written), so a write never moves what is stored and the lock
// is held only for the copy. The written ranges are tracked exactly; holes
// read as silence.
class gfcAudioClip {
public:
    static constexpr int kRate = 48000;
    static constexpr int kChannels = 2;

    // Empties the clip. `startSample` is an index in the movie's own 48 kHz
    // timeline (sample 0 = the first video frame); an append to the empty clip
    // lands there.
    void reset(int64_t startSample);
    // Writes `frames` stereo frames at `sample` (anywhere; overwrites).
    void writeAt(int64_t sample, const float* interleaved, int64_t frames);
    // Like writeAt, but only into the holes: written samples are kept. The
    // ranges actually written ([begin, end) pairs) go to `written` (optional).
    // Returns the frames written.
    int64_t fillHoles(int64_t sample, const float* interleaved, int64_t frames,
                      std::vector<std::pair<int64_t, int64_t>>* written);
    // writeAt(endSample(), ...).
    void append(const float* interleaved, int64_t frames);
    // True when every sample of [fromSample, fromSample+frames) is written.
    bool covers(int64_t fromSample, int64_t frames) const;
    // The first unwritten sample in [sample, endSample()), or -1 when there is none.
    int64_t firstHoleFrom(int64_t sample) const;
    // Lowest written sample and highest written + 1 (both the reset start
    // while nothing is written).
    int64_t startSample() const;
    int64_t endSample() const;
    // Copies `frames` stereo frames from `fromSample` into `out`, zero-filling
    // holes and anything outside [start, end). Returns the frames of the
    // request that fall inside [start, end).
    int64_t read(int64_t fromSample, int64_t frames, float* out) const;
    // floor(frameIndex * 48000 * rateDen / rateNum), exact in int64.
    static int64_t frameStartSample(int64_t frameIndex, int rateNum, int rateDen);

private:
    static constexpr int64_t kChunkFrames = 1 << 16;
    mutable std::mutex m_;
    int64_t resetStart_ = 0;
    std::map<int64_t, int64_t> filled_;  // written ranges [begin -> end), merged, disjoint
    std::vector<std::unique_ptr<float[]>> chunks_;  // slot k holds samples [k*kChunkFrames, (k+1)*kChunkFrames)
    bool coversLocked(int64_t from, int64_t to) const;
    void writeLocked(int64_t sample, const float* interleaved, int64_t frames);
    void markLocked(int64_t from, int64_t to);
};

// Abs-peak of the stereo mix (the louder channel) per 10 ms bin, built as
// audio arrives. Not thread-safe by itself; gfcSequence guards its copy.
struct gfcAudioPeaks {
    int binsPerSecond = 100;
    int64_t firstSample = 0;
    std::vector<float> peaks;
    // Grows on the first accumulate() after a reset, then at most once per
    // second of accumulated audio, and on publish(); reset() keeps it.
    uint64_t version = 0;
    int64_t framesAccumulated = 0; // frames seen since reset (last bin may be partial)

    void reset(int64_t firstSample);
    void accumulate(const float* interleaved, int64_t frames);
    // Max-merges `frames` stereo frames whose first sample is `firstSample`
    // into the bins at their absolute positions (bin = (sample - firstSample
    // of the peaks) / 480), growing the vector with zeros; samples before the
    // peaks' first sample are ignored. Bumps the version like accumulate().
    void accumulateAt(int64_t firstSample, const float* interleaved, int64_t frames);
    void publish();  // bumps version, e.g. when a fill pass ends with a partial second
};

// Writes `frames` stereo frames whose first sample sits at movie-time index
// `first` into `clip`, at their own positions, but only where the clip has a
// hole: what is already written is kept (a re-decode after a seek never
// replaces good sound), nothing is written before `floor` or at or past
// `limit` (the range's last sample), so a bad timestamp cannot fill memory.
// `peaks` (optional) accumulates what was written. Returns the frames written.
int64_t gfcAppendAudioAt(gfcAudioClip& clip, int64_t first, const float* data, int64_t frames, int64_t limit,
                         gfcAudioPeaks* peaks, int64_t floor = 0);

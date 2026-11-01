#pragma once
// A track's decoded movie sound, held at one fixed format (48 kHz, stereo,
// float32 interleaved) so slice lengths are exact whatever the source was.
// Plain C++ (no libav, no Qt): safe to include from any translation unit.
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

// Thread-safe: the loader thread appends while the mixer reads. Samples live
// in fixed-size chunks, so an append never moves what is already stored and
// the lock is held only for the copy itself.
class gfcAudioClip {
public:
    static constexpr int kRate = 48000;
    static constexpr int kChannels = 2;

    // Empties the clip; the next append lands at `startSample`, an index in the
    // movie's own 48 kHz timeline (sample 0 = the first video frame).
    void reset(int64_t startSample);
    void append(const float* interleaved, int64_t frames);
    int64_t startSample() const;
    int64_t endSample() const;  // [start, end)
    // Copies `frames` stereo frames from `fromSample` into `out`, zero-filling
    // anything outside [start, end). Returns the number of frames copied.
    int64_t read(int64_t fromSample, int64_t frames, float* out) const;
    // floor(frameIndex * 48000 * rateDen / rateNum), exact in int64.
    static int64_t frameStartSample(int64_t frameIndex, int rateNum, int rateDen);

private:
    static constexpr int64_t kChunkFrames = 1 << 16;
    mutable std::mutex m_;
    int64_t start_ = 0;
    int64_t length_ = 0;  // frames stored
    std::vector<std::unique_ptr<float[]>> chunks_;
};

// Abs-peak of the stereo mix (the louder channel) per 10 ms bin, built as
// audio arrives. Not thread-safe by itself; gfcSequence guards its copy.
struct gfcAudioPeaks {
    int binsPerSecond = 100;
    int64_t firstSample = 0;
    std::vector<float> peaks;
    uint64_t version = 0;          // bumped by every accumulate(); reset() keeps it
    int64_t framesAccumulated = 0; // frames seen since reset (last bin may be partial)

    void reset(int64_t firstSample);
    void accumulate(const float* interleaved, int64_t frames);
};

#include "gfcAudioClip.h"

#include <algorithm>
#include <cmath>
#include <cstring>

void gfcAudioClip::reset(int64_t startSample) {
    std::lock_guard<std::mutex> lock(m_);
    start_ = startSample;
    length_ = 0;
    chunks_.clear();
}

void gfcAudioClip::append(const float* interleaved, int64_t frames) {
    if (!interleaved || frames <= 0) return;
    std::lock_guard<std::mutex> lock(m_);
    while (frames > 0) {
        const int64_t chunk = length_ / kChunkFrames;
        const int64_t offset = length_ % kChunkFrames;
        if (chunk >= int64_t(chunks_.size()))
            chunks_.emplace_back(new float[size_t(kChunkFrames) * kChannels]);
        const int64_t n = std::min(frames, kChunkFrames - offset);
        std::memcpy(chunks_[size_t(chunk)].get() + offset * kChannels, interleaved,
                    size_t(n) * kChannels * sizeof(float));
        interleaved += n * kChannels;
        frames -= n;
        length_ += n;
    }
}

int64_t gfcAudioClip::startSample() const {
    std::lock_guard<std::mutex> lock(m_);
    return start_;
}

int64_t gfcAudioClip::endSample() const {
    std::lock_guard<std::mutex> lock(m_);
    return start_ + length_;
}

int64_t gfcAudioClip::read(int64_t fromSample, int64_t frames, float* out) const {
    if (!out || frames <= 0) return 0;
    std::fill(out, out + frames * kChannels, 0.f);
    std::lock_guard<std::mutex> lock(m_);
    // Overlap of [fromSample, fromSample+frames) with the stored range.
    const int64_t lo = std::max(fromSample, start_);
    const int64_t hi = std::min(fromSample + frames, start_ + length_);
    if (hi <= lo) return 0;
    int64_t pos = lo - start_;        // index into the stored frames
    float* dst = out + (lo - fromSample) * kChannels;
    int64_t left = hi - lo;
    while (left > 0) {
        const int64_t chunk = pos / kChunkFrames;
        const int64_t offset = pos % kChunkFrames;
        const int64_t n = std::min(left, kChunkFrames - offset);
        std::memcpy(dst, chunks_[size_t(chunk)].get() + offset * kChannels,
                    size_t(n) * kChannels * sizeof(float));
        dst += n * kChannels;
        pos += n;
        left -= n;
    }
    return hi - lo;
}

int64_t gfcAudioClip::frameStartSample(int64_t frameIndex, int rateNum, int rateDen) {
    if (rateNum <= 0 || rateDen <= 0) return 0;
    // frameIndex * 48000 * rateDen fits int64 for any realistic clip
    // (rateDen <= ~1e6, frameIndex <= ~1e8). Floor, also for negative indices.
    const int64_t num = frameIndex * int64_t(kRate) * int64_t(rateDen);
    int64_t q = num / rateNum;
    if ((num % rateNum) != 0 && num < 0) --q;
    return q;
}

void gfcAudioPeaks::reset(int64_t first) {
    firstSample = first;
    peaks.clear();
    framesAccumulated = 0;
    // version is left alone: it only ever grows, so a holder that resets and
    // refills can bump it itself and readers never see a repeat.
}

void gfcAudioPeaks::accumulate(const float* interleaved, int64_t frames) {
    if (!interleaved || frames <= 0 || binsPerSecond <= 0) return;
    const int64_t perBin = std::max<int64_t>(1, gfcAudioClip::kRate / binsPerSecond);
    for (int64_t k = 0; k < frames; ++k) {
        const size_t bin = size_t((framesAccumulated + k) / perBin);
        if (bin >= peaks.size()) peaks.resize(bin + 1, 0.f);
        const float v = std::max(std::fabs(interleaved[2 * k]), std::fabs(interleaved[2 * k + 1]));
        if (v > peaks[bin]) peaks[bin] = v;
    }
    framesAccumulated += frames;
    ++version;
}

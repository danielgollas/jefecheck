// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfcAudioClip.h"

#include <algorithm>
#include <cmath>
#include <cstring>

void gfcAudioClip::reset(int64_t startSample) {
    std::lock_guard<std::mutex> lock(m_);
    resetStart_ = startSample;
    filled_.clear();
    chunks_.clear();
}

// Copies into the chunk slots, allocating (zeroed) the ones first touched.
// Negative samples are dropped: the clip holds movie time from sample 0.
void gfcAudioClip::writeLocked(int64_t sample, const float* interleaved, int64_t frames) {
    if (sample < 0) {
        if (sample + frames <= 0) return;
        interleaved += (-sample) * kChannels;
        frames += sample;
        sample = 0;
    }
    while (frames > 0) {
        const int64_t chunk = sample / kChunkFrames;
        const int64_t offset = sample % kChunkFrames;
        if (chunk >= int64_t(chunks_.size())) chunks_.resize(size_t(chunk) + 1);
        std::unique_ptr<float[]>& slot = chunks_[size_t(chunk)];
        if (!slot) slot.reset(new float[size_t(kChunkFrames) * kChannels]());
        const int64_t n = std::min(frames, kChunkFrames - offset);
        std::memcpy(slot.get() + offset * kChannels, interleaved, size_t(n) * kChannels * sizeof(float));
        interleaved += n * kChannels;
        frames -= n;
        sample += n;
    }
}

// Adds [from, to) to the written ranges, merging neighbours.
void gfcAudioClip::markLocked(int64_t from, int64_t to) {
    from = std::max<int64_t>(from, 0);
    if (to <= from) return;
    auto it = filled_.upper_bound(from);
    if (it != filled_.begin()) {
        auto prev = std::prev(it);
        if (prev->second >= from) {
            from = prev->first;
            to = std::max(to, prev->second);
            it = filled_.erase(prev);
        }
    }
    while (it != filled_.end() && it->first <= to) {
        to = std::max(to, it->second);
        it = filled_.erase(it);
    }
    filled_.emplace(from, to);
}

bool gfcAudioClip::coversLocked(int64_t from, int64_t to) const {
    if (to <= from) return true;
    auto it = filled_.upper_bound(from);
    if (it == filled_.begin()) return false;
    --it;
    return it->first <= from && it->second >= to;
}

void gfcAudioClip::writeAt(int64_t sample, const float* interleaved, int64_t frames) {
    if (!interleaved || frames <= 0) return;
    std::lock_guard<std::mutex> lock(m_);
    writeLocked(sample, interleaved, frames);
    markLocked(sample, sample + frames);
}

int64_t gfcAudioClip::fillHoles(int64_t sample, const float* interleaved, int64_t frames,
                                std::vector<std::pair<int64_t, int64_t>>* written) {
    if (!interleaved || frames <= 0) return 0;
    std::lock_guard<std::mutex> lock(m_);
    const int64_t lo = std::max<int64_t>(sample, 0), hi = sample + frames;
    // The holes of [lo, hi): the gaps between the written ranges that overlap it.
    std::vector<std::pair<int64_t, int64_t>> holes;
    int64_t pos = lo;
    auto it = filled_.upper_bound(lo);
    if (it != filled_.begin()) {
        auto prev = std::prev(it);
        if (prev->second > pos) pos = prev->second;
    }
    for (; pos < hi; ++it) {
        const int64_t next = it == filled_.end() ? hi : std::min(hi, it->first);
        if (next > pos) holes.emplace_back(pos, next);
        if (it == filled_.end()) break;
        pos = std::max(pos, it->second);
    }
    int64_t total = 0;
    for (const auto& h : holes) {
        writeLocked(h.first, interleaved + (h.first - sample) * kChannels, h.second - h.first);
        markLocked(h.first, h.second);
        total += h.second - h.first;
    }
    if (written) *written = std::move(holes);
    return total;
}

void gfcAudioClip::append(const float* interleaved, int64_t frames) {
    if (!interleaved || frames <= 0) return;
    std::lock_guard<std::mutex> lock(m_);
    const int64_t at = filled_.empty() ? resetStart_ : filled_.rbegin()->second;
    writeLocked(at, interleaved, frames);
    markLocked(at, at + frames);
}

bool gfcAudioClip::covers(int64_t fromSample, int64_t frames) const {
    std::lock_guard<std::mutex> lock(m_);
    return coversLocked(fromSample, fromSample + std::max<int64_t>(frames, 0));
}

int64_t gfcAudioClip::firstHoleFrom(int64_t sample) const {
    std::lock_guard<std::mutex> lock(m_);
    if (filled_.empty()) return -1;
    const int64_t end = filled_.rbegin()->second;
    if (sample >= end) return -1;
    auto it = filled_.upper_bound(sample);
    if (it == filled_.begin()) return sample;   // before the first written range
    --it;
    if (it->second <= sample) return sample;    // between two ranges
    return it->second < end ? it->second : -1;  // the end of the range holding `sample`
}

int64_t gfcAudioClip::startSample() const {
    std::lock_guard<std::mutex> lock(m_);
    return filled_.empty() ? resetStart_ : filled_.begin()->first;
}

int64_t gfcAudioClip::endSample() const {
    std::lock_guard<std::mutex> lock(m_);
    return filled_.empty() ? resetStart_ : filled_.rbegin()->second;
}

int64_t gfcAudioClip::read(int64_t fromSample, int64_t frames, float* out) const {
    if (!out || frames <= 0) return 0;
    std::fill(out, out + frames * kChannels, 0.f);
    std::lock_guard<std::mutex> lock(m_);
    if (filled_.empty()) return 0;
    const int64_t start = filled_.begin()->first, end = filled_.rbegin()->second;
    const int64_t lo = std::max(fromSample, start);
    const int64_t hi = std::min(fromSample + frames, end);
    if (hi <= lo) return 0;
    // Unwritten parts of an allocated chunk are zero, and a missing chunk is
    // skipped, so holes come out silent.
    int64_t pos = lo;
    float* dst = out + (lo - fromSample) * kChannels;
    while (pos < hi) {
        const int64_t chunk = pos / kChunkFrames;
        const int64_t offset = pos % kChunkFrames;
        const int64_t n = std::min(hi - pos, kChunkFrames - offset);
        if (chunk < int64_t(chunks_.size()) && chunks_[size_t(chunk)])
            std::memcpy(dst, chunks_[size_t(chunk)].get() + offset * kChannels, size_t(n) * kChannels * sizeof(float));
        dst += n * kChannels;
        pos += n;
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
    // Readers copy the whole vector when the version moves, so it moves at most once per second
    // of new audio; the holder calls publish() when loading stops.
    const int64_t before = framesAccumulated;
    framesAccumulated += frames;
    if (before == 0 || before / gfcAudioClip::kRate != framesAccumulated / gfcAudioClip::kRate) ++version;
}

void gfcAudioPeaks::accumulateAt(int64_t first, const float* interleaved, int64_t frames) {
    if (!interleaved || frames <= 0 || binsPerSecond <= 0) return;
    if (first < firstSample) {
        const int64_t skip = firstSample - first;
        if (skip >= frames) return;
        interleaved += skip * 2;
        frames -= skip;
        first = firstSample;
    }
    const int64_t perBin = std::max<int64_t>(1, gfcAudioClip::kRate / binsPerSecond);
    const int64_t base = first - firstSample;
    const size_t lastBin = size_t((base + frames - 1) / perBin);
    if (lastBin >= peaks.size()) peaks.resize(lastBin + 1, 0.f);
    for (int64_t k = 0; k < frames; ++k) {
        const size_t bin = size_t((base + k) / perBin);
        const float v = std::max(std::fabs(interleaved[2 * k]), std::fabs(interleaved[2 * k + 1]));
        if (v > peaks[bin]) peaks[bin] = v;
    }
    const int64_t before = framesAccumulated;
    framesAccumulated += frames;
    if (before == 0 || before / gfcAudioClip::kRate != framesAccumulated / gfcAudioClip::kRate) ++version;
}

void gfcAudioPeaks::publish() { ++version; }

int64_t gfcAppendAudioAt(gfcAudioClip& clip, int64_t first, const float* data, int64_t frames, int64_t limit,
                         gfcAudioPeaks* peaks, int64_t floor) {
    if (!data || frames <= 0) return 0;
    const int64_t lo = std::max<int64_t>({first, floor, 0});
    const int64_t hi = std::min(first + frames, limit);
    if (hi <= lo) return 0;
    const float* from = data + (lo - first) * gfcAudioClip::kChannels;
    std::vector<std::pair<int64_t, int64_t>> written;
    const int64_t n = clip.fillHoles(lo, from, hi - lo, peaks ? &written : nullptr);
    if (peaks)
        for (const auto& w : written)
            peaks->accumulateAt(w.first, data + (w.first - first) * gfcAudioClip::kChannels, w.second - w.first);
    return n;
}

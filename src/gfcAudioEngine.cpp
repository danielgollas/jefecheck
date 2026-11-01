// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfcAudioEngine.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "UIConstants.h"
#include "gfcplaybackmanager.h"   // advanceFrame (pulls in glad: keep it out of the header)

gfcAudioEngine audioEngine;

namespace {
// Constructed after audioEngine and destroyed before it (same TU, reverse order).
bool gEngineAlive = false;
struct EngineAliveSentinel {
    EngineAliveSentinel() { gEngineAlive = true; }
    ~EngineAliveSentinel() { gEngineAlive = false; }
} gEngineAliveSentinel;

inline int64_t fss(int64_t index, int num, int den) { return gfcAudioClip::frameStartSample(index, num, den); }
}

gfcAudioEngine::gfcAudioEngine() : scratch_(size_t(kScratchFrames) * kChannels, 0.f) {}

void gfcAudioEngine::setOutput(gfcAudioOutput* out) {
    std::lock_guard<std::mutex> lock(m_);
    out_ = out;
    renderPos_ = 0;
    leading_ = leadStartPending_ = stalled_ = false;
    logCount_ = 0;
    hasPending_ = false;
    stopAllVoices();
}

void gfcAudioEngine::setTrackClip(int track, std::shared_ptr<gfcAudioClip> clip, int rateNum, int rateDen, int64_t frameCount) {
    if (track < 0 || track >= kMaxTracks) return;
    // Every clip the track lets go of lands in one of these and is destroyed
    // after the lock is released (freeing a long clip's chunks takes time, and
    // render() must never wait on it). Each holder is empty when it is assigned,
    // so no move-assignment can release a clip while the lock is held.
    std::shared_ptr<gfcAudioClip> replaced, rejected;
    {
        std::lock_guard<std::mutex> lock(m_);
        Track& tr = tracks_[track];
        if (tr.clip != clip) {
            // Voices point into the old clip: stop them before it can go away.
            tr.voice = Voice(); tr.tail = Voice();
            tr.seamValid = false;
            replaced = std::move(tr.clip);
            tr.clip = std::move(clip);
        }
        if (!tr.clip || rateNum <= 0 || rateDen <= 0 || frameCount <= 0) {
            tr.voice = Voice(); tr.tail = Voice(); tr.seamValid = false;
            rejected = std::move(tr.clip);   // leaves tr.clip empty
            tr.rateNum = 0; tr.rateDen = 1; tr.frameCount = 0;
        } else {
            tr.rateNum = rateNum; tr.rateDen = rateDen; tr.frameCount = frameCount;
        }
    }   // lock released here; `rejected`, `replaced` and `clip` are destroyed after it
}

bool gfcAudioEngine::testMutexFreeFromOtherThread() const {
    // try_lock from a second thread is well-defined even when this thread
    // holds the mutex (it just fails), unlike a same-thread try_lock.
    bool free = false;
    std::thread([&] { if (m_.try_lock()) { free = true; m_.unlock(); } }).join();
    return free;
}

void gfcAudioEngine::clearTrack(int track) { setTrackClip(track, nullptr, 0, 1, 0); }

void gfcAudioEngine::setTrackOffset(int track, int offset) {
    if (track < 0 || track >= kMaxTracks) return;
    std::lock_guard<std::mutex> lock(m_);
    tracks_[track].offset = offset;
}

void gfcAudioEngine::setTrackMuted(int track, bool muted) {
    if (track < 0 || track >= kMaxTracks) return;
    std::lock_guard<std::mutex> lock(m_);
    Track& tr = tracks_[track];
    if (muted && !tr.muted) cutVoices(tr);
    tr.muted = muted;
}

bool gfcAudioEngine::trackMuted(int track) const {
    if (track < 0 || track >= kMaxTracks) return false;
    std::lock_guard<std::mutex> lock(m_);
    return tracks_[track].muted;
}

bool gfcAudioEngine::trackHasAudio(int track) const {
    if (track < 0 || track >= kMaxTracks) return false;
    std::lock_guard<std::mutex> lock(m_);
    return tracks_[track].clip && tracks_[track].frameCount > 0;
}

void gfcAudioEngine::setPlayerRate(int num, int den) {
    if (num <= 0 || den <= 0) return;
    std::lock_guard<std::mutex> lock(m_);
    playerNum_ = num; playerDen_ = den;
}

bool gfcAudioEngine::audible(const Track& tr) const {
    return tr.clip && tr.frameCount > 0 && !tr.muted;
}

bool gfcAudioEngine::nativeRate(const Track& tr) const {
    // Cross-multiplied, so 48000/2002 equals 24000/1001 (reduced equality).
    return tr.rateNum > 0 && int64_t(tr.rateNum) * playerDen_ == int64_t(playerNum_) * tr.rateDen;
}

bool gfcAudioEngine::movieIndex(const Track& tr, int frame, int64_t& index) const {
    index = int64_t(frame) - tr.offset - 1;
    return index >= 0 && index < tr.frameCount;
}

bool gfcAudioEngine::canLeadLocked() const {
    if (!out_ || !out_->isOpen() || transport_.direction <= 0) return false;
    for (const Track& tr : tracks_)
        if (audible(tr) && nativeRate(tr)) return true;
    return false;
}

bool gfcAudioEngine::canLead() const {
    std::lock_guard<std::mutex> lock(m_);
    return canLeadLocked();
}

void gfcAudioEngine::startLeading(const gfcAudioTransport& t) {
    std::lock_guard<std::mutex> lock(m_);
    transport_ = t;
    if (!canLeadLocked()) { leading_ = leadStartPending_ = false; return; }
    hasPending_ = false;
    for (Track& tr : tracks_) cutVoices(tr);
    leading_ = true; leadStartPending_ = true; stalled_ = false;
    leadFrame_ = t.startFrame; walkDir_ = 1; frameLeft_ = 0;
    logCount_ = 0;
}

void gfcAudioEngine::updateTransport(const gfcAudioTransport& t) {
    std::lock_guard<std::mutex> lock(m_);
    if (leading_) {
        transport_.startLimit = t.startLimit; transport_.endLimit = t.endLimit; transport_.loopMode = t.loopMode;
        stalled_ = false;   // new limits may let a held ONCE walk move again
    } else {
        transport_ = t;
    }
}

void gfcAudioEngine::stopLeading() {
    std::lock_guard<std::mutex> lock(m_);
    if (!leading_) return;
    leading_ = leadStartPending_ = stalled_ = false;
    for (Track& tr : tracks_) cutVoices(tr);
}

bool gfcAudioEngine::isLeading() const {
    std::lock_guard<std::mutex> lock(m_);
    return leading_;
}

int gfcAudioEngine::leaderFrame() const {
    std::lock_guard<std::mutex> lock(m_);
    if (leadStartPending_ || logCount_ == 0) return leadFrame_;
    const int64_t played = out_ ? out_->playedSamples() : renderPos_;
    const int64_t n = std::min<int64_t>(logCount_, kLogSize);
    for (int64_t k = 0; k < n; ++k) {
        const gfcAudioLogEntry& e = log_[(logCount_ - 1 - k) % kLogSize];
        if (e.startSample <= played) return e.frame;
    }
    return log_[(logCount_ - n) % kLogSize].frame;   // device still behind the oldest entry
}

int gfcAudioEngine::leaderDirection() const {
    std::lock_guard<std::mutex> lock(m_);
    return walkDir_;
}

void gfcAudioEngine::playSlice(int frame) {
    std::lock_guard<std::mutex> lock(m_);
    if (leading_) return;
    hasPending_ = true; pendingFrame_ = frame;
}

void gfcAudioEngine::stopSlices() {
    std::lock_guard<std::mutex> lock(m_);
    hasPending_ = false;
    if (!leading_) for (Track& tr : tracks_) cutVoices(tr);
}

std::vector<gfcAudioLogEntry> gfcAudioEngine::renderLogSnapshot() const {
    std::lock_guard<std::mutex> lock(m_);
    std::vector<gfcAudioLogEntry> v;
    const int64_t n = std::min<int64_t>(logCount_, kLogSize);
    for (int64_t k = logCount_ - n; k < logCount_; ++k) v.push_back(log_[k % kLogSize]);
    return v;
}

// ---- render side (called with m_ held) ----

int64_t gfcAudioEngine::leadDuration(int frame) const {
    // Timeline frame F covers [fss(F-1), fss(F)) at the player rate, so the walk
    // never drifts from the rational (29.97 alternates 1601/1602 samples).
    return std::max<int64_t>(1, fss(frame, playerNum_, playerDen_) - fss(int64_t(frame) - 1, playerNum_, playerDen_));
}

void gfcAudioEngine::cutVoices(Track& tr) {
    tr.seamValid = false;
    if (!tr.voice.active) return;
    Voice& v = tr.voice;
    v.playLen = std::min(v.playLen, v.played + kFadeSamples);
    v.fadeOut = true;
    tr.tail = v;            // an older tail still fading is dropped
    v = Voice();
}

void gfcAudioEngine::stopAllVoices() {
    for (Track& tr : tracks_) { tr.voice = Voice(); tr.tail = Voice(); tr.seamValid = false; }
}

void gfcAudioEngine::startSlices(int frame) {
    const int64_t onScreen = std::max<int64_t>(1, fss(1, playerNum_, playerDen_));
    for (Track& tr : tracks_) {
        cutVoices(tr);
        int64_t i = 0;
        if (!audible(tr) || !movieIndex(tr, frame, i)) continue;
        const int64_t start = fss(i, tr.rateNum, tr.rateDen);
        const int64_t len = fss(i + 1, tr.rateNum, tr.rateDen) - start;
        Voice& v = tr.voice;
        v.active = true; v.clip = tr.clip.get();
        v.readStart = start; v.playLen = std::min(len, onScreen); v.played = 0;
        v.fadeIn = true; v.fadeOut = true;   // the next slice is unknown: always faded
    }
}

void gfcAudioEngine::beginLeadFrame(int frame) {
    leadFrame_ = frame;
    frameLeft_ = leadDuration(frame);
    log_[logCount_ % kLogSize] = gfcAudioLogEntry{renderPos_, frame};
    ++logCount_;

    // The walk's next frame, to decide each slice's fade-out now.
    int nextDir = walkDir_;
    const int next = gfcPlaybackManager::advanceFrame(frame, nextDir, transport_.loopMode, transport_.startLimit, transport_.endLimit);
    const bool nextHeld = transport_.loopMode == LOOPMODEONCE_ID && next == frame;
    const int64_t D = frameLeft_;

    for (Track& tr : tracks_) {
        int64_t i = 0;
        if (walkDir_ < 0 || !audible(tr) || !movieIndex(tr, frame, i)) {
            // Reverse, muted, no audio or outside the movie: silence (a held
            // frame never repeats audio).
            cutVoices(tr);
            continue;
        }
        const int64_t start = fss(i, tr.rateNum, tr.rateDen);
        const int64_t len = fss(i + 1, tr.rateNum, tr.rateDen) - start;
        // Contiguous with the previous slice (same track, next sample, give or
        // take the one-sample rounding of the two rationals): keep streaming.
        const bool continuous = tr.seamValid && !tr.voice.active && std::llabs(start - tr.seamSrc) <= 1;
        Voice v;
        v.active = true; v.clip = tr.clip.get(); v.played = 0;
        v.readStart = continuous ? tr.seamSrc : start;
        v.playLen = continuous ? D : std::min(len, D);
        v.fadeIn = !continuous;

        const int64_t end = v.readStart + v.playLen;
        bool nextContiguous = false;
        int64_t ni = 0;
        if (!nextHeld && nextDir > 0 && v.playLen == D && movieIndex(tr, next, ni))
            nextContiguous = std::llabs(fss(ni, tr.rateNum, tr.rateDen) - end) <= 1;
        v.fadeOut = !nextContiguous;

        if (tr.voice.active) cutVoices(tr);
        tr.voice = v;
        tr.seamValid = !v.fadeOut;
        tr.seamSrc = end;
    }
}

void gfcAudioEngine::mixVoice(Voice& v, float* out, int frames) {
    if (!v.active || !v.clip) { v.active = false; return; }
    int64_t todo = std::min<int64_t>(frames, v.playLen - v.played);
    float* dst = out;
    while (todo > 0) {
        const int n = int(std::min<int64_t>(todo, kScratchFrames));
        v.clip->read(v.readStart + v.played, n, scratch_.data());
        for (int j = 0; j < n; ++j) {
            const int64_t idx = v.played + j;
            float g = 1.f;
            if (v.fadeIn && idx < kFadeSamples) g = float(idx + 1) / float(kFadeSamples);
            if (v.fadeOut) {
                const int64_t rem = v.playLen - idx;
                if (rem < kFadeSamples) g *= float(rem) / float(kFadeSamples);
            }
            dst[2*j]     += g * scratch_[size_t(2*j)];
            dst[2*j + 1] += g * scratch_[size_t(2*j + 1)];
        }
        v.played += n; dst += 2*n; todo -= n;
    }
    if (v.played >= v.playLen) v.active = false;
}

bool gfcAudioEngineAlive() { return gEngineAlive; }

bool gfcAudioEngine::renderFor(const gfcAudioOutput* out, float* interleaved, int frames) {
    if (!interleaved || frames <= 0) return false;
    std::memset(interleaved, 0, size_t(frames) * kChannels * sizeof(float));
    std::lock_guard<std::mutex> lock(m_);
    if (out != out_) return false;
    renderLocked(interleaved, frames);
    return true;
}

void gfcAudioEngine::render(float* interleaved, int frames) {
    if (!interleaved || frames <= 0) return;
    std::memset(interleaved, 0, size_t(frames) * kChannels * sizeof(float));
    std::lock_guard<std::mutex> lock(m_);
    renderLocked(interleaved, frames);
}

void gfcAudioEngine::renderLocked(float* interleaved, int frames) {

    if (hasPending_ && !leading_) { hasPending_ = false; startSlices(pendingFrame_); }
    if (leading_ && leadStartPending_) { leadStartPending_ = false; beginLeadFrame(leadFrame_); }

    int pos = 0;
    while (pos < frames) {
        int seg = frames - pos;
        const bool walking = leading_ && !stalled_;
        if (walking) seg = int(std::min<int64_t>(seg, frameLeft_));
        float* dst = interleaved + size_t(pos) * kChannels;
        for (Track& tr : tracks_) { mixVoice(tr.tail, dst, seg); mixVoice(tr.voice, dst, seg); }
        pos += seg; renderPos_ += seg;
        if (walking) {
            frameLeft_ -= seg;
            if (frameLeft_ <= 0) {
                int dir = walkDir_;
                const int next = gfcPlaybackManager::advanceFrame(leadFrame_, dir, transport_.loopMode, transport_.startLimit, transport_.endLimit);
                if (transport_.loopMode == LOOPMODEONCE_ID && next == leadFrame_) {
                    stalled_ = true;   // ONCE holds at the end: sound stops, the frame stays
                } else {
                    walkDir_ = dir;
                    beginLeadFrame(next);
                }
            }
        }
    }
    for (int k = 0; k < frames * kChannels; ++k)
        interleaved[k] = std::clamp(interleaved[k], -1.f, 1.f);
}

// ---- fake output ----

bool gfcFakeAudioOutput::open(gfcAudioEngine* source) {
    src_ = source; open_ = source != nullptr; played_ = 0;
    return open_;
}

void gfcFakeAudioOutput::close() { open_ = false; src_ = nullptr; }

bool gfcFakeAudioOutput::isOpen() const { return open_; }

int64_t gfcFakeAudioOutput::playedSamples() const { return played_; }

void gfcFakeAudioOutput::pump(int frames, std::vector<float>* captured) {
    if (!open_ || !src_ || frames <= 0) return;
    std::vector<float> buf(size_t(frames) * gfcAudioEngine::kChannels);
    src_->render(buf.data(), frames);
    if (captured) captured->insert(captured->end(), buf.begin(), buf.end());
    played_ += frames;
}

#pragma once
// Mixes the tracks' movie sound into one 48 kHz stereo float stream for one
// output device. Device-free: an output (QAudioSink in the app, a fake in the
// self-tests) pulls samples with render().
//
// Two ways sound follows the timeline:
//  * Leading - while a native-rate track plays forward, the engine walks the
//    frames itself (gfcPlaybackManager::advanceFrame) one on-screen duration at
//    a time, streams contiguous frames seamlessly, and logs where each frame
//    starts so leaderFrame() tells the picture which frame is being heard.
//    Off-rate tracks play each frame's own slice (cut or silence-padded).
//    Reverse (a BOUNCE return pass) is silent but the walk continues.
//  * Slices - playSlice() plays one frame's slice for step, scrub and
//    follow-the-picture playback. A newer request replaces a pending one.
//
// Threading: render() runs on the output's audio thread; every other call is
// GUI-side. One mutex guards all state; render() holds it for one mix pass and
// never allocates (one scratch buffer is preallocated).
//
// Plain C++: this header must stay free of glad/libav/Qt (developer_notes §1).
// The loop mode is a plain int (LOOPMODEONCE_ID/LOOPMODELOOP_ID/LOOPMODEBOUNCE_ID).
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "gfcAudioClip.h"

class gfcAudioEngine;

class gfcAudioOutput {
public:
    virtual ~gfcAudioOutput(){};
    // Starts pulling from `source` (calls source->render). playedSamples()
    // restarts at 0, so call gfcAudioEngine::setOutput(this) again after
    // re-opening, which restarts the engine's own sample count to match.
    virtual bool open(gfcAudioEngine* source) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    // Samples the device has played since open(), on the same scale as the
    // samples render() has produced.
    virtual int64_t playedSamples() const = 0;
};

struct gfcAudioTransport {
    int startFrame = 1; int startLimit = 1; int endLimit = 1; int loopMode = 0;
    int direction = 1; /* LOOPMODEONCE_ID/LOOPMODELOOP_ID/LOOPMODEBOUNCE_ID; direction from advanceFrame */
};

struct gfcAudioLogEntry { int64_t startSample = 0; int frame = 0; };

class gfcAudioEngine {
public:
    static constexpr int kRate = 48000, kChannels = 2, kFadeSamples = 144;   // 3 ms fades
    static constexpr int kMaxTracks = 4;

    gfcAudioEngine();

    // May be null (no device): everything below still works, silently. Stops
    // leading and any slices, and restarts the engine's sample count at 0.
    void setOutput(gfcAudioOutput* out);
    // A track's slice for timeline frame F is movie frame i = F - offset - 1 when
    // 0 <= i < frameCount, samples [frameStartSample(i), frameStartSample(i+1))
    // at the track's rate. A null clip clears the track.
    void setTrackClip(int track, std::shared_ptr<gfcAudioClip> clip, int rateNum, int rateDen, int64_t frameCount);
    void clearTrack(int track);
    void setTrackOffset(int track, int offset);
    void setTrackMuted(int track, bool muted);   // muting fades the track's sound out
    bool trackMuted(int track) const;
    bool trackHasAudio(int track) const;
    void setPlayerRate(int num, int den);

    // An open output, a forward transport (ONCE/LOOP forward, or BOUNCE on a
    // forward pass, from the last startLeading/updateTransport), and some
    // unmuted track with audio whose rate equals the player rate.
    bool canLead() const;
    // No-op unless canLead() holds for `t`. Stops any slices; the walk starts
    // at t.startFrame on the next render().
    void startLeading(const gfcAudioTransport& t);
    // While leading, takes the new limits and loop mode; the walk keeps its own
    // frame and direction. Otherwise stores `t` for canLead().
    void updateTransport(const gfcAudioTransport& t);
    void stopLeading();
    // True from startLeading until stopLeading (also while ONCE holds at the end).
    bool isLeading() const;
    // The frame whose slice is at the output's playedSamples().
    int leaderFrame() const;
    // The walk's current direction (BOUNCE turns it).
    int leaderDirection() const;

    // Follow mode / step / scrub: start this frame's slices now (cuts a playing
    // slice with a fade-out); a slice requested while a newer one is pending
    // replaces it. Each slice plays for at most one on-screen duration at the
    // player rate and fades in and out. Ignored while leading.
    void playSlice(int frame);
    void stopSlices();

    // Called by the output: mixes `frames` stereo frames (sum, clamp [-1,1]).
    // Never allocates.
    void render(float* interleaved, int frames);
    // render() for a device that may have been replaced: mixes only while `out`
    // is the engine's output (setOutput), otherwise writes silence and returns
    // false, so a replaced device never advances the engine's walk.
    bool renderFor(const gfcAudioOutput* out, float* interleaved, int frames);

    // Self-test view of the leading log, oldest first (allocates; GUI side).
    std::vector<gfcAudioLogEntry> renderLogSnapshot() const;
    // Self-test hook: true when no thread holds the engine mutex (probed from a
    // second thread). Used to prove clips are never destroyed under the lock.
    bool testMutexFreeFromOtherThread() const;

private:
    struct Voice {
        bool active = false;
        const gfcAudioClip* clip = nullptr;  // owned by the Track's shared_ptr
        int64_t readStart = 0, playLen = 0, played = 0;
        bool fadeIn = false, fadeOut = false;
    };
    struct Track {
        std::shared_ptr<gfcAudioClip> clip;
        int rateNum = 0, rateDen = 1; int64_t frameCount = 0; int offset = 0; bool muted = false;
        Voice voice, tail;                   // tail = a cut slice fading out
        bool seamValid = false; int64_t seamSrc = 0;  // where a seamless next slice would continue
    };
    static constexpr int kLogSize = 1024;
    static constexpr int kScratchFrames = 4096;

    bool canLeadLocked() const;
    bool audible(const Track& tr) const;
    bool nativeRate(const Track& tr) const;
    bool movieIndex(const Track& tr, int frame, int64_t& index) const;
    int64_t leadDuration(int frame) const;
    void cutVoices(Track& tr);
    void stopAllVoices();
    void startSlices(int frame);
    void beginLeadFrame(int frame);
    void mixVoice(Voice& v, float* out, int frames);
    void renderLocked(float* interleaved, int frames);   // m_ held, buffer zeroed

    mutable std::mutex m_;
    gfcAudioOutput* out_ = nullptr;
    Track tracks_[kMaxTracks];
    int playerNum_ = 24, playerDen_ = 1;
    gfcAudioTransport transport_;
    int64_t renderPos_ = 0;                  // samples rendered since setOutput

    bool hasPending_ = false; int pendingFrame_ = 0;

    bool leading_ = false, leadStartPending_ = false, stalled_ = false;
    int leadFrame_ = 1, walkDir_ = 1;
    int64_t frameLeft_ = 0;                  // samples left of leadFrame_'s on-screen duration
    gfcAudioLogEntry log_[kLogSize];
    int64_t logCount_ = 0;

    std::vector<float> scratch_;             // kScratchFrames stereo frames
};

// The app's one engine (defined in gfcAudioEngine.cpp). gfcAudioEngineAlive()
// is false before it is constructed and after it is destroyed, so code that
// runs during static teardown (gfcSequence's destructor) can skip it.
extern gfcAudioEngine audioEngine;
bool gfcAudioEngineAlive();

// Device-free output for the self-tests: pump() pulls `frames` from the engine.
class gfcFakeAudioOutput : public gfcAudioOutput {
public:
    bool open(gfcAudioEngine* source) override;
    void close() override;
    bool isOpen() const override;
    int64_t playedSamples() const override;
    void pump(int frames, std::vector<float>* captured = nullptr); /* calls render, advances playedSamples */
private:
    gfcAudioEngine* src_ = nullptr;
    bool open_ = false;
    int64_t played_ = 0;
};

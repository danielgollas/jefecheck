// SPDX-License-Identifier: GPL-2.0-or-later
// The app's audio device: QAudioSink in pull mode on its own thread, fed by gfcAudioEngine.
// The only Qt Multimedia TU; never include glad here. See developer_notes.md §38.
#include "AudioOutput_qt.h"

#include "../gfcAudioEngine.h"
#include "../gfcAudioClip.h"
#include "../UIConstants.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QCoreApplication>
#include <QIODevice>
#include <QMediaDevices>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kEngineRate = gfcAudioEngine::kRate;
constexpr double kBufferSeconds = 0.040;
constexpr int kPollMs = 2;
constexpr int kBlock = 128;   // engine frames per render() pull (2.7 ms)

// Self-test only: skip the 48 kHz float/int16 attempts and use this format, so
// the resampler and int conversion run on a real device.
std::atomic<bool> gForceFormat{false};
std::atomic<int> gForceRate{44100};

// Pulls the engine and writes the device's format. Lives on the audio thread.
class EnginePull : public QIODevice {
public:
    void configure(gfcAudioEngine* src, const gfcAudioOutput* owner, const QAudioFormat& fmt) {
        src_ = src; owner_ = owner; fmt_ = fmt;
        channels_ = std::max(1, fmt.channelCount());
        resample_ = fmt.sampleRate() != kEngineRate && fmt.sampleRate() > 0;
        step_ = resample_ ? double(kEngineRate) / double(fmt.sampleRate()) : 1.0;
        frac_ = 1.0; prevL_ = prevR_ = curL_ = curR_ = 0.f;
        inPos_ = inLen_ = 0;
        in_.assign(size_t(kBlock) * 2, 0.f);
    }
    void resetCounters() { rendered_.store(0); inPos_ = inLen_ = 0; }
    // Engine (48 kHz) frames pulled from the engine since resetCounters().
    int64_t rendered() const { return rendered_.load(std::memory_order_acquire); }

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return (qint64(1) << 24) + QIODevice::bytesAvailable(); }

    // Self-test hook: the engine-frame index of the first audible sample
    // rendered after arm(), or -1.
    void armAudible() { firstAudible_.store(-1); armed_.store(true); }
    int64_t firstAudible() const { return firstAudible_.load(); }

protected:
    qint64 readData(char* data, qint64 maxlen) override {
        const int bpf = fmt_.bytesPerFrame();
        if (!src_ || bpf <= 0 || maxlen < bpf) return 0;
        const qint64 frames = maxlen / bpf;
        char* p = data;
        for (qint64 k = 0; k < frames; ++k, p += bpf) {
            float l, r;
            if (resample_) {
                while (frac_ >= 1.0) { prevL_ = curL_; prevR_ = curR_; nextEngineFrame(curL_, curR_); frac_ -= 1.0; }
                const float t = float(frac_);
                l = prevL_ + (curL_ - prevL_) * t; r = prevR_ + (curR_ - prevR_) * t;
                frac_ += step_;
            } else {
                nextEngineFrame(l, r);
            }
            writeFrame(p, l, r);
        }
        return frames * bpf;
    }
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    void nextEngineFrame(float& l, float& r) {
        if (inPos_ >= inLen_) {
            src_->renderFor(owner_, in_.data(), kBlock);
            const int64_t base = rendered_.load(std::memory_order_relaxed);
            if (armed_.load(std::memory_order_relaxed)) {
                for (int i = 0; i < kBlock; ++i)
                    if (in_[size_t(2*i)] != 0.f || in_[size_t(2*i+1)] != 0.f) {
                        firstAudible_.store(base + i); armed_.store(false); break;
                    }
            }
            rendered_.store(base + kBlock, std::memory_order_release);
            inPos_ = 0; inLen_ = kBlock;
        }
        l = in_[size_t(2*inPos_)]; r = in_[size_t(2*inPos_ + 1)]; ++inPos_;
    }
    void writeFrame(char* p, float l, float r) const {
        for (int c = 0; c < channels_; ++c) {
            const float v = channels_ == 1 ? 0.5f * (l + r) : (c == 0 ? l : (c == 1 ? r : 0.f));
            const float x = std::clamp(v, -1.f, 1.f);
            switch (fmt_.sampleFormat()) {
            case QAudioFormat::Float: reinterpret_cast<float*>(p)[c] = x; break;
            case QAudioFormat::Int16: reinterpret_cast<qint16*>(p)[c] = qint16(std::lrint(x * 32767.f)); break;
            case QAudioFormat::Int32: reinterpret_cast<qint32*>(p)[c] = qint32(std::llrint(double(x) * 2147483647.0)); break;
            case QAudioFormat::UInt8: reinterpret_cast<quint8*>(p)[c] = quint8(std::lrint(128.f + x * 127.f)); break;
            default: break;
            }
        }
    }

    gfcAudioEngine* src_ = nullptr;
    const gfcAudioOutput* owner_ = nullptr;
    QAudioFormat fmt_;
    int channels_ = 2;
    bool resample_ = false;
    double step_ = 1.0, frac_ = 1.0;
    float prevL_ = 0, prevR_ = 0, curL_ = 0, curR_ = 0;
    std::vector<float> in_;
    int inPos_ = 0, inLen_ = 0;
    std::atomic<int64_t> rendered_{0};
    std::atomic<bool> armed_{false};
    std::atomic<int64_t> firstAudible_{-1};
};

// Owns the sink. Every method runs on the audio thread.
class SinkWorker : public QObject {
public:
    explicit SinkWorker(const gfcAudioOutput* owner) : owner_(owner) {}

    std::atomic<bool> active{false};
    std::atomic<int64_t> played{0};
    EnginePull* pull = nullptr;

    std::string formatInfo() const { std::lock_guard<std::mutex> l(infoM_); return info_; }

    void start(gfcAudioEngine* src) {
        src_ = src; wanted_ = true;
        ensureObjects();
        closeSink();
        pull->resetCounters();
        base_ = 0; played.store(0);
        const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
        if (!dev.isNull()) openOn(dev);
    }
    void stop() { wanted_ = false; closeSink(); }
    void teardown() {
        stop();
        delete poll_; poll_ = nullptr;
        delete pull; pull = nullptr;
    }
    // Default device changed: follow it. Samples the old sink still held count as played, so
    // playedSamples() keeps matching what the engine rendered.
    void devicesChanged() {
        if (!wanted_ || !pull) return;
        const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
        if (dev.isNull()) { closeSink(); return; }
        if (sink_ && dev.id() == deviceId_ && sink_->state() != QAudio::StoppedState) return;
        closeSink();
        base_ = pull->rendered(); played.store(base_);
        openOn(dev);
    }

private:
    void ensureObjects() {
        if (!pull) pull = new EnginePull;
        if (!poll_) {
            poll_ = new QTimer;
            poll_->setTimerType(Qt::PreciseTimer);
            poll_->setInterval(kPollMs);
            QObject::connect(poll_, &QTimer::timeout, this, [this] { pollClock(); });
        }
    }
    bool openOn(const QAudioDevice& dev) {
        QAudioFormat f;
        f.setSampleRate(kEngineRate); f.setChannelCount(gfcAudioEngine::kChannels); f.setSampleFormat(QAudioFormat::Float);
        if (gForceFormat.load()) {
            f.setSampleRate(gForceRate.load()); f.setSampleFormat(QAudioFormat::Int16);
        } else if (!dev.isFormatSupported(f)) {
            f.setSampleFormat(QAudioFormat::Int16);
            if (!dev.isFormatSupported(f)) f = dev.preferredFormat();
        }
        if (!f.isValid() || f.sampleFormat() == QAudioFormat::Unknown || f.bytesPerFrame() <= 0) return false;
        pull->configure(src_, owner_, f);
        if (!pull->isOpen()) pull->open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        sink_ = new QAudioSink(dev, f, this);
        sink_->setBufferSize(int(std::lround(f.sampleRate() * kBufferSeconds)) * f.bytesPerFrame());
        QObject::connect(sink_, &QAudioSink::stateChanged, this, [this](QAudio::State st) {
            if (sink_ && st == QAudio::StoppedState && sink_->error() != QAudio::NoError) active.store(false);
        });
        sink_->start(pull);
        if (sink_->error() != QAudio::NoError || sink_->state() == QAudio::StoppedState) {
            std::fprintf(stderr, "Audio: could not start %s\n", dev.description().toLocal8Bit().constData());
            closeSink();
            return false;
        }
        deviceId_ = dev.id();
        {
            std::lock_guard<std::mutex> l(infoM_);
            const char* type = f.sampleFormat() == QAudioFormat::Float ? "float" : f.sampleFormat() == QAudioFormat::Int16 ? "int16"
                             : f.sampleFormat() == QAudioFormat::Int32 ? "int32" : "uint8";
            info_ = dev.description().toStdString() + ": " + std::to_string(f.sampleRate()) + " Hz, " +
                    std::to_string(f.channelCount()) + " ch, " + type + ", buffer " + std::to_string(sink_->bufferSize()) + " bytes";
        }
        poll_->start();
        active.store(true);
        return true;
    }
    void closeSink() {
        active.store(false);
        if (poll_) poll_->stop();
        if (sink_) { sink_->stop(); delete sink_; sink_ = nullptr; }
        deviceId_.clear();
    }
    void pollClock() {
        if (!sink_ || !pull) return;
        const int64_t p = base_ + sink_->processedUSecs() * kEngineRate / 1000000;
        played.store(std::min(p, pull->rendered()), std::memory_order_release);
    }

    const gfcAudioOutput* owner_;
    gfcAudioEngine* src_ = nullptr;
    bool wanted_ = false;
    QAudioSink* sink_ = nullptr;
    QTimer* poll_ = nullptr;
    QByteArray deviceId_;
    int64_t base_ = 0;
    mutable std::mutex infoM_;
    std::string info_;
};

class QtAudioOutput : public gfcAudioOutput {
public:
    QtAudioOutput() : worker_(new SinkWorker(this)) {
        thread_.setObjectName(QStringLiteral("jefe-audio"));
        worker_->moveToThread(&thread_);
        thread_.start(QThread::TimeCriticalPriority);
        // QMediaDevices on the creating (GUI) thread; changes are handled on
        // the audio thread (queued to the worker).
        devices_ = std::make_unique<QMediaDevices>();
        SinkWorker* w = worker_;
        QObject::connect(devices_.get(), &QMediaDevices::audioOutputsChanged, worker_, [w] { w->devicesChanged(); });
    }
    ~QtAudioOutput() override {
        devices_.reset();
        SinkWorker* w = worker_;
        QMetaObject::invokeMethod(worker_, [w] { w->teardown(); }, Qt::BlockingQueuedConnection);
        thread_.quit();
        thread_.wait();
        delete worker_;
    }

    // Returns false (and stays closed) when there is no output device; a device
    // that appears later is opened by the device-change handler.
    bool open(gfcAudioEngine* source) override {
        SinkWorker* w = worker_;
        QMetaObject::invokeMethod(worker_, [w, source] { w->start(source); }, Qt::QueuedConnection);
        return !QMediaDevices::defaultAudioOutput().isNull();
    }
    void close() override {
        SinkWorker* w = worker_;
        QMetaObject::invokeMethod(worker_, [w] { w->stop(); }, Qt::BlockingQueuedConnection);
    }
    bool isOpen() const override { return worker_->active.load(std::memory_order_acquire); }
    int64_t playedSamples() const override { return worker_->played.load(std::memory_order_acquire); }

    SinkWorker* worker() const { return worker_; }

private:
    QThread thread_;
    SinkWorker* worker_;
    std::unique_ptr<QMediaDevices> devices_;
};

}  // namespace

namespace jefe::qt::audio {

gfcAudioOutput* createOutput() { return new QtAudioOutput; }

void destroyOutput(gfcAudioOutput* out) { delete out; }

int deviceSelfTest() {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    using clock = std::chrono::steady_clock;
    auto msSince = [](clock::time_point t) { return std::chrono::duration<double, std::milli>(clock::now() - t).count(); };
    auto spinUntil = [](auto pred, int ms) {
        const auto until = clock::now() + std::chrono::milliseconds(ms);
        while (!pred() && clock::now() < until) { QCoreApplication::processEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        return pred();
    };
    if (QMediaDevices::defaultAudioOutput().isNull()) {
        std::printf("AUDIO-DEVICE SKIP: no audio output device\n");
        return -1;
    }
    // A very quiet 1 kHz tone (-60 dBFS): audible output is detected, not heard.
    auto tone = std::make_shared<gfcAudioClip>(); tone->reset(0);
    { const int n = 2002 * 96; std::vector<float> s(size_t(n) * 2);
      for (int i = 0; i < n; ++i) s[size_t(2*i)] = s[size_t(2*i+1)] = 0.001f * float(std::sin(2.0 * M_PI * 1000.0 * i / 48000.0));
      tone->append(s.data(), n); }
    // Converted pass: 44.1 kHz int16 through the resampler, a short check.
    QAudioFormat forced; forced.setSampleRate(44100); forced.setChannelCount(2); forced.setSampleFormat(QAudioFormat::Int16);
    if (!QMediaDevices::defaultAudioOutput().isFormatSupported(forced)) {
        std::printf("  note: the device does not accept 44.1 kHz int16; converted-path check skipped\n");
    } else {
        gForceFormat.store(true);
        gfcAudioEngine e; e.setTrackClip(0, tone, 24000, 1001, 96); e.setPlayerRate(24000, 1001);
        auto* out = static_cast<QtAudioOutput*>(createOutput());
        e.setOutput(out);
        const bool opened = out->open(&e) && spinUntil([&] { return out->isOpen(); }, 3000);
        bool ok = opened;
        if (opened) {
            std::printf("  converted: %s\n", out->worker()->formatInfo().c_str());
            spinUntil([&] { return out->playedSamples() > 0; }, 1000);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));   // let the start-up burst settle
            const int64_t p0 = out->playedSamples(); const auto t0 = clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const double ratio = double(out->playedSamples() - p0) / kEngineRate / (msSince(t0) / 1000.0);
            out->worker()->pull->armAudible(); e.playSlice(10);
            const bool heard = spinUntil([&] { const int64_t a = out->worker()->pull->firstAudible(); return a >= 0 && out->playedSamples() >= a; }, 1000);
            std::printf("  converted clock: %.3f s of 48 kHz samples per wall second\n", ratio);
            ok = ratio > 0.9 && ratio < 1.1 && heard && out->worker()->formatInfo().find("44100 Hz, 2 ch, int16") != std::string::npos;
        }
        check(ok, "a 44.1 kHz int16 device plays the engine's 48 kHz through the resampler at the right speed");
        e.setOutput(nullptr);
        destroyOutput(out);
        gForceFormat.store(false);
    }

    gfcAudioEngine e; e.setTrackClip(0, tone, 24000, 1001, 96); e.setPlayerRate(24000, 1001);
    auto* out = static_cast<QtAudioOutput*>(createOutput());
    e.setOutput(out);
    const auto tOpen = clock::now();
    const bool requested = out->open(&e);
    const bool opened = requested && spinUntil([&] { return out->isOpen(); }, 3000);
    const double openMs = msSince(tOpen);
    check(opened, "the default output device opens");
    if (opened) {
        std::printf("  device: %s (open took %.0f ms, off the GUI thread)\n", out->worker()->formatInfo().c_str(), openMs);
        // Clock: playedSamples() advances at wall-clock speed.
        spinUntil([&] { return out->playedSamples() > 0; }, 1000);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));   // let the start-up burst settle
        const int64_t p0 = out->playedSamples(); const auto t0 = clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        const double ratio = double(out->playedSamples() - p0) / kEngineRate / (msSince(t0) / 1000.0);
        std::printf("  clock: %.3f s of samples per wall second\n", ratio);
        check(ratio > 0.9 && ratio < 1.1, "playedSamples advances at wall-clock speed");

        // Step latency: playSlice -> the device has processed the slice's first sample.
        std::vector<double> lat;
        for (int k = 0; k < 10; ++k) {
            out->worker()->pull->armAudible();
            const auto ts = clock::now();
            e.playSlice(10 + k);
            const bool heard = spinUntil([&] { const int64_t a = out->worker()->pull->firstAudible(); return a >= 0 && out->playedSamples() >= a; }, 1000);
            if (heard) lat.push_back(msSince(ts));
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
        }
        std::sort(lat.begin(), lat.end());
        if (!lat.empty())
            std::printf("  step-slice latency (to device processed): min %.1f ms, median %.1f ms, max %.1f ms over %d steps\n",
                        lat.front(), lat[lat.size() / 2], lat.back(), int(lat.size()));
        check(lat.size() == 10 && lat.back() < 60.0, "every step slice reaches the device within 60 ms");

        // Leading on the real device: one second of play lands near frame 25.
        e.updateTransport(gfcAudioTransport{1, 1, 96, LOOPMODELOOP_ID, 1});
        check(e.canLead(), "a native-rate track can lead on the real device");
        e.startLeading(gfcAudioTransport{1, 1, 96, LOOPMODELOOP_ID, 1});
        const auto tl = clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        const double secs = msSince(tl) / 1000.0;
        const int f = e.leaderFrame();
        std::printf("  leading: frame %d after %.3f s (expect ~%.1f minus the buffer)\n", f, secs, 1.0 + secs * 24000.0 / 1001.0);
        check(f >= int(1.0 + secs * 23.976) - 3 && f <= int(1.0 + secs * 23.976) + 1, "the leader follows the device clock");
        e.stopLeading();
    }
    e.setOutput(nullptr);
    destroyOutput(out);
    std::printf("AUDIO-DEVICE: pass=%d fail=%d\n", pass, fail);
    return fail;
}

}  // namespace jefe::qt::audio

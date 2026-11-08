// SPDX-License-Identifier: GPL-2.0-or-later
#include "gfcMovieReader.h"
#include "gfcAudioClip.h"

#include <cmath>
#include <algorithm>
#include <climits>
#include <atomic>
#include <deque>
#include <filesystem>
#include <list>
#include <map>
#include <mutex>
#include <utility>

#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace {

std::string avErr(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(code, buf, sizeof(buf));
    return buf;
}

void setErr(std::string* err, const std::string& msg) {
    if (err) *err = msg;
}

// libav logs to stderr by default; errors reach callers through `err`.
void quietLibavOnce() {
    static std::once_flag once;
    std::call_once(once, [] { av_log_set_level(AV_LOG_FATAL); });
}

gfcYuvMatrix matrixOf(AVColorSpace cs) {
    switch (cs) {
        case AVCOL_SPC_BT709: return gfcYuvMatrix::BT709;
        case AVCOL_SPC_BT470BG:
        case AVCOL_SPC_SMPTE170M: return gfcYuvMatrix::BT601;
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL: return gfcYuvMatrix::BT2020;
        default: return gfcYuvMatrix::Unspecified;
    }
}

gfcYuvRange rangeOf(AVColorRange r) {
    switch (r) {
        case AVCOL_RANGE_MPEG: return gfcYuvRange::Video;
        case AVCOL_RANGE_JPEG: return gfcYuvRange::Full;
        default: return gfcYuvRange::Unspecified;
    }
}

gfcChromaSiting sitingOf(AVChromaLocation loc) {
    switch (loc) {
        case AVCHROMA_LOC_CENTER: return gfcChromaSiting::Center;
        case AVCHROMA_LOC_TOPLEFT: return gfcChromaSiting::TopLeft;
        default: return gfcChromaSiting::Left;  // left is also the MPEG-2/H.264 default
    }
}

std::atomic<bool> gTestFailAudioOpen{false};
std::atomic<bool> gHardwareAllowed{true};

// This platform's hardware decoders, best first.
const AVHWDeviceType kHardwareTypes[] = {
#if defined(__APPLE__)
    AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
#elif defined(_WIN32)
    AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_DXVA2,
#else
    AV_HWDEVICE_TYPE_VAAPI, AV_HWDEVICE_TYPE_CUDA,
#endif
};

// One device per type for the whole process, made on first use and kept until exit. A type whose
// device cannot be made (no GPU, no driver) is not tried again.
AVBufferRef* hardwareDevice(AVHWDeviceType type) {
    static std::mutex m;
    static std::map<int, AVBufferRef*> devices;
    std::lock_guard<std::mutex> lock(m);
    auto it = devices.find(int(type));
    if (it != devices.end()) return it->second;
    AVBufferRef* dev = nullptr;
    if (av_hwdevice_ctx_create(&dev, type, nullptr, nullptr, 0) < 0) dev = nullptr;
    devices[int(type)] = dev;
    return dev;
}

// get_format for a hardware decoder: its surface format when libav offers it. When it doesn't (a
// profile the hardware can't decode) libav's software choice is taken, and decodeNext reports the
// missing hardware frame so the reader goes back to its threaded software decoder.
AVPixelFormat pickHardwareFormat(AVCodecContext* c, const AVPixelFormat* formats) {
    const AVPixelFormat want = *static_cast<const AVPixelFormat*>(c->opaque);
    for (const AVPixelFormat* f = formats; *f != AV_PIX_FMT_NONE; ++f)
        if (*f == want) return *f;
    return avcodec_default_get_format(c, formats);
}

struct FrameDeleter {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};

}  // namespace

struct gfcMovieReader::Impl {
    AVFormatContext* fmt = nullptr;
    AVCodecContext* dec = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws = nullptr;
    int vstream = -1;
    AVRational rate{0, 1};
    int64_t startPts = 0;
    bool eofSent = false;
    int64_t lastIndex = -1;
    std::shared_ptr<AVFrame> last;   // most recently decoded frame (index lastIndex)
    // Every frame a keyframe (ProRes, DNx). Such decoders get slice threads only, so a seek is cheap;
    // a flushed frame-threaded decoder needs about one packet per thread first.
    bool intraOnly = false;
    int64_t packetsSent = 0;         // video packets given to the decoder (self-test hook)
    bool hardwareTried = false;      // the first readFrame() has decided hardware or software
    bool hardwareActive = false;     // dec is a hardware decoder
    AVPixelFormat hardwareFormat = AV_PIX_FMT_NONE;  // its surface format (dec->opaque points here)
    std::string hardwareName;
    int readAheadFd = -1;            // the movie file, opened again only to ask the OS to read ahead
    int64_t readAheadFrom = 0, readAheadTo = 0;  // the byte range already asked for
    static constexpr int64_t kDecodeOnward = 12;   // longest forward skip decoded through rather than sought
    gfcMovieInfo info;
    std::mutex m;

    // Sound. audioEnabled survives release(); the rest is per open file.
    bool audioEnabled = false;
    bool audioOnly = false;            // survives release() too
    int astream = -1;
    AVCodecContext* adec = nullptr;
    AVFrame* aframe = nullptr;
    SwrContext* swr = nullptr;
    std::deque<AVPacket*> videoQueue;  // video packets demuxed while filling audio
    bool demuxEof = false;             // av_read_frame has reported the end
    bool audioEof = false;             // audio decoder and resampler drained
    bool audioPosKnown = false;        // audioEnd valid (set by the first frame after open/seek)
    int64_t audioEnd = 0;              // movie-time 48 kHz index one past the newest sample
    int64_t videoStartSample = 0;      // the first video frame's time at 48 kHz
    std::vector<float> fifo;           // stereo float, ends at audioEnd

    ~Impl() { release(); }

    void clearVideoQueue() {
        for (AVPacket* q : videoQueue) av_packet_free(&q);
        videoQueue.clear();
    }

    void resetAudioState() {
        clearVideoQueue();
        demuxEof = false;
        audioEof = false;
        audioPosKnown = false;
        audioEnd = 0;
        fifo.clear();
        if (adec) avcodec_flush_buffers(adec);
        swr_free(&swr);  // rebuilt from the next decoded frame
    }

    void releaseAudio() {
        resetAudioState();
        av_frame_free(&aframe);
        avcodec_free_context(&adec);
        astream = -1;
    }

    void release() {
        releaseAudio();
        last.reset();
        lastIndex = -1;
        eofSent = false;
        vstream = -1;
        packetsSent = 0;
        hardwareTried = false;
        hardwareActive = false;
        hardwareFormat = AV_PIX_FMT_NONE;
        hardwareName.clear();
#if defined(__APPLE__) || defined(__linux__)
        if (readAheadFd >= 0) ::close(readAheadFd);
#endif
        readAheadFd = -1;
        readAheadFrom = readAheadTo = 0;
        if (sws) { sws_freeContext(sws); sws = nullptr; }
        av_packet_free(&pkt);
        avcodec_free_context(&dec);
        if (fmt) avformat_close_input(&fmt);
        info = gfcMovieInfo{};
    }

    AVStream* stream() const { return fmt->streams[vstream]; }

    // Asks the OS to read the next kReadAhead bytes after the demuxer's position in the background
    // (F_RDADVISE / POSIX_FADV_WILLNEED), so the disk works while the decoder does instead of each
    // frame's read waiting on it. Topped up once half the range is used; a seek starts a new range.
    static constexpr int64_t kReadAhead = 128 << 20;
    void readAhead() {
#if defined(__APPLE__) || defined(__linux__)
        if (readAheadFd < 0 || !fmt || !fmt->pb) return;
        const int64_t pos = avio_tell(fmt->pb);
        if (pos < 0) return;
        if (pos < readAheadFrom || pos > readAheadTo) readAheadFrom = readAheadTo = pos;
        if (readAheadTo - pos > kReadAhead / 2) return;
        const int64_t from = std::max(pos, readAheadTo), to = pos + kReadAhead;
#if defined(__APPLE__)
        radvisory ra{};
        ra.ra_offset = off_t(from);
        ra.ra_count = int(to - from);
        fcntl(readAheadFd, F_RDADVISE, &ra);
#else
        posix_fadvise(readAheadFd, off_t(from), off_t(to - from), POSIX_FADV_WILLNEED);
#endif
        readAheadTo = to;
#endif
    }

    int64_t tsOf(int64_t index) const {
        return startPts + av_rescale_q(index, av_inv_q(rate), stream()->time_base);
    }

    int64_t indexOf(int64_t pts) const {
        return av_rescale_q_rnd(pts - startPts, stream()->time_base, av_inv_q(rate),
                                AV_ROUND_NEAR_INF);
    }

    // Opens `dec`: software with threads, or on `device` (surface format `surface`) single-threaded.
    bool openDecoder(std::string* err, AVBufferRef* device = nullptr, AVPixelFormat surface = AV_PIX_FMT_NONE) {
        const AVCodecParameters* par = stream()->codecpar;
        const AVCodec* codec = avcodec_find_decoder(par->codec_id);
        if (!codec) { setErr(err, "no decoder for this video codec"); return false; }
        dec = avcodec_alloc_context3(codec);
        if (!dec) { setErr(err, "out of memory"); return false; }
        int ret = avcodec_parameters_to_context(dec, par);
        if (ret < 0) { setErr(err, "decoder parameters: " + avErr(ret)); return false; }
        const AVCodecDescriptor* cd = avcodec_descriptor_get(par->codec_id);
        intraOnly = cd && (cd->props & AV_CODEC_PROP_INTRA_ONLY);
        if (device) {
            dec->hw_device_ctx = av_buffer_ref(device);
            if (!dec->hw_device_ctx) { setErr(err, "out of memory"); return false; }
            hardwareFormat = surface;
            dec->opaque = &hardwareFormat;
            dec->get_format = pickHardwareFormat;
            dec->thread_count = 1;
        } else {
            dec->thread_count = 0;
            // Slice threads only for intra-only codecs that support them: frame threads hold back ~thread_count
            // packets after each flush. Long-GOP and slice-less decoders keep frame threads.
            const bool sliceOnly = intraOnly && (codec->capabilities & AV_CODEC_CAP_SLICE_THREADS);
            dec->thread_type = sliceOnly ? FF_THREAD_SLICE : (FF_THREAD_SLICE | FF_THREAD_FRAME);
        }
        dec->pkt_timebase = stream()->time_base;
        ret = avcodec_open2(dec, codec, nullptr);
        if (ret < 0) { setErr(err, "cannot open decoder: " + avErr(ret)); return false; }
        return true;
    }

    // Replaces the software decoder with the first hardware one that can decode this stream; false
    // (software kept) when none can. Files with alpha stay in software: hardware decoders drop it
    // (VideoToolbox decodes ProRes 4444 to P416). The caller seeks afterwards.
    bool startHardware() {
        if (info.hasAlpha) return false;
        const AVCodec* codec = avcodec_find_decoder(stream()->codecpar->codec_id);
        if (!codec) return false;
        for (AVHWDeviceType type : kHardwareTypes) {
            const AVCodecHWConfig* cfg = nullptr;
            for (int k = 0; (cfg = avcodec_get_hw_config(codec, k)); ++k)
                if (cfg->device_type == type && (cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) break;
            if (!cfg) continue;
            AVBufferRef* device = hardwareDevice(type);
            if (!device) continue;
            AVCodecContext* software = dec;
            dec = nullptr;
            if (openDecoder(nullptr, device, cfg->pix_fmt)) {
                avcodec_free_context(&software);
                hardwareActive = true;
                hardwareName = av_hwdevice_get_type_name(type);
                return true;
            }
            avcodec_free_context(&dec);
            dec = software;
        }
        return false;
    }

    // Back to a threaded software decoder for the rest of this open file. The caller seeks afterwards.
    bool stopHardware(std::string* err) {
        avcodec_free_context(&dec);
        hardwareActive = false;
        info.hardwareDecoder.clear();
        return openDecoder(err);
    }

    // A hardware frame as a CPU-readable one: mapped in place where the platform allows it (VideoToolbox:
    // the decoder's own buffer, no copy), else copied. Timestamps and colour tags come along.
    static bool readableFrame(AVFrame* f, std::string* err) {
        AVFrame* sw = av_frame_alloc();
        if (!sw) { setErr(err, "out of memory"); return false; }
        int ret = av_hwframe_map(sw, f, AV_HWFRAME_MAP_READ);
        if (ret < 0) {
            av_frame_unref(sw);
            ret = av_hwframe_transfer_data(sw, f, 0);
        }
        if (ret >= 0) ret = av_frame_copy_props(sw, f);
        if (ret < 0) {
            av_frame_free(&sw);
            setErr(err, "cannot read hardware frame: " + avErr(ret));
            return false;
        }
        av_frame_unref(f);
        av_frame_move_ref(f, sw);
        av_frame_free(&sw);
        return true;
    }

    // Next frame in presentation order; false at end of stream or on a decode error (err set).
    bool decodeNext(AVFrame* out, std::string* err) {
        for (;;) {
            int ret = avcodec_receive_frame(dec, out);
            if (ret == 0 && hardwareActive) {
                if (out->format != hardwareFormat) { setErr(err, "hardware decoder not used"); return false; }
                return readableFrame(out, err);
            }
            if (ret == 0) return true;
            if (ret == AVERROR_EOF) { setErr(err, "end of stream"); return false; }
            if (ret != AVERROR(EAGAIN)) { setErr(err, "decode error: " + avErr(ret)); return false; }
            if (eofSent) { setErr(err, "end of stream"); return false; }
            if (!nextVideoPacket()) {
                // End of file or unreadable data: drain what the decoder holds.
                avcodec_send_packet(dec, nullptr);
                eofSent = true;
                continue;
            }
            ret = avcodec_send_packet(dec, pkt);
            ++packetsSent;
            av_packet_unref(pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_INVALIDDATA) {
                setErr(err, "decode error: " + avErr(ret));
                return false;
            }
        }
    }

    // The next video packet into `pkt`: queued ones first, then from the file (decoding audio
    // packets met on the way). False at the end of the file.
    bool nextVideoPacket() {
        if (!videoQueue.empty()) {
            AVPacket* q = videoQueue.front();
            videoQueue.pop_front();
            av_packet_move_ref(pkt, q);
            av_packet_free(&q);
            return true;
        }
        for (;;) {
            if (demuxEof) return false;
            if (av_read_frame(fmt, pkt) < 0) { onDemuxEof(); return false; }
            if (pkt->stream_index == vstream) return true;
            if (adec && pkt->stream_index == astream) decodeAudioPacket(pkt);
            av_packet_unref(pkt);
        }
    }

    void onDemuxEof() {
        demuxEof = true;
        if (!adec || audioEof) return;
        decodeAudioPacket(nullptr);  // drain the decoder
        if (swr) convertAudio(nullptr);  // and the resampler's tail
        audioEof = true;
    }

    // Demuxes until the FIFO reaches `target` (a 48 kHz movie-time index), queuing video packets met.
    // Stops kAudioLookahead past the target so audio that ends early never queues the whole file.
    static constexpr int64_t kAudioLookahead = 2 * gfcAudioClip::kRate;
    void pumpAudio(int64_t target) {
        if (!adec) return;
        const AVRational vtb = stream()->time_base;
        while (!audioEof && !(audioPosKnown && audioEnd >= target)) {
            if (demuxEof) { onDemuxEof(); break; }
            if (av_read_frame(fmt, pkt) < 0) { onDemuxEof(); break; }
            if (pkt->stream_index == vstream) {
                const int64_t ts = pkt->dts != AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
                AVPacket* q = av_packet_alloc();
                if (!q) { av_packet_unref(pkt); break; }
                av_packet_move_ref(q, pkt);
                videoQueue.push_back(q);
                if (ts != AV_NOPTS_VALUE &&
                    av_rescale_q(ts, vtb, AVRational{1, gfcAudioClip::kRate}) - videoStartSample > target + kAudioLookahead)
                    break;
                continue;
            }
            if (pkt->stream_index == astream) decodeAudioPacket(pkt);
            av_packet_unref(pkt);
        }
    }

    // Sends one packet (null = drain) and converts every frame it yields.
    // Corrupt audio is skipped; the picture never fails because of sound.
    void decodeAudioPacket(const AVPacket* p) {
        if (avcodec_send_packet(adec, p) < 0 && p) return;
        while (avcodec_receive_frame(adec, aframe) == 0) {
            convertAudio(aframe);
            av_frame_unref(aframe);
        }
    }

    // Resamples a decoded frame (null = flush) into the FIFO at 48 kHz stereo
    // float; libswresample downmixes and converts the rate.
    void convertAudio(const AVFrame* f) {
        if (f && !swr) {
            AVChannelLayout in{};
            if (f->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC || f->ch_layout.nb_channels <= 0)
                av_channel_layout_default(&in, std::max(1, f->ch_layout.nb_channels));
            else if (av_channel_layout_copy(&in, &f->ch_layout) < 0)
                return;
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            const int ret = swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, gfcAudioClip::kRate, &in,
                                                AVSampleFormat(f->format), f->sample_rate, 0, nullptr);
            av_channel_layout_uninit(&in);
            if (ret < 0 || swr_init(swr) < 0) { swr_free(&swr); return; }
        }
        if (!swr) return;
        if (f && !audioPosKnown) {
            const int64_t pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
            audioEnd = pts == AV_NOPTS_VALUE
                           ? 0
                           : av_rescale_q(pts, fmt->streams[astream]->time_base, AVRational{1, gfcAudioClip::kRate}) -
                                 videoStartSample;
            audioPosKnown = true;
        }
        const int inSamples = f ? f->nb_samples : 0;
        const int cap = swr_get_out_samples(swr, inSamples);
        if (cap <= 0) return;
        const size_t old = fifo.size();
        fifo.resize(old + size_t(cap) * gfcAudioClip::kChannels);
        uint8_t* outPtr = reinterpret_cast<uint8_t*>(fifo.data() + old);
        const int n = swr_convert(swr, &outPtr, cap, f ? const_cast<const uint8_t**>(f->extended_data) : nullptr,
                                  inSamples);
        fifo.resize(old + size_t(std::max(n, 0)) * gfcAudioClip::kChannels);
        if (n > 0) audioEnd += n;
    }

    // On failure every audio member is freed: an unopened AVCodecContext must never reach
    // avcodec_flush_buffers, so adec is non-null only when it is open.
    bool openAudio() {
        astream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (astream < 0) return false;
        const AVCodecParameters* par = fmt->streams[astream]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(par->codec_id);
        AVCodecContext* ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
        aframe = av_frame_alloc();
        bool ok = ctx && aframe && avcodec_parameters_to_context(ctx, par) >= 0;
        if (ok) {
            ctx->pkt_timebase = fmt->streams[astream]->time_base;
            ok = !gTestFailAudioOpen.load() && avcodec_open2(ctx, codec, nullptr) >= 0;
        }
        if (!ok) {
            avcodec_free_context(&ctx);
            av_frame_free(&aframe);
            astream = -1;
            return false;
        }
        adec = ctx;
        return true;
    }

    int64_t frameIndexOf(const AVFrame* f, int64_t fallback) const {
        const int64_t pts = f->best_effort_timestamp;
        return pts == AV_NOPTS_VALUE ? fallback : indexOf(pts);
    }

    // Decodes forward until the frame at or after `index`; leaves it in `last`.
    bool decodeUntil(int64_t index, std::string* err) {
        int64_t expected = lastIndex + 1;
        for (;;) {
            std::shared_ptr<AVFrame> f(av_frame_alloc(), FrameDeleter{});
            if (!f) { setErr(err, "out of memory"); return false; }
            if (!decodeNext(f.get(), err)) return false;
            const int64_t idx = frameIndexOf(f.get(), expected);
            expected = idx + 1;
            if (idx >= index) {
                // After a timestamp gap the frame found can be later than asked: record its real index so the
                // next sequential read doesn't shift every later frame by the gap.
                last = std::move(f);
                lastIndex = idx;
                return true;
            }
        }
    }

    bool seekTo(int64_t index, std::string* err) {
        last.reset();
        int ret = av_seek_frame(fmt, vstream, tsOf(index), AVSEEK_FLAG_BACKWARD);
        if (ret < 0) { setErr(err, "seek failed: " + avErr(ret)); return false; }
        avcodec_flush_buffers(dec);
        eofSent = false;
        resetAudioState();
        // Unknown position until the first frame arrives; decodeUntil reads pts.
        lastIndex = -1;
        return true;
    }

    void fillFrame(gfcMovieFrame& out) const {
        const AVFrame* f = last.get();
        const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(AVPixelFormat(f->format));
        out = gfcMovieFrame{};
        out.index = lastIndex;
        out.width = f->width;
        out.height = f->height;
        out.bitDepth = desc ? desc->comp[0].depth + desc->comp[0].shift : 8;
        out.planeCount = std::min(av_pix_fmt_count_planes(AVPixelFormat(f->format)), 4);
        const bool yuv = desc && (desc->flags & AV_PIX_FMT_FLAG_PLANAR) && !(desc->flags & AV_PIX_FMT_FLAG_RGB) &&
                         desc->nb_components >= 3;
        out.planarYuv = yuv;
        // NV12 and friends: Cb and Cr share plane 1.
        out.chromaInterleaved = yuv && out.planeCount == 2 && desc->comp[1].plane == 1 && desc->comp[2].plane == 1;
        out.subX = desc ? 1 << desc->log2_chroma_w : 1;
        out.subY = desc ? 1 << desc->log2_chroma_h : 1;
        for (int p = 0; p < out.planeCount; ++p) {
            const bool chroma = desc && (p == 1 || p == 2) && !(desc->flags & AV_PIX_FMT_FLAG_RGB);
            out.plane[p].data = f->data[p];
            out.plane[p].width = chroma ? AV_CEIL_RSHIFT(f->width, desc->log2_chroma_w) : f->width;
            out.plane[p].height = chroma ? AV_CEIL_RSHIFT(f->height, desc->log2_chroma_h) : f->height;
            out.plane[p].strideBytes = f->linesize[p];
        }
        out.owner = last;  // shared_ptr<AVFrame> -> shared_ptr<void>, same deleter
    }
};

gfcMovieReader::gfcMovieReader() : d_(std::make_unique<Impl>()) {}
gfcMovieReader::~gfcMovieReader() = default;

bool gfcMovieReader::isOpen() const { return d_->fmt != nullptr; }
const gfcMovieInfo& gfcMovieReader::info() const { return d_->info; }

void gfcMovieReader::close() {
    std::lock_guard<std::mutex> lock(d_->m);
    d_->release();
}

bool gfcMovieReader::open(const std::string& path, std::string* err) {
    quietLibavOnce();
    std::lock_guard<std::mutex> lock(d_->m);
    Impl& d = *d_;
    d.release();
    auto fail = [&](const std::string& msg) {
        setErr(err, msg);
        d.release();
        return false;
    };

    int ret = avformat_open_input(&d.fmt, path.c_str(), nullptr, nullptr);
    if (ret < 0) { d.fmt = nullptr; return fail("cannot open " + path + ": " + avErr(ret)); }
#if defined(__APPLE__) || defined(__linux__)
    d.readAheadFd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
#endif
    // Audio only: skip every non-audio stream so the probe below decodes no picture (hundreds of ms
    // on 4K ProRes 4444); the container header still gives the video rate.
    if (d.audioOnly)
        for (unsigned k = 0; k < d.fmt->nb_streams; ++k)
            if (d.fmt->streams[k]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
                d.fmt->streams[k]->discard = AVDISCARD_ALL;
    ret = avformat_find_stream_info(d.fmt, nullptr);
    if (ret < 0) return fail("cannot read stream info: " + avErr(ret));
    d.vstream = av_find_best_stream(d.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (d.vstream < 0) return fail("no video stream in " + path);
    if (!d.audioOnly && !d.openDecoder(err)) { d.release(); return false; }
    d.pkt = av_packet_alloc();
    if (!d.pkt) return fail("out of memory");

    AVStream* st = d.stream();
    AVRational avg = st->avg_frame_rate;
    if (avg.num <= 0 || avg.den <= 0) avg = st->r_frame_rate;
    if (avg.num <= 0 || avg.den <= 0) return fail("unknown frame rate");
    int num = 0, den = 1;
    av_reduce(&num, &den, avg.num, avg.den, INT_MAX);
    d.rate = AVRational{num, den};
    d.startPts = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
    d.videoStartSample = av_rescale_q(d.startPts, st->time_base, AVRational{1, gfcAudioClip::kRate});
    // Sound is optional: a movie whose audio cannot be decoded still plays.
    const bool audioUndecodable = (d.audioEnabled || d.audioOnly) && !d.openAudio();
    if (audioUndecodable) d.releaseAudio();
    // Audio only: the demuxer also skips the audio streams not chosen (the
    // loop above could not know which one openAudio would pick).
    if (d.audioOnly)
        for (unsigned k = 0; k < d.fmt->nb_streams; ++k)
            if (int(k) != d.astream) d.fmt->streams[k]->discard = AVDISCARD_ALL;

    gfcMovieInfo& i = d.info;
    i.width = st->codecpar->width;
    i.height = st->codecpar->height;
    i.rateNum = num;
    i.rateDen = den;
    i.variableRate = av_cmp_q(st->r_frame_rate, st->avg_frame_rate) != 0;
    if (st->duration != AV_NOPTS_VALUE && st->duration > 0)
        i.durationSeconds = st->duration * av_q2d(st->time_base);
    else if (d.fmt->duration != AV_NOPTS_VALUE && d.fmt->duration > 0)
        i.durationSeconds = double(d.fmt->duration) / AV_TIME_BASE;
    i.frameCount = st->nb_frames > 0 ? st->nb_frames
                                     : std::llround(i.durationSeconds * num / den);
    i.codecName = avcodec_get_name(st->codecpar->codec_id);

    // Start timecode: the first "timecode" tag over the format metadata, then
    // every stream's metadata (a mov tmcd track surfaces on its own stream).
    {
        const AVDictionaryEntry* tag = av_dict_get(d.fmt->metadata, "timecode", nullptr, 0);
        for (unsigned k = 0; !tag && k < d.fmt->nb_streams; ++k)
            tag = av_dict_get(d.fmt->streams[k]->metadata, "timecode", nullptr, 0);
        if (tag && tag->value) i.startTimecode = tag->value;
    }

    const int astream = av_find_best_stream(d.fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (astream >= 0) {
        const AVCodecParameters* ap = d.fmt->streams[astream]->codecpar;
        i.hasAudio = true;
        i.audioSampleRate = ap->sample_rate;
        i.audioChannels = ap->ch_layout.nb_channels;
    }
    // An audio stream that cannot be decoded reports as no audio, so the
    // track gets no clip and the picture plays on.
    if (audioUndecodable) i.hasAudio = false;

    if (d.audioOnly) return true;  // no picture: the pixel facts stay at their defaults

    // Pixel format and colour tags come from the first decoded frame: ProRes
    // carries them only in the frame header, not in the container.
    std::string decErr;
    if (!d.decodeUntil(0, &decErr)) return fail("cannot decode first frame: " + decErr);
    const AVFrame* f = d.last.get();
    const AVPixelFormat pf = AVPixelFormat(f->format);
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(pf);
    if (!desc) return fail("unknown pixel format");
    i.width = f->width;
    i.height = f->height;
    i.pixFmt = desc->name;
    i.bitDepth = desc->comp[0].depth;
    i.subX = 1 << desc->log2_chroma_w;
    i.subY = 1 << desc->log2_chroma_h;
    i.hasAlpha = (desc->flags & AV_PIX_FMT_FLAG_ALPHA) != 0;
    i.planarYuv = (desc->flags & AV_PIX_FMT_FLAG_PLANAR) && !(desc->flags & AV_PIX_FMT_FLAG_RGB) &&
                  desc->nb_components >= 3;
    i.matrix = matrixOf(f->colorspace);
    if (i.matrix == gfcYuvMatrix::Unspecified) i.matrix = matrixOf(st->codecpar->color_space);
    i.range = rangeOf(f->color_range);
    if (i.range == gfcYuvRange::Unspecified) i.range = rangeOf(st->codecpar->color_range);
    i.siting = sitingOf(f->chroma_location != AVCHROMA_LOC_UNSPECIFIED ? f->chroma_location
                                                                        : st->codecpar->chroma_location);
    return true;
}

bool gfcMovieReader::readFrame(int64_t index, gfcMovieFrame& out, std::string* err) {
    std::lock_guard<std::mutex> lock(d_->m);
    Impl& d = *d_;
    if (!d.fmt) { setErr(err, "movie not open"); return false; }
    if (!d.dec) { setErr(err, "audio-only reader"); return false; }
    if (index < 0 || index >= d.info.frameCount) {
        setErr(err, "frame " + std::to_string(index) + " out of range");
        return false;
    }
    // The first read picks the decoder; open() decoded frame 0 in software for the stream facts.
    if (!d.hardwareTried) {
        d.hardwareTried = true;
        if (gHardwareAllowed.load() && d.startHardware()) {
            d.last.reset();
            d.lastIndex = -1;
        }
    }
    if (!(d.last && index == d.lastIndex)) {
        // A short skip forward in a long-GOP stream decodes on (a seek would go
        // back to the keyframe and decode further); an intra-only stream seeks.
        const bool onward = d.last && index > d.lastIndex &&
            (index == d.lastIndex + 1 || (!d.intraOnly && index - d.lastIndex <= Impl::kDecodeOnward));
        bool ok = (onward || d.seekTo(index, err)) && d.decodeUntil(index, err);
        // A hardware frame of another size counts as a failure (a decoder that crops odd sizes).
        if (ok && d.hardwareActive && (d.last->width != d.info.width || d.last->height != d.info.height)) {
            setErr(err, "hardware frame size differs");
            ok = false;
        }
        if (!ok && d.hardwareActive)
            ok = d.stopHardware(err) && d.seekTo(index, err) && d.decodeUntil(index, err);
        if (!ok) {
            d.last.reset();
            d.lastIndex = -1;
            return false;
        }
        if (d.hardwareActive) d.info.hardwareDecoder = d.hardwareName;
    }
    d.pumpAudio(gfcAudioClip::frameStartSample(index + 1, d.info.rateNum, d.info.rateDen));
    d.readAhead();
    d.fillFrame(out);
    return true;
}

void gfcMovieReader::testFailAudioDecoderOpen(bool fail) { gTestFailAudioOpen = fail; }

void gfcMovieReader::setHardwareDecodingAllowed(bool allowed) { gHardwareAllowed = allowed; }
bool gfcMovieReader::hardwareDecodingAllowed() { return gHardwareAllowed.load(); }

int64_t gfcMovieReader::readAheadThrough() const {
    std::lock_guard<std::mutex> lock(d_->m);
    return d_->readAheadTo;
}

int64_t gfcMovieReader::videoPacketsSent() const {
    std::lock_guard<std::mutex> lock(d_->m);
    return d_->packetsSent;
}

void gfcMovieReader::setAudioEnabled(bool on) {
    std::lock_guard<std::mutex> lock(d_->m);
    d_->audioEnabled = on;
}

void gfcMovieReader::setAudioOnly(bool on) {
    std::lock_guard<std::mutex> lock(d_->m);
    d_->audioOnly = on;
}

bool gfcMovieReader::readAudioUntil(int64_t sampleEnd, std::string* err) {
    std::lock_guard<std::mutex> lock(d_->m);
    Impl& d = *d_;
    if (!d.fmt) { setErr(err, "movie not open"); return false; }
    if (!d.adec) { setErr(err, "no audio decoder"); return false; }
    while (!d.audioEof && !(d.audioPosKnown && d.audioEnd >= sampleEnd)) {
        if (d.demuxEof) { d.onDemuxEof(); break; }
        const int ret = av_read_frame(d.fmt, d.pkt);
        if (ret < 0) {
            d.onDemuxEof();
            if (ret != AVERROR_EOF) { setErr(err, "read error: " + avErr(ret)); return false; }
            break;
        }
        if (d.pkt->stream_index == d.astream) d.decodeAudioPacket(d.pkt);
        av_packet_unref(d.pkt);  // video and every other stream: dropped
    }
    d.readAhead();
    return true;
}

bool gfcMovieReader::seekAudio(int64_t frameIndex, std::string* err) {
    std::lock_guard<std::mutex> lock(d_->m);
    Impl& d = *d_;
    if (!d.fmt) { setErr(err, "movie not open"); return false; }
    if (!d.adec) { setErr(err, "no audio decoder"); return false; }
    // AAC and friends overlap frames: the first frame decoded after a flush
    // is a warm-up, so start a little early and let the caller drop it.
    constexpr int64_t kPreRoll = 4096;
    const int64_t sample =
        std::max<int64_t>(0, gfcAudioClip::frameStartSample(frameIndex, d.info.rateNum, d.info.rateDen) - kPreRoll);
    const AVRational atb = d.fmt->streams[d.astream]->time_base;
    const int64_t ts = av_rescale_q(sample + d.videoStartSample, AVRational{1, gfcAudioClip::kRate}, atb);
    const int ret = av_seek_frame(d.fmt, d.astream, ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) { setErr(err, "audio seek failed: " + avErr(ret)); return false; }
    if (d.dec) {  // a full reader: its picture position is gone too
        avcodec_flush_buffers(d.dec);
        d.eofSent = false;
        d.last.reset();
        d.lastIndex = -1;
    }
    d.resetAudioState();
    return true;
}

int64_t gfcMovieReader::queuedAudioFrames() const {
    std::lock_guard<std::mutex> lock(d_->m);
    return int64_t(d_->fifo.size() / gfcAudioClip::kChannels);
}

bool gfcMovieReader::takeAudio(std::vector<float>& interleaved, int64_t& firstSample) {
    std::lock_guard<std::mutex> lock(d_->m);
    Impl& d = *d_;
    if (d.fifo.empty()) return false;
    firstSample = d.audioEnd - int64_t(d.fifo.size() / gfcAudioClip::kChannels);
    interleaved.swap(d.fifo);
    d.fifo.clear();
    return true;
}

bool gfcMovieReader::convertToBGRA(const gfcMovieFrame& in, int bitsPerComponent, int outW, int outH,
                                   std::vector<uint8_t>& out, std::string* err) {
    if (!in.owner) { setErr(err, "empty frame"); return false; }
    if (outW <= 0 || outH <= 0) { setErr(err, "bad output size"); return false; }
    if (bitsPerComponent != 8 && bitsPerComponent != 16) {
        setErr(err, "bitsPerComponent must be 8 or 16");
        return false;
    }
    std::lock_guard<std::mutex> lock(d_->m);
    Impl& d = *d_;
    const AVFrame* f = static_cast<const AVFrame*>(in.owner.get());
    const AVPixelFormat dstFmt = bitsPerComponent == 16 ? AV_PIX_FMT_BGRA64LE : AV_PIX_FMT_BGRA;
    d.sws = sws_getCachedContext(d.sws, f->width, f->height, AVPixelFormat(f->format), outW, outH,
                                 dstFmt, SWS_BICUBIC | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT,
                                 nullptr, nullptr, nullptr);
    if (!d.sws) { setErr(err, "cannot create colour converter"); return false; }

    int cs = SWS_CS_ITU709;  // untagged defaults to Rec.709
    if (d.info.matrix == gfcYuvMatrix::BT601) cs = SWS_CS_ITU601;
    else if (d.info.matrix == gfcYuvMatrix::BT2020) cs = SWS_CS_BT2020;
    const int srcFull = d.info.range == gfcYuvRange::Full ? 1 : 0;  // untagged = video range
    sws_setColorspaceDetails(d.sws, sws_getCoefficients(cs), srcFull,
                             sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16);

    const int bpp = bitsPerComponent == 16 ? 8 : 4;
    out.resize(size_t(outW) * size_t(outH) * bpp);
    uint8_t* dst[4] = {out.data(), nullptr, nullptr, nullptr};
    int dstStride[4] = {outW * bpp, 0, 0, 0};
    const int rows = sws_scale(d.sws, f->data, f->linesize, 0, f->height, dst, dstStride);
    if (rows != outH) { setErr(err, "colour conversion failed"); return false; }
    return true;
}

bool gfcMovieReader::probe(const std::string& path, gfcMovieInfo& out, std::string* err) {
    namespace fs = std::filesystem;
    struct Entry { std::string path; uintmax_t size; fs::file_time_type mtime; gfcMovieInfo info; };
    static std::mutex cacheMutex;
    static std::list<Entry> cache;  // most recent first
    constexpr size_t kCacheSize = 16;

    std::error_code ec;
    const uintmax_t size = fs::file_size(path, ec);
    if (ec) { setErr(err, "cannot read " + path + ": " + ec.message()); return false; }
    if (size == 0) { setErr(err, path + " is empty"); return false; }
    const fs::file_time_type mtime = fs::last_write_time(path, ec);
    if (ec) { setErr(err, "cannot read " + path + ": " + ec.message()); return false; }

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        for (auto it = cache.begin(); it != cache.end(); ++it) {
            if (it->path == path && it->size == size && it->mtime == mtime) {
                cache.splice(cache.begin(), cache, it);
                out = cache.front().info;
                return true;
            }
        }
    }

    gfcMovieReader r;
    if (!r.open(path, err)) return false;
    out = r.info();

    std::lock_guard<std::mutex> lock(cacheMutex);
    cache.remove_if([&](const Entry& e) { return e.path == path; });
    cache.push_front(Entry{path, size, mtime, out});
    if (cache.size() > kCacheSize) cache.pop_back();
    return true;
}

std::string gfcMovieReader::libavVersionString() {
    return "libavcodec " + std::to_string(LIBAVCODEC_VERSION_MAJOR) + "." +
           std::to_string(LIBAVCODEC_VERSION_MINOR);
}

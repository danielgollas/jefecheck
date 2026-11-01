#include "gfcMovieReader.h"

#include <cmath>
#include <algorithm>
#include <climits>
#include <filesystem>
#include <list>
#include <mutex>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
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
    gfcMovieInfo info;
    std::mutex m;

    ~Impl() { release(); }

    void release() {
        last.reset();
        lastIndex = -1;
        eofSent = false;
        vstream = -1;
        if (sws) { sws_freeContext(sws); sws = nullptr; }
        av_packet_free(&pkt);
        avcodec_free_context(&dec);
        if (fmt) avformat_close_input(&fmt);
        info = gfcMovieInfo{};
    }

    AVStream* stream() const { return fmt->streams[vstream]; }

    int64_t tsOf(int64_t index) const {
        return startPts + av_rescale_q(index, av_inv_q(rate), stream()->time_base);
    }

    int64_t indexOf(int64_t pts) const {
        return av_rescale_q_rnd(pts - startPts, stream()->time_base, av_inv_q(rate),
                                AV_ROUND_NEAR_INF);
    }

    // Decoder setup lives here only: hardware decode (hw_device_ctx +
    // get_format) would be added in this one place.
    bool openDecoder(std::string* err) {
        const AVCodecParameters* par = stream()->codecpar;
        const AVCodec* codec = avcodec_find_decoder(par->codec_id);
        if (!codec) { setErr(err, "no decoder for this video codec"); return false; }
        dec = avcodec_alloc_context3(codec);
        if (!dec) { setErr(err, "out of memory"); return false; }
        int ret = avcodec_parameters_to_context(dec, par);
        if (ret < 0) { setErr(err, "decoder parameters: " + avErr(ret)); return false; }
        dec->thread_count = 0;
        dec->thread_type = FF_THREAD_SLICE | FF_THREAD_FRAME;
        dec->pkt_timebase = stream()->time_base;
        ret = avcodec_open2(dec, codec, nullptr);
        if (ret < 0) { setErr(err, "cannot open decoder: " + avErr(ret)); return false; }
        return true;
    }

    // Next frame in presentation order. False at end of stream or on a decode
    // error (truncated data), with err set. A software frame is returned; a
    // future hwaccel path would call av_hwframe_transfer_data here.
    bool decodeNext(AVFrame* out, std::string* err) {
        for (;;) {
            int ret = avcodec_receive_frame(dec, out);
            if (ret == 0) return true;
            if (ret == AVERROR_EOF) { setErr(err, "end of stream"); return false; }
            if (ret != AVERROR(EAGAIN)) { setErr(err, "decode error: " + avErr(ret)); return false; }
            if (eofSent) { setErr(err, "end of stream"); return false; }
            ret = av_read_frame(fmt, pkt);
            if (ret < 0) {
                // End of file or unreadable data: drain what the decoder holds.
                avcodec_send_packet(dec, nullptr);
                eofSent = true;
                continue;
            }
            if (pkt->stream_index == vstream) ret = avcodec_send_packet(dec, pkt);
            av_packet_unref(pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_INVALIDDATA) {
                setErr(err, "decode error: " + avErr(ret));
                return false;
            }
        }
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
                last = std::move(f);
                lastIndex = index;
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
        out.bitDepth = desc ? desc->comp[0].depth : 8;
        out.planarYuv = info.planarYuv;
        out.planeCount = std::min(av_pix_fmt_count_planes(AVPixelFormat(f->format)), 4);
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
    ret = avformat_find_stream_info(d.fmt, nullptr);
    if (ret < 0) return fail("cannot read stream info: " + avErr(ret));
    d.vstream = av_find_best_stream(d.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (d.vstream < 0) return fail("no video stream in " + path);
    if (!d.openDecoder(err)) { d.release(); return false; }
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
    if (index < 0 || index >= d.info.frameCount) {
        setErr(err, "frame " + std::to_string(index) + " out of range");
        return false;
    }
    if (!(d.last && index == d.lastIndex)) {
        if (!(d.last && index == d.lastIndex + 1) && !d.seekTo(index, err)) return false;
        if (!d.decodeUntil(index, err)) {
            d.last.reset();
            d.lastIndex = -1;
            return false;
        }
    }
    d.fillFrame(out);
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

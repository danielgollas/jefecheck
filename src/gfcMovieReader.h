// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Movie decoding via libav*. libav headers are confined to gfcMovieReader.cpp
// (developer_notes §1), so this header stays include-safe everywhere.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class gfcYuvMatrix  { BT709, BT601, BT2020, Unspecified };
enum class gfcYuvRange   { Video, Full, Unspecified };
enum class gfcChromaSiting { Left, Center, TopLeft };

struct gfcMovieInfo {
    int width = 0, height = 0; int64_t frameCount = 0;
    int rateNum = 0, rateDen = 1; bool variableRate = false;
    int bitDepth = 8; int subX = 1, subY = 1; bool hasAlpha = false; bool planarYuv = false;
    std::string pixFmt, codecName, startTimecode;
    gfcYuvMatrix matrix = gfcYuvMatrix::Unspecified; gfcYuvRange range = gfcYuvRange::Unspecified;
    gfcChromaSiting siting = gfcChromaSiting::Left;
    bool hasAudio = false; int audioSampleRate = 0, audioChannels = 0; double durationSeconds = 0;
    // The hardware decoder in use ("videotoolbox", "d3d11va", "vaapi", ...),
    // or empty for software. Set by the first readFrame(), not by open()/probe().
    std::string hardwareDecoder;
};

// One decoded plane, pointing straight into the decoder's frame (zero copy).
struct gfcMoviePlane { const uint8_t* data = nullptr; int width = 0, height = 0, strideBytes = 0; };

// A decoded picture. `owner` keeps the underlying decoder frame alive; the
// plane pointers are valid as long as any copy of the frame exists.
// chromaInterleaved: two planes, Cb/Cr pairs in plane 1 (a hardware decoder's
// NV12/P010/P416); plane[1].width counts pairs. bitDepth counts the padding
// bits of MSB-aligned formats (P010 is 16), so codes scale like 16-bit ones.
struct gfcMovieFrame { int64_t index = -1; int width = 0, height = 0, bitDepth = 8; bool planarYuv = false;
                       bool chromaInterleaved = false; int subX = 1, subY = 1;
                       int planeCount = 0; gfcMoviePlane plane[4]; std::shared_ptr<void> owner; };

class gfcMovieReader {
public:
    gfcMovieReader();
    ~gfcMovieReader();
    gfcMovieReader(const gfcMovieReader&) = delete;
    gfcMovieReader& operator=(const gfcMovieReader&) = delete;

    // Stream facts without keeping the file open. Cached by path+size+mtime
    // (16 entries); failures are not cached.
    static bool probe(const std::string& path, gfcMovieInfo& out, std::string* err);

    bool open(const std::string& path, std::string* err);
    void close();
    bool isOpen() const;
    const gfcMovieInfo& info() const;

    // Decodes frame `index` (0-based). Sequential reads decode forward;
    // anything else seeks to the preceding keyframe. Thread-safe. Each read
    // also asks the OS to read the next 128 MiB of the file in the background
    // (macOS and Linux).
    bool readFrame(int64_t index, gfcMovieFrame& out, std::string* err);

    // Sound (default off; takes effect at the next open()). When on and the
    // file has an audio stream, open() also opens its decoder and a resampler
    // to 48 kHz stereo float, and readFrame(i) decodes every audio packet it
    // demuxes into an internal FIFO, demuxing on (queueing the video packets it
    // meets for later reads) until the FIFO covers frameStartSample(i+1) or the
    // end of the file. A seek empties the FIFO; its position restarts from the
    // audio timestamps.
    void setAudioEnabled(bool on);
    // Audio-only mode (default off; takes effect at the next open()): no
    // video decoder is opened and every stream but the audio one is discarded
    // at the demuxer, sound is decoded as if setAudioEnabled(true), and
    // readFrame() returns false. info() keeps the video stream's rate and
    // frame count; the pixel-format facts stay at their defaults.
    void setAudioOnly(bool on);
    // Demuxes forward, decoding the audio into the FIFO until it covers
    // `sampleEnd` (a movie-time 48 kHz index) or the file ends. Video packets
    // are dropped, never queued. False only on an error (no audio decoder, a
    // read error); the end of the file is not one.
    bool readAudioUntil(int64_t sampleEnd, std::string* err);
    // Seeks the audio stream to the packet at or before frameStartSample of
    // `frameIndex` (less a short decoder pre-roll, so the first samples a
    // caller keeps are not the decoder's warm-up) and empties the FIFO.
    bool seekAudio(int64_t frameIndex, std::string* err);
    // Drains the FIFO into `interleaved` (stereo float). `firstSample` is the
    // first sample's index in the movie's own 48 kHz timeline (sample 0 = the
    // first video frame). False when the FIFO is empty.
    bool takeAudio(std::vector<float>& interleaved, int64_t& firstSample);
    // Stereo frames decoded but not yet taken (0 with sound off).
    int64_t queuedAudioFrames() const;
    // Self-test hook: while set, opening an audio decoder fails after its
    // context is allocated (the "sound cannot be decoded" path).
    static void testFailAudioDecoderOpen(bool fail);
    // Self-test hook: video packets sent to the decoder since open().
    int64_t videoPacketsSent() const;
    // Self-test hook: the file offset the OS has been asked to read ahead to
    // (0 before the first read, and always 0 on Windows).
    int64_t readAheadThrough() const;

    // YUV->BGRA with the file's matrix/range (untagged = Rec.709 video range).
    // bitsPerComponent 8 -> BGRA, 16 -> BGRA64LE; out is outW*outH pixels.
    bool convertToBGRA(const gfcMovieFrame& in, int bitsPerComponent, int outW, int outH,
                       std::vector<uint8_t>& out, std::string* err);

    // Hardware decoding (default on): when allowed, the first readFrame()
    // after open() moves decoding to the platform's hardware decoder
    // (VideoToolbox, D3D11VA, VAAPI, NVDEC) if it can decode this stream and
    // the file has no alpha. Any failure, then or later, goes back to
    // software for the rest of that open file. Takes effect at the next open().
    static void setHardwareDecodingAllowed(bool allowed);
    static bool hardwareDecodingAllowed();

    // e.g. "libavcodec 62.11" - proves libav is linked.
    static std::string libavVersionString();

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

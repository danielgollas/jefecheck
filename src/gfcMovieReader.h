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
};

// One decoded plane, pointing straight into the decoder's frame (zero copy).
struct gfcMoviePlane { const uint8_t* data = nullptr; int width = 0, height = 0, strideBytes = 0; };

// A decoded picture. `owner` keeps the underlying decoder frame alive; the
// plane pointers are valid as long as any copy of the frame exists.
struct gfcMovieFrame { int64_t index = -1; int width = 0, height = 0, bitDepth = 8; bool planarYuv = false;
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
    // anything else seeks to the preceding keyframe. Thread-safe.
    bool readFrame(int64_t index, gfcMovieFrame& out, std::string* err);

    // Sound (default off; takes effect at the next open()). When on and the
    // file has an audio stream, open() also opens its decoder and a resampler
    // to 48 kHz stereo float, and readFrame(i) decodes every audio packet it
    // demuxes into an internal FIFO, demuxing on (queueing the video packets it
    // meets for later reads) until the FIFO covers frameStartSample(i+1) or the
    // end of the file. A seek empties the FIFO; its position restarts from the
    // audio timestamps.
    void setAudioEnabled(bool on);
    // Drains the FIFO into `interleaved` (stereo float). `firstSample` is the
    // first sample's index in the movie's own 48 kHz timeline (sample 0 = the
    // first video frame). False when the FIFO is empty.
    bool takeAudio(std::vector<float>& interleaved, int64_t& firstSample);
    // Self-test hook: while set, opening an audio decoder fails after its
    // context is allocated (the "sound cannot be decoded" path).
    static void testFailAudioDecoderOpen(bool fail);

    // YUV->BGRA with the file's matrix/range (untagged = Rec.709 video range).
    // bitsPerComponent 8 -> BGRA, 16 -> BGRA64LE; out is outW*outH pixels.
    bool convertToBGRA(const gfcMovieFrame& in, int bitsPerComponent, int outW, int outH,
                       std::vector<uint8_t>& out, std::string* err);

    // e.g. "libavcodec 62.11" - proves libav is linked.
    static std::string libavVersionString();

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

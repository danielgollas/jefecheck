#ifndef GFCIMAGELOADERMOVIE_H
#define GFCIMAGELOADERMOVIE_H

// Loads one frame of a .mov/.mp4 through gfcMovieReader. No libav headers
// here or in the .cpp (developer_notes §1): libav stays in gfcMovieReader.cpp.
#include <cstdint>
#include <memory>
#include <vector>

#include "gfcimageloader.h"
#include "gfcMovieReader.h"

class gfcImageLoaderMovie : public gfcImageLoader {
public:
    gfcImageLoaderMovie();
    ~gfcImageLoaderMovie() override;

    // Decodes frame params.movieFrameIndex (or fromFrame-1 when that is < 0)
    // from params.movieReader (or a reader of its own when null). Planar YUV
    // is handed to upload as planes (frameInfo.planarYuv; converted to RGB on
    // the GPU at params.scale, cropped to params.aoi when params.crop); any
    // other layout is converted to BGRA here.
    int load(gfcLoadParams params) override;
    int peek(gfcLoadParams params, gfcPeekInfo* results) override;
    void* getPixelPointer() override;
    void releaseMemory() override;
    std::vector<std::string> getChannelNames() override;

    // The decoded picture (index, size, bit depth). For planar frames the
    // planes stay valid until releaseMemory(); on the BGRA path they are
    // released once converted, so the YUV frame and a BGRA copy are never
    // both held.
    const gfcMovieFrame& movieFrame() const { return frame_; }

private:
    gfcMovieFrame frame_;
    bool planar_ = false;
    std::vector<uint8_t> pixels_;
};

#endif

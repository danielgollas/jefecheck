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
    // from params.movieReader (or a reader of its own when null) and converts
    // it to BGRA at params.scale, cropped to params.aoi when params.crop.
    int load(gfcLoadParams params) override;
    int peek(gfcLoadParams params, gfcPeekInfo* results) override;
    void* getPixelPointer() override;
    void releaseMemory() override;
    std::vector<std::string> getChannelNames() override;

    // The decoded picture's description (index, size, bit depth). The plane
    // pointers are released once the BGRA conversion is done, so the YUV
    // frame and its BGRA copy are never both held.
    const gfcMovieFrame& movieFrame() const { return frame_; }

private:
    gfcMovieFrame frame_;
    std::vector<uint8_t> pixels_;
};

#endif

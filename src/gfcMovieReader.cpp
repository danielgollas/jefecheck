#include "gfcMovieReader.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

std::string gfcMovieReader::libavVersionString() {
    return "libavcodec " + std::to_string(LIBAVCODEC_VERSION_MAJOR) + "." +
           std::to_string(LIBAVCODEC_VERSION_MINOR);
}

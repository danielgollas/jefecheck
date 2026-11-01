#pragma once
// Movie decoding via libav*. libav headers are confined to gfcMovieReader.cpp
// (developer_notes §1), so this header stays include-safe everywhere.
#include <string>

class gfcMovieReader {
public:
    // e.g. "libavcodec 62.11" - proves libav is linked.
    static std::string libavVersionString();
};

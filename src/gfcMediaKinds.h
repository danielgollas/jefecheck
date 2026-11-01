#pragma once
// Classifies a path by its container extension. Movies (.mov/.mp4) are one
// entry in a track's sequence list and are never frame-numbered.
#include <algorithm>
#include <cctype>
#include <string>

inline bool gfcIsMovieFile(const std::string& path) {
    const size_t dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    const size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos && slash > dot) return false;
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return ext == "mov" || ext == "mp4";
}

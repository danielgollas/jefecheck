#include "gfcMovieSelfTests.h"

#include <cstdio>

#include "gfcMediaKinds.h"
#include "gfcMovieReader.h"

int movieKindsSelfTest(const std::string&) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    check(gfcIsMovieFile("/x/shot.mov") && gfcIsMovieFile("/x/SHOT.MP4") && gfcIsMovieFile("a.Mov"), "mov/mp4 any case");
    check(!gfcIsMovieFile("/x/shot.0001.exr") && !gfcIsMovieFile("/x/clip.mxf") && !gfcIsMovieFile("movie"), "others rejected");
    std::printf("MOVIE-KINDS: pass=%d fail=%d\n", pass, fail);
    return fail;
}

int movieLibavSelfTest(const std::string&) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    check(gfcMovieReader::libavVersionString().rfind("libavcodec ", 0) == 0, "libav linked and reports a version");
    std::printf("MOVIE-LIBAV: pass=%d fail=%d\n", pass, fail);
    return fail;
}

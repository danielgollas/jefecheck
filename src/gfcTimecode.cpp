#include "gfcTimecode.h"

#include <cctype>
#include <cstdio>

#include "gfcMovieReader.h"

namespace {
int dropPerMinute(int nominal) { return 2 * (nominal / 30); }
}

bool gfcParseTimecode(const std::string& s, gfcTimecode& out) {
    if (s.size() != 11) return false;
    for (size_t k = 0; k < s.size(); ++k) {
        const bool sep = (k == 2 || k == 5 || k == 8);
        if (sep) {
            if (s[k] != ':' && !(k == 8 && s[k] == ';')) return false;
        } else if (k == 8) {
            continue;
        } else if (!std::isdigit(static_cast<unsigned char>(s[k]))) {
            return false;
        }
    }
    auto two = [&](size_t p) { return (s[p] - '0') * 10 + (s[p + 1] - '0'); };
    out.hh = two(0); out.mm = two(3); out.ss = two(6); out.ff = two(9);
    out.drop = (s[8] == ';');
    return true;
}

int gfcNominalFps(int num, int den) {
    return den > 0 ? (num + den - 1) / den : 0;
}

int64_t gfcTimecodeToFrames(const gfcTimecode& tc, int num, int den) {
    const int n = gfcNominalFps(num, den);
    int64_t frames = (int64_t(tc.hh) * 3600 + tc.mm * 60 + tc.ss) * n + tc.ff;
    if (tc.drop) {
        const int64_t totalMin = int64_t(tc.hh) * 60 + tc.mm;
        frames -= int64_t(dropPerMinute(n)) * (totalMin - totalMin / 10);
    }
    return frames;
}

gfcTimecode gfcFramesToTimecode(int64_t frames, int num, int den, bool drop) {
    const int n = gfcNominalFps(num, den);
    gfcTimecode tc; tc.drop = drop;
    if (n <= 0 || frames < 0) return tc;
    if (drop) {
        const int64_t dr = dropPerMinute(n);
        const int64_t perMin = int64_t(n) * 60 - dr;
        const int64_t per10 = int64_t(n) * 600 - dr * 9;
        const int64_t d = frames / per10, m = frames % per10;
        frames += dr * 9 * d;
        if (m > dr) frames += dr * ((m - dr) / perMin);
    }
    tc.ff = int(frames % n);
    int64_t secs = frames / n;
    tc.ss = int(secs % 60); secs /= 60;
    tc.mm = int(secs % 60); secs /= 60;
    tc.hh = int(secs % 24);
    return tc;
}

std::string gfcFormatTimecode(const gfcTimecode& tc) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d%c%02d", tc.hh, tc.mm, tc.ss, tc.drop ? ';' : ':', tc.ff);
    return buf;
}

int timecodeSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    gfcTimecode tc; check(gfcParseTimecode("01:00:00;00", tc) && tc.drop && tc.hh==1, "parse drop");
    check(gfcParseTimecode("00:59:59:28", tc) && !tc.drop && tc.ss==59 && tc.ff==28, "parse non-drop");
    check(!gfcParseTimecode("1:2", tc), "garbage rejected");
    check(gfcNominalFps(24000,1001)==24 && gfcNominalFps(30000,1001)==30 && gfcNominalFps(25,1)==25 && gfcNominalFps(60000,1001)==60, "nominal fps");
    gfcTimecode s; gfcParseTimecode("01:00:00:00", s);
    check(gfcFormatTimecode(gfcFramesToTimecode(gfcTimecodeToFrames(s,24000,1001)+24, 24000,1001,false))=="01:00:01:00", "ndf 23.976 +24 frames");
    check(gfcFormatTimecode(gfcFramesToTimecode(gfcTimecodeToFrames(s,24000,1001)+86400, 24000,1001,false))=="02:00:00:00", "ndf +86400");
    gfcTimecode d; gfcParseTimecode("01:00:00;00", d); const int64_t d0=gfcTimecodeToFrames(d,30000,1001);
    check(gfcFormatTimecode(gfcFramesToTimecode(d0+1799,30000,1001,true))=="01:00:59;29", "df +1799");
    check(gfcFormatTimecode(gfcFramesToTimecode(d0+1800,30000,1001,true))=="01:01:00;02", "df +1800 skips 2");
    check(gfcFormatTimecode(gfcFramesToTimecode(d0+17982,30000,1001,true))=="01:10:00;00", "df tenth minute not dropped");
    check(gfcTimecodeToFrames(gfcFramesToTimecode(123456,60000,1001,true),60000,1001)==123456, "df 59.94 round trip");
    gfcMovieInfo i; std::string err;
    check(gfcMovieReader::probe(dir+"/prores422hq_23976.mov", i, &err) && i.startTimecode=="01:00:00:00", "tmcd start timecode read");
    check(gfcMovieReader::probe(dir+"/df_2997.mov", i, &err) && i.startTimecode=="00:59:59;28", "drop-frame tmcd read");
    check(gfcMovieReader::probe(dir+"/h264_25.mp4", i, &err) && i.startTimecode.empty(), "no tmcd => empty");
    std::printf("MOVIE-TIMECODE: pass=%d fail=%d\n", pass, fail);
    return fail;
}

#include "gfcMovieSelfTests.h"

#include <cstdint>
#include <cstdio>
#include <vector>

#include "gfcMediaKinds.h"
#include "gfcMovieReader.h"
#include "gfcframe.h"
#include "gfcimageloadermovie.h"
#include "gfcStructures.h"
#include "gfcNoteStore.h"
#include "gfcMediaFingerprint.h"
#include "qt/SequenceLoadBridge_qt.h"
#include <filesystem>
#include <cmath>
#include <fstream>
#include <sstream>
#include "gfcplaybackmanager.h"
#include "gfctrackmanager.h"
#include "gfcSequence.h"
#include "gfcsequencegui.h"

extern gfcPlaybackManager playbackManager;
extern gfcTrackManager trackManager;

extern gfcSettings sett;
void findSequence(std::vector<std::string>& refFiles, std::string inputFilename, std::string& label, int& startNum, int& endNum);

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

int movieReaderSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    // Copies the visible rows of planes 0..2 into one vector (strides vary).
    auto bytesOf = [](const gfcMovieFrame& fr) {
        std::vector<uint8_t> v;
        const int bpp = fr.bitDepth > 8 ? 2 : 1;
        for (int p = 0; p < fr.planeCount && p < 3; ++p) {
            const gfcMoviePlane& pl = fr.plane[p];
            if (!pl.data) continue;
            for (int y = 0; y < pl.height; ++y) {
                const uint8_t* row = pl.data + size_t(y) * size_t(pl.strideBytes);
                v.insert(v.end(), row, row + size_t(pl.width) * bpp);
            }
        }
        return v;
    };

    gfcMovieInfo i; std::string err;
    check(gfcMovieReader::probe(dir+"/prores422hq_23976.mov", i, &err), "probe prores");
    check(i.width==320 && i.height==180 && i.frameCount==48 && i.rateNum==24000 && i.rateDen==1001, "size/count/rate 24000/1001");
    check(i.bitDepth==10 && i.subX==2 && i.subY==1 && i.planarYuv && i.pixFmt=="yuv422p10le", "422 10-bit planar");
    check(i.matrix==gfcYuvMatrix::BT709 && i.range==gfcYuvRange::Video && !i.variableRate && !i.hasAudio, "tags + CFR");
    gfcMovieInfo p4; check(gfcMovieReader::probe(dir+"/prores4444_24.mov", p4, &err) && p4.bitDepth==12 && p4.subX==1 && p4.subY==1 && p4.rateNum==24 && p4.rateDen==1, "4444 12-bit 444 24/1");
    gfcMovieInfo h; check(gfcMovieReader::probe(dir+"/h264_25.mp4", h, &err) && h.bitDepth==8 && h.subX==2 && h.subY==2 && h.rateNum==25 && h.rateDen==1 && h.siting==gfcChromaSiting::Left, "h264 420 25/1 left-sited");
    gfcMovieInfo u; check(gfcMovieReader::probe(dir+"/bars_untagged.mp4", u, &err) && u.matrix==gfcYuvMatrix::Unspecified, "untagged reports Unspecified");
    gfcMovieInfo f; check(gfcMovieReader::probe(dir+"/bars_601full.mp4", f, &err) && f.matrix==gfcYuvMatrix::BT601 && f.range==gfcYuvRange::Full, "601 full-range tags");
    gfcMovieReader r; check(r.open(dir+"/h264_25.mp4", &err), "open h264");
    gfcMovieFrame a, b; std::vector<uint8_t> seq40, jump40;
    bool seqOk=true; for (int k=0;k<=40;++k) seqOk = seqOk && r.readFrame(k, a, &err); check(seqOk, "sequential read 0..40"); seq40=bytesOf(a);
    check(r.readFrame(5, b, &err) && r.readFrame(40, b, &err), "jump 5 then 40"); jump40=bytesOf(b);
    check(!seq40.empty() && seq40==jump40, "random access is bit-exact");
    check(r.readFrame(39, b, &err) && b.index==39, "backward step");
    check(r.readFrame(47, b, &err) && b.index==47 && !r.readFrame(48, b, &err), "last frame decodes (drained), 48 is out of range");
    check(r.readFrame(0, b, &err) && b.plane[0].width==320 && b.plane[1].width==160 && b.plane[1].height==90, "plane geometry 420");
    std::vector<uint8_t> bgra; check(r.convertToBGRA(b, 8, 320, 180, bgra, &err) && bgra.size()==320u*180*4, "bgra 8-bit");
    check(r.convertToBGRA(b, 16, 160, 90, bgra, &err) && bgra.size()==160u*90*8, "bgra 16-bit scaled");
    err.clear();
    check(!gfcMovieReader::probe(dir+"/empty.mov", i, &err) && !err.empty(), "empty file errors");
    gfcMovieReader t; check(t.open(dir+"/truncated.mov", &err), "truncated opens"); int got=0; gfcMovieFrame tf; for (int k=0;k<48;++k){ if(!t.readFrame(k,tf,&err)) break; ++got; } check(got>0 && got<48, "truncated decodes some frames then stops cleanly");
    std::printf("MOVIE-READER: pass=%d fail=%d\n", pass, fail);
    return fail;
}

int movieNumberingSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    std::vector<std::string> files; std::string label; int s = 0, e = 0;
    findSequence(files, dir+"/shot_v003.mov", label, s, e);
    check(files.size()==48 && s==1 && e==48 && label==dir+"/shot_v003.mov" && files[0]==files[47], "shot_v003.mov is 48 frames of one file, not frame 3");
    findSequence(files, dir+"/a_001.mov", label, s, e);
    check(files.size()==48 && files[0]==dir+"/a_001.mov", "a_001.mov and a_002.mov are never joined");
    check(gfcNoteStore::normalisePath(dir+"/a.001.mov")==dir+"/a.001.mov", "normalisePath leaves a movie alone");
    auto seqs = gfcMediaFingerprint::sequencesIn(dir, false);
    check(seqs.count(dir+"/a_001.mov")==1 && seqs.at(dir+"/a_001.mov").size()==1 && seqs.count(dir+"/a_002.mov")==1, "sequencesIn lists each movie once");
    check(jefe::qt::listSequenceFrames(dir+"/shot_v003.mov").size()==1, "listSequenceFrames returns the single file");
    std::filesystem::create_directories(dir+"/moved"); std::filesystem::copy_file(dir+"/a_002.mov", dir+"/moved/a_002.mov");
    sett.useSearchPaths=true; sett.searchPaths={dir+"/moved"}; findSequence(files, dir+"/gone/a_002.mov", label, s, e); sett.useSearchPaths=false; sett.searchPaths.clear();
    check(files.size()==48 && files[0]==dir+"/moved/a_002.mov", "a moved movie is found through search paths");
    std::printf("MOVIE-NUMBERING: pass=%d fail=%d\n", pass, fail);
    return fail;
}

int movieLoaderSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    gfcLoadParams p; p.fileName=dir+"/prores422hq_23976.mov"; p.fromFrame=1; p.toFrame=48; p.scale=100; p.compressed=GFC_16HALF; p.movieFrameIndex=7;
    gfcFrame f; check(f.loadFrame(p)==0 && f.loaded && f.sizeX==320 && f.sizeY==180 && f.bpc==16 && f.originalBitDepth==10, "16-bit movie frame loads");
    check(f.whatLoaderToUse(p.fileName)==GFCLOADER_MOVIE, "dispatch");
    gfcGLFrameInfo gi=f.theImageLoader->getFrameInfo(); check(gi.dataType==GL_UNSIGNED_SHORT && gi.internalFormat==GL_RGBA16F_ARB && gi.target==GL_TEXTURE_RECTANGLE_ARB, "16F upload info");
    p.compressed=GFC_8BPC; p.scale=50; gfcFrame g; check(g.loadFrame(p)==0 && g.sizeX==160 && g.sizeY==90 && g.bpc==8, "8-bit at 50% scale");
    p.fileName=dir+"/empty.mov"; gfcFrame e; check(e.loadFrame(p)!=0 && !e.loaded && !e.loadErrorString.empty(), "empty movie reports an error");
    p.fileName=dir+"/truncated.mov"; p.movieFrameIndex=47; gfcFrame t; check(t.loadFrame(p)!=0 && !t.loaded, "frame past the truncation fails without crashing");
    p.fileName=dir+"/h264_25.mp4"; p.movieFrameIndex=-1; p.fromFrame=3; gfcFrame h; check(h.loadFrame(p)==0 && h.theImageLoader && static_cast<gfcImageLoaderMovie*>(h.theImageLoader)->movieFrame().index==2, "index defaults to fromFrame-1");
    for (gfcFrame* fr : {&f, &g, &e, &t, &h}) fr->clearFrame();
    std::printf("MOVIE-LOADER: pass=%d fail=%d\n", pass, fail);
    return fail;
}

int movieRateSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    auto readFile = [](const std::string& path) { std::ifstream in(path); std::stringstream ss; ss << in.rdbuf(); return ss.str(); };
    const int savedQueue = sett.maximumFramesInQueue; sett.maximumFramesInQueue = 100000;
    int n,d; gfcPlaybackManager::rationalFromFPS(23.976f,n,d); check(n==24000&&d==1001,"23.976 -> 24000/1001");
    gfcPlaybackManager::rationalFromFPS(29.97f,n,d); check(n==30000&&d==1001,"29.97"); gfcPlaybackManager::rationalFromFPS(23.98f,n,d); check(n==24000&&d==1001,"a typed 23.98 is NTSC 24000/1001"); gfcPlaybackManager::rationalFromFPS(25.0f,n,d); check(n==25&&d==1,"25");
    playbackManager.setTargetRate(24000,1001); playbackManager.getTargetRate(n,d); check(n==24000&&d==1001 && std::fabs(playbackManager.getTargetFPS()-23.976f)<0.001f, "rate held exactly");
    gfcSequence* seq=trackManager.getSequence(0); seq->myGUI->setFilename(dir+"/prores422hq_23976.mov"); seq->myGUI->setCompression(GFC_8BPC);
    playbackManager.setTargetFPS(30.0f); trackManager.startLoadingSequence(0); trackManager.stopLoadingSequence(0);
    playbackManager.getTargetRate(n,d); check(n==24000&&d==1001, "loading a movie sets the player's rate to its rational");
    playbackManager.setTargetFPS(30.0f); trackManager.startLoadingSequenceAt(0, 10); trackManager.stopLoadingSequence(0); playbackManager.getTargetRate(n,d); check(n==30&&d==1, "an alt-click reload keeps the user's rate");
    playbackManager.setTargetRate(24000,1001); playbackManager.setCurrentFrame(25);
    check(jefe::qt::activeTrackSourceTimecode()=="01:00:01:00", "source TC for frame 25 = start + 24 frames at the exact rate");
    seq->setOffset(10); check(jefe::qt::activeTrackSourceTimecode()=="01:00:00:14", "offset shifts the mapping"); seq->setOffset(0);
    check(!jefe::qt::activeTrackIsVariableRate(), "CFR clip");
    const std::string sess=dir+"/rate.jcs"; jefe::qt::saveSession(sess); playbackManager.setTargetFPS(24.0f); jefe::qt::loadSession(sess);
    playbackManager.getTargetRate(n,d); check(n==24000&&d==1001, "session round-trips the rational");
    check(readFile(sess).find("targetFPSNum=\"24000\"")!=std::string::npos, "session stores num/den");
    sett.maximumFramesInQueue = savedQueue;
    std::printf("MOVIE-RATE: pass=%d fail=%d\n", pass, fail);
    return fail;
}

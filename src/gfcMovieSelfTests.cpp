#include "gfcMovieSelfTests.h"

#include <cstdint>
#include <cstdio>
#include <vector>

#include "gfcMediaKinds.h"
#include "gfcMovieReader.h"
#include "gfcAudioClip.h"
#include "gfcAudioEngine.h"
#include "UIConstants.h"
#include "gfcframe.h"
#include "gfcimageloadermovie.h"
#include "gfcStructures.h"
#include "gfcNoteStore.h"
#include "gfcMediaFingerprint.h"
#include "qt/SequenceLoadBridge_qt.h"
#include <filesystem>
#include <chrono>
#include <thread>
#include <cmath>
#include <fstream>
#include <sstream>
#include "gfcplaybackmanager.h"
#include "gfctrackmanager.h"
#include "gfcSequence.h"
#include "gfcsequencegui.h"
#include "gfcNoteStamp.h"
#include "gfcreview.h"
#include "qt/ReviewPackage_qt.h"

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
    // A session restore keeps the session's saved rate even though it contains a movie at another rate.
    playbackManager.setTargetRate(30,1); jefe::qt::saveSession(sess); playbackManager.setTargetFPS(24.0f);
    jefe::qt::loadSession(sess); jefe::qt::startLoadingAllTracks(); trackManager.stopLoadingSequence(0);
    playbackManager.getTargetRate(n,d); check(n==30&&d==1, "session restore keeps the saved rate, not the movie's");
    // a fresh load afterwards still takes the movie's rate
    jefe::qt::startLoadingAllTracks(); trackManager.stopLoadingSequence(0); playbackManager.getTargetRate(n,d); check(n==24000&&d==1001, "a fresh load after a restore sets the movie's rate");
    playbackManager.setTargetFPS(24.0f); seq->setOffset(0);
    sett.maximumFramesInQueue = savedQueue;
    std::printf("MOVIE-RATE: pass=%d fail=%d\n", pass, fail);
    return fail;
}

int movieFingerprintSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    auto readFile = [](const std::string& path) { std::ifstream in(path); std::stringstream ss; ss << in.rdbuf(); return ss.str(); };
    namespace fs = std::filesystem;
    const int savedQueue = sett.maximumFramesInQueue; sett.maximumFramesInQueue = 100000;
    std::string err; gfcMediaFingerprint::Probe p;
    check(gfcMediaFingerprint::probe(dir+"/h264_25.mp4", p, &err) && p.width==320 && p.height==180 && p.channels==std::vector<std::string>{"R","G","B"}, "movie probe");
    const std::string fa=gfcMediaFingerprint::compute({dir+"/a_001.mov"}, &err), fb=gfcMediaFingerprint::compute({dir+"/a_002.mov"}, &err), fh=gfcMediaFingerprint::compute({dir+"/h264_25.mp4"}, &err);
    check(fa.rfind("fp1:",0)==0 && fa.size()==44 && fa==fb && fa!=fh, "same bytes same fp1; different movie differs");
    err.clear();
    check(gfcMediaFingerprint::compute({dir+"/empty.mov"}, &err).empty() && !err.empty(), "empty movie errors");

    { const std::uint64_t G=1ull<<30, size=5*G; std::uint64_t w[3][2]; gfcMediaFingerprint::movieHashWindows(size, w);
      check(w[0][0]==0 && w[0][1]==65536 && w[1][0]==size/2-32768 && w[1][1]==size/2+32768 && w[2][0]==size-65536 && w[2][1]==size && w[1][0]>(1ull<<31) && w[2][0]>(1ull<<31), "hash windows of a 5 GiB file are exact and above 2^31");
      gfcMediaFingerprint::movieHashWindows(100, w); check(w[0][1]==100 && w[1][0]==0 && w[1][1]==100 && w[2][0]==0 && w[2][1]==100, "hash windows clamp for a tiny file"); }

    // Package round trip: shot_v003.mov on track A, exported with media, opened.
    const std::string movie=dir+"/shot_v003.mov";
    gfcSequence* seq=trackManager.getSequence(0); seq->myGUI->setFilename(movie); seq->myGUI->setCompression(GFC_8BPC);
    trackManager.startLoadingSequence(0); trackManager.stopLoadingSequence(0);
    const std::string sess=dir+"/pkg.jcs"; jefe::qt::saveSession(sess);
    gfcReview review; review.mediaPath=movie;
    jefe::qt::package::ExportInput in;
    in.outPath=dir+"/movie.jcreview"; in.includeMedia=true; in.sessionXml=readFile(sess); in.appVersion="test"; in.createdIso="2026-10-31T00:00:00Z";
    jefe::qt::package::ExportMedia em; em.mediaPath=movie; em.frames={movie}; em.notesXml=gfcNoteStore::toXmlString(review); em.fingerprint=gfcMediaFingerprint::compute(em.frames,&err); em.width=p.width; em.height=p.height;
    in.media.push_back(em);
    QString qerr; jefe::qt::package::Exporter ex; bool exported=ex.begin(in,&qerr);
    if (exported) { jefe::qt::package::Exporter::State st; while ((st=ex.step(&qerr))==jefe::qt::package::Exporter::State::Running) {} exported=(st==jefe::qt::package::Exporter::State::Done); }
    jefe::qt::package::OpenServices svc; svc.interactive=false; jefe::qt::package::OpenResult res;
    const bool opened=exported && jefe::qt::package::openPackage(in.outPath, dir+"/pkgcache", svc, res, &qerr);
    if (!opened) std::printf("  package: %s\n", qerr.toStdString().c_str());
    check(opened && res.manifest.media.size()==1 && res.manifest.media[0].frames.size()==1 && res.manifest.media[0].frames[0]=="shot_v003.mov", "package lists the movie once");
    check(opened && fs::exists(res.extractDir+"/media/000/shot_v003.mov") && readFile(res.sessionPath).find("media/000/shot_v003.mov")!=std::string::npos, "the movie file is copied and relinked");
    sett.maximumFramesInQueue = savedQueue;
    std::printf("MOVIE-FINGERPRINT: pass=%d fail=%d\n", pass, fail);
    return fail;
}

int movieStampSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    std::string err;
    std::vector<float> rgb(320*180*3, 0.25f); gfcReview review; gfcNoteStamp::Options opt;
    check(gfcNoteStamp::stampPixels(320,180,rgb.data(), dir+"/stamp.exr", review, opt, &err), "stampPixels writes");
    int w=0,h=0; check(gfcNoteStamp::imageSize(dir+"/stamp.exr",&w,&h) && w==320 && h==180, "stamped EXR has the frame size");
    std::printf("MOVIE-STAMP: pass=%d fail=%d\n", pass, fail);
    return fail;
}

int audioClipSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    check(gfcAudioClip::frameStartSample(1,24000,1001)==2002 && gfcAudioClip::frameStartSample(1000,24000,1001)==2002000, "23.976: 2002 samples per frame, exact");
    check(gfcAudioClip::frameStartSample(1,30000,1001)==1601 && gfcAudioClip::frameStartSample(2,30000,1001)==3203 && gfcAudioClip::frameStartSample(3,30000,1001)==4804, "29.97 floor math 1601/1602");
    check(gfcAudioClip::frameStartSample(1,25,1)==1920 && gfcAudioClip::frameStartSample(1,24,1)==2000, "25 and 24");
    gfcMovieReader r; std::string err; r.setAudioEnabled(true); check(r.open(dir+"/flashbeep_23976.mov", &err) && r.info().hasAudio && r.info().audioSampleRate==48000 && r.info().audioChannels==2, "audio stream opens");
    gfcAudioClip clip; clip.reset(0); gfcMovieFrame f; std::vector<float> a; int64_t first=-1;
    for (int k=0;k<48;++k){ r.readFrame(k,f,&err); while (r.takeAudio(a, first)) { if (clip.endSample()==clip.startSample()) clip.reset(first); clip.append(a.data(), (int64_t)a.size()/2); } }
    check(clip.startSample()==0 && clip.endSample()>=gfcAudioClip::frameStartSample(48,24000,1001), "audio covers the decoded frames");
    auto rms=[&](int64_t frame){ std::vector<float> s(2002*2); clip.read(gfcAudioClip::frameStartSample(frame,24000,1001), 2002, s.data()); double e=0; for(float v:s) e+=v*v; return std::sqrt(e/s.size()); };
    check(rms(0)>0.3 && rms(24)>0.3 && rms(1)<0.01 && rms(23)<0.01 && rms(25)<0.01, "beep lands in frames 0 and 24 only");
    gfcMovieReader m; m.setAudioEnabled(true); check(m.open(dir+"/mono441_24.mp4",&err) && m.info().audioChannels==1 && m.info().audioSampleRate==44100, "mono 44.1k source");
    gfcAudioClip mc; mc.reset(0); for (int k=0;k<48;++k){ m.readFrame(k,f,&err); while (m.takeAudio(a,first)) mc.append(a.data(), (int64_t)a.size()/2); }
    check(mc.endSample()>=48*2000-2000 && mc.endSample()<=48*2000+4096, "resampled to 48k stereo: ~2000 samples per frame");
    std::vector<float> z(100*2); check(clip.read(-50, 100, z.data())==50 && z[0]==0.f, "reads before the start are zero-filled");
    gfcAudioPeaks pk; pk.reset(0); std::vector<float> sig(4800*2, 0.f); sig[2*480]=0.9f; pk.accumulate(sig.data(), 4800);
    check(pk.peaks.size()==10 && pk.peaks[1]>0.89f && pk.peaks[0]==0.f && pk.version==1, "10 ms bins, abs peak");
    std::printf("AUDIO-CLIP: pass=%d fail=%d\n", pass, fail);
    return fail;
}

// The loader thread fills a track's clip and peaks as a movie loads.
int audioSequenceSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    auto waitUntil = [](auto pred, int ms) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (!pred() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return pred();
    };
    auto rmsAt = [](const gfcAudioClip& c, int64_t frame) {
        std::vector<float> s(2002*2); c.read(gfcAudioClip::frameStartSample(frame,24000,1001), 2002, s.data());
        double e=0; for (float v : s) e+=v*v; return std::sqrt(e/s.size());
    };
    const int savedQueue = sett.maximumFramesInQueue; sett.maximumFramesInQueue = 100000;
    gfcSequence* seq=trackManager.getSequence(0); seq->myGUI->setCompression(GFC_8BPC);

    seq->myGUI->setFilename(dir+"/flashbeep_23976.mov"); seq->loadPreview(); trackManager.startLoadingSequence(0);
    const int64_t want=gfcAudioClip::frameStartSample(200,24000,1001);
    const bool filled=waitUntil([&]{ auto c=seq->audioClip(); return c && c->endSample()>=want; }, 20000);
    trackManager.stopLoadingSequence(0);
    std::shared_ptr<gfcAudioClip> clip=seq->audioClip();
    check(seq->hasAudio() && clip && filled, "the loader fills the track's clip");
    check(clip && clip->startSample()==0 && rmsAt(*clip,0)>0.3 && rmsAt(*clip,24)>0.3 && rmsAt(*clip,12)<0.01 && rmsAt(*clip,25)<0.01, "beeps at frames 0 and 24 in the track clip");
    const gfcAudioPeaks pk=seq->audioPeaksSnapshot();
    float beep=0; for (size_t b=100; b<104 && b<pk.peaks.size(); ++b) beep=std::max(beep, pk.peaks[b]);
    check(pk.firstSample==0 && pk.peaks.size()>=size_t(want/480) && beep>0.7f && pk.peaks[50]<0.01f && seq->audioPeaksVersion()==pk.version && pk.version>0, "peaks follow the clip in 10 ms bins");

    const uint64_t v0=seq->audioPeaksVersion();
    trackManager.startLoadingSequenceAt(0, 10);
    const int64_t want10=gfcAudioClip::frameStartSample(30,24000,1001);
    const bool filled10=waitUntil([&]{ auto c=seq->audioClip(); return c && c!=clip && c->endSample()>=want10; }, 20000);
    trackManager.stopLoadingSequence(0);
    std::shared_ptr<gfcAudioClip> clip10=seq->audioClip();
    check(filled10 && clip10->startSample()==gfcAudioClip::frameStartSample(10,24000,1001) && rmsAt(*clip10,24)>0.3 && rmsAt(*clip10,12)<0.01 && rmsAt(*clip10,9)==0.0, "a load from frame 10 starts its clip at frame 10's sample, aligned");
    check(clip && rmsAt(*clip,0)>0.3 && seq->audioPeaksVersion()>v0 && seq->audioPeaksSnapshot().firstSample==clip10->startSample(), "a reload makes a new clip; the old one stays intact");

    // Sound that cannot be decoded (decoder context allocated, open fails):
    // the movie still opens and plays, and reports no audio.
    gfcMovieReader::testFailAudioDecoderOpen(true);
    { gfcMovieReader r; std::string err; gfcMovieFrame f; r.setAudioEnabled(true);
      std::vector<float> a; int64_t first=0;
      const bool ok=r.open(dir+"/flashbeep_23976.mov", &err) && r.readFrame(0,f,&err) && r.readFrame(30,f,&err) && f.index==30;
      check(ok && !r.info().hasAudio && !r.takeAudio(a, first), "undecodable audio: the reader opens, decodes video, reports no audio"); }
    seq->myGUI->setFilename(dir+"/flashbeep_23976.mov"); seq->loadPreview();
    // The clip is decided synchronously in initializeSequence, before the loader thread runs.
    trackManager.startLoadingSequence(0);
    const bool movieNoClip=seq->isMovie() && !seq->hasAudio() && !seq->audioClip();
    trackManager.stopLoadingSequence(0);
    gfcMovieReader::testFailAudioDecoderOpen(false);
    check(movieNoClip, "undecodable audio: the track loads as a movie with no clip");

    seq->myGUI->setFilename(dir+"/prores422hq_23976.mov"); seq->loadPreview(); trackManager.startLoadingSequence(0); trackManager.stopLoadingSequence(0);
    check(!seq->hasAudio() && !seq->audioClip(), "a silent movie has no clip");
    sett.maximumFramesInQueue = savedQueue;
    std::printf("AUDIO-SEQUENCE: pass=%d fail=%d\n", pass, fail);
    return fail;
}

// Decodes a whole movie with the reader: its 48 kHz sound into `clip` and each
// frame's mean luma (BGRA CPU path) into `luma`. Returns the frame count.
static int64_t decodeMovieAudioAndLuma(const std::string& path, gfcAudioClip& clip, std::vector<double>* luma,
                                       int& rateNum, int& rateDen) {
    gfcMovieReader r; std::string err; r.setAudioEnabled(true);
    if (!r.open(path, &err)) return 0;
    const int64_t n = r.info().frameCount; rateNum = r.info().rateNum; rateDen = r.info().rateDen;
    clip.reset(0); bool first = true; gfcMovieFrame f; std::vector<float> a; int64_t firstSample = 0; std::vector<uint8_t> bgra;
    for (int64_t k = 0; k < n; ++k) {
        if (!r.readFrame(k, f, &err)) return k;
        if (luma) {
            double sum = 0;
            if (r.convertToBGRA(f, 8, f.width, f.height, bgra, &err))
                for (size_t p = 0; p + 3 < bgra.size(); p += 4) sum += 0.0722*bgra[p] + 0.7152*bgra[p+1] + 0.2126*bgra[p+2];
            luma->push_back(bgra.empty() ? 0.0 : sum / (255.0 * double(bgra.size() / 4)));
        }
        while (r.takeAudio(a, firstSample)) { if (first) { clip.reset(firstSample); first = false; } clip.append(a.data(), (int64_t)a.size()/2); }
    }
    return n;
}

// The extracted frame-advance rule keeps update()'s behaviour.
int playbackAdvanceSelfTest(const std::string&) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    int d = 1;
    check(gfcPlaybackManager::advanceFrame(5, d, LOOPMODEONCE_ID, 1, 10) == 6 && d == 1, "once: forward step");
    d = 1; check(gfcPlaybackManager::advanceFrame(10, d, LOOPMODEONCE_ID, 1, 10) == 10 && d == 1, "once: holds at the end");
    d = -1; check(gfcPlaybackManager::advanceFrame(1, d, LOOPMODEONCE_ID, 1, 10) == 1 && d == -1, "once: holds at the start in reverse");
    d = 1; check(gfcPlaybackManager::advanceFrame(10, d, LOOPMODELOOP_ID, 1, 10) == 1 && d == 1, "loop: wraps forward");
    d = -1; check(gfcPlaybackManager::advanceFrame(1, d, LOOPMODELOOP_ID, 1, 10) == 10 && d == -1, "loop: wraps in reverse");
    d = 1; check(gfcPlaybackManager::advanceFrame(10, d, LOOPMODEBOUNCE_ID, 1, 10) == 9 && d == -1, "bounce: turns at the end");
    d = -1; check(gfcPlaybackManager::advanceFrame(1, d, LOOPMODEBOUNCE_ID, 1, 10) == 2 && d == 1, "bounce: turns at the start");
    d = -1; check(gfcPlaybackManager::advanceFrame(5, d, LOOPMODELOOP_ID, 1, 10) == 4, "loop: reverse step");
    std::printf("PLAYBACK-ADVANCE: pass=%d fail=%d\n", pass, fail);
    return fail;
}

int audioEngineSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    auto nonZero = [&](const std::vector<float>& v) { int64_t n = 0; for (size_t i = 0; i < v.size(); i += 2) if (std::fabs(v[i]) > 1e-6f) n = int64_t(i/2) + 1; return n; };
    // Sign changes of the left channel over stereo frames [from, to); exact zeros carry the previous sign.
    auto countZeroCrossings = [](const std::vector<float>& v, int from, int to) {
        int n = 0, sign = 0;
        for (int i = from; i < to && size_t(2*i) < v.size(); ++i) {
            const float s = v[size_t(2*i)];
            const int sg = s > 0.f ? 1 : (s < 0.f ? -1 : 0);
            if (sg != 0) { if (sign != 0 && sg != sign) ++n; sign = sg; }
        }
        return n;
    };
    auto clipA = std::make_shared<gfcAudioClip>(); int rn = 0, rd = 1;
    const int64_t framesA = decodeMovieAudioAndLuma(dir+"/flashbeep_23976.mov", *clipA, nullptr, rn, rd);
    if (framesA != 240) std::printf("  note: flashbeep_23976 decoded %lld frames\n", (long long)framesA);

    gfcAudioEngine e; gfcFakeAudioOutput out; e.setOutput(&out); out.open(&e);
    e.setTrackClip(0, clipA, 24000, 1001, 240); e.setPlayerRate(24000,1001);
    // step slice: exactly one frame, faded
    std::vector<float> cap; e.playSlice(25); out.pump(4000, &cap);                      // frame 25 = movie index 24 (beep)
    check(nonZero(cap)==2002, "one frame's samples (2002) then silence");
    bool ramp=true; for (int i=0;i<144;++i) ramp &= std::fabs(cap[2*i]) <= 0.8f*(i+1)/144.0f + 1e-3f; check(ramp, "3 ms fade-in envelope");
    check(std::fabs(cap[2*2001])<0.02f, "fade-out reaches ~0 at the slice end");
    // fast scrub drops the late one: two requests before any pump -> only the last plays
    cap.clear(); e.playSlice(49); e.playSlice(73); out.pump(4000,&cap); check(nonZero(cap)==2002, "only the newest pending slice plays");
    // muted -> zeros
    e.setTrackMuted(0,true); cap.clear(); e.playSlice(25); out.pump(4000,&cap); check(nonZero(cap)==0, "muted track renders silence"); e.setTrackMuted(0,false);
    // off-rate: 2x -> cut to 1001 samples; 0.5x -> 2002 then 2002 of silence; pitch unchanged (1 kHz = 48 samples/period)
    e.setPlayerRate(48000,1001); cap.clear(); e.playSlice(25); out.pump(1001,&cap); e.playSlice(26); out.pump(3000,&cap);
    check(nonZero(cap)<=1001+144 && countZeroCrossings(cap,0,900)>=35 && countZeroCrossings(cap,0,900)<=40, "2x: cut at 1001 samples, unchanged pitch");
    e.setPlayerRate(12000,1001); cap.clear(); e.playSlice(25); out.pump(4004,&cap); check(nonZero(cap)==2002, "0.5x: full slice then silence until the next frame");
    // leading: contiguous, no fades between frames, leaderFrame follows playedSamples, loop wraps
    e.setPlayerRate(24000,1001); check(e.canLead(), "native-rate unmuted track can lead");
    gfcAudioTransport t{1,1,48,LOOPMODELOOP_ID}; e.startLeading(t); cap.clear(); out.pump(2002*10+1000,&cap);
    check(e.isLeading() && e.leaderFrame()==11, "after 10.5 frames of samples the leader is on frame 11");
    // After the first slice's fade-in the output is the clip sample for sample,
    // straight across all ten frame boundaries (incl. the beep->silence cut at
    // frame 1|2, where any fade-out would show): no fades between contiguous frames.
    { const int n = 2002*10+1000; std::vector<float> ref(size_t(2*n)); clipA->read(0, n, ref.data()); float worst=0;
      for (int i=gfcAudioEngine::kFadeSamples; i<n && size_t(2*i+1)<cap.size(); ++i)
          worst=std::max({worst, std::fabs(cap[2*i]-ref[2*i]), std::fabs(cap[2*i+1]-ref[2*i+1])});
      check(cap.size()==size_t(2*n) && worst < 1e-6f, "no fade between contiguous frames"); }
    out.pump(2002*40, nullptr); check(e.leaderFrame()>=1 && e.leaderFrame()<=48, "loop wraps inside [1,48]");
    e.stopLeading(); check(!e.isLeading(), "stopped");
    // reverse never leads; off-rate track never leads
    e.setPlayerRate(25,1); check(!e.canLead(), "off-rate track cannot lead");
    // mixing: two tracks, sum and clamp
    e.setTrackClip(1, clipA, 24000, 1001, 240); e.setPlayerRate(24000,1001); cap.clear(); e.playSlice(25); out.pump(2002,&cap);
    float peak=0; for (float v: cap) peak=std::max(peak,std::fabs(v)); check(peak<=1.0f && peak>0.95f, "two 0.8 beeps sum then clamp to 1.0");
    // A clip the engine drops is destroyed after the engine mutex is released
    // (render() must never wait on freeing a long clip): clearTrack, and a
    // setTrackClip that replaces the clip with one rejected for a bad rate.
    { int freedUnlocked = 0, freed = 0;
      auto tracked = [&]{ return std::shared_ptr<gfcAudioClip>(new gfcAudioClip, [&](gfcAudioClip* c){ ++freed; freedUnlocked += e.testMutexFreeFromOtherThread() ? 1 : 0; delete c; }); };
      e.setTrackClip(2, tracked(), 24000, 1001, 240); e.clearTrack(2);
      check(freed==1 && freedUnlocked==1 && !e.trackHasAudio(2), "clearTrack frees the clip outside the engine lock");
      freed = freedUnlocked = 0;
      e.setTrackClip(2, tracked(), 24000, 1001, 240); e.setTrackClip(2, tracked(), 0, 1001, 240);
      check(freed==2 && freedUnlocked==2 && !e.trackHasAudio(2), "a replaced clip and a rejected (bad-rate) clip are both freed outside the lock"); }
    // null output: every call is safe
    gfcAudioEngine n; n.setOutput(nullptr); n.setTrackClip(0, clipA, 24000,1001,240); n.setPlayerRate(24000,1001); n.playSlice(25); n.startLeading(t);
    check(!n.canLead() && !n.isLeading(), "no device: cannot lead, every call is a safe no-op");
    std::printf("AUDIO-ENGINE: pass=%d fail=%d\n", pass, fail);
    return fail;
}

// Plays each flash/beep clip whole while the audio leads and checks every
// logged frame: its slice is loud exactly when its picture is the white flash.
int audioSyncSelfTest(const std::string& dir) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    struct Case { const char* file; int64_t expectFrames; const char* syncMsg; const char* countMsg; };
    const Case cases[] = {
        {"/flashbeep_23976.mov", 240, "23.976: every frame's sound matches its picture", "23.976: 240 frames rendered, none twice"},
        {"/flashbeep_25.mov",    250, "25: every frame's sound matches its picture",     "25: 250 frames rendered, none twice"},
    };
    for (const Case& c : cases) {
        auto clip = std::make_shared<gfcAudioClip>(); std::vector<double> luma; int rn = 0, rd = 1;
        const int64_t frames = decodeMovieAudioAndLuma(dir + c.file, *clip, &luma, rn, rd);
        gfcAudioEngine e; gfcFakeAudioOutput out; e.setOutput(&out); out.open(&e);
        e.setTrackClip(0, clip, rn, rd, frames); e.setPlayerRate(rn, rd);
        gfcAudioTransport t{1, 1, int(frames), LOOPMODEONCE_ID};
        e.startLeading(t);
        std::vector<float> cap;
        const int64_t total = gfcAudioClip::frameStartSample(frames + 3, rn, rd);
        for (int64_t done = 0; done < total; done += 512) out.pump(512, &cap);   // device-sized pulls
        const auto log = e.renderLogSnapshot();
        int mismatches = 0; bool ordered = !log.empty();
        const int64_t base = log.empty() ? 0 : log.front().startSample;
        for (size_t k = 0; k < log.size(); ++k) {
            const int frame = log[k].frame;
            ordered &= (frame == int(k) + 1);
            const int64_t from = log[k].startSample - base;
            const int64_t len = gfcAudioClip::frameStartSample(frame, rn, rd) - gfcAudioClip::frameStartSample(frame - 1, rn, rd);
            double en = 0; int64_t cnt = 0;
            for (int64_t s = from; s < from + len && size_t(2*s+1) < cap.size(); ++s) { en += cap[size_t(2*s)]*cap[size_t(2*s)] + cap[size_t(2*s+1)]*cap[size_t(2*s+1)]; cnt += 2; }
            const bool loud = cnt > 0 && std::sqrt(en / double(cnt)) > 0.3;
            const bool white = frame >= 1 && size_t(frame - 1) < luma.size() && luma[size_t(frame - 1)] > 0.5;
            if (loud != white) { ++mismatches; if (mismatches <= 3) std::printf("  %s frame %d: loud=%d white=%d\n", c.file, frame, int(loud), int(white)); }
        }
        check(mismatches == 0 && !log.empty(), c.syncMsg);
        check(int64_t(log.size()) == c.expectFrames && ordered, c.countMsg);
    }
    std::printf("AUDIO-SYNC: pass=%d fail=%d\n", pass, fail);
    return fail;
}

// Leading walk details on a synthetic constant-level clip: BOUNCE return
// passes are silent and resume with a fade-in, ONCE holds without repeating,
// offsets shift sound with picture, and 29.97 (1601/1602-sample frames) streams
// without gaps.
int audioWalkSelfTest(const std::string&) {
    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char* msg) {
        if (cond) ++pass; else { ++fail; std::printf("  FAIL: %s\n", msg); }
    };
    auto dcClip = [](int64_t frames) {
        auto c = std::make_shared<gfcAudioClip>(); c->reset(0);
        std::vector<float> s(size_t(frames) * 2, 0.5f); c->append(s.data(), frames); return c;
    };
    auto level = [](const std::vector<float>& v, int64_t from, int64_t len) {
        double e = 0; int64_t n = 0;
        for (int64_t s = from; s < from + len && size_t(2*s+1) < v.size(); ++s) { e += std::fabs(v[size_t(2*s)]); ++n; }
        return n ? e / double(n) : 0.0;
    };
    {   // 24/1: 2000 samples per frame, ten frames of 0.5
        auto clip = dcClip(20000);
        gfcAudioEngine e; gfcFakeAudioOutput out; e.setOutput(&out); out.open(&e);
        e.setTrackClip(0, clip, 24, 1, 10); e.setPlayerRate(24, 1);
        gfcAudioTransport b{1, 1, 5, LOOPMODEBOUNCE_ID};
        e.startLeading(b); std::vector<float> cap; out.pump(2000 * 12, &cap);
        const auto log = e.renderLogSnapshot();
        const int want[] = {1,2,3,4,5,4,3,2,1,2,3,4};
        bool order = log.size() >= 12; for (size_t k = 0; order && k < 12; ++k) order = log[k].frame == want[k];
        check(order, "bounce: the walk goes 1..5..1..4");
        bool ok = true;
        for (int k = 1; k < 4; ++k) ok &= level(cap, 2000*k, 2000) > 0.49;          // forward frames 2..4 unfaded
        ok &= cap[2*(2000*5 - 1)] < 0.01f;                                            // frame 5 fades out before the turn
        for (int k = 5; k < 9; ++k) ok &= level(cap, 2000*k, 2000) == 0.0;           // return pass silent
        ok &= std::fabs(cap[2*(2000*9)]) < 0.01f && level(cap, 2000*9 + 144, 1856) > 0.49; // resume with a fade-in
        check(ok, "bounce: forward passes sound, the return pass is silent, resume fades in");

        gfcAudioTransport once{8, 1, 10, LOOPMODEONCE_ID};
        e.startLeading(once); cap.clear(); out.pump(2000 * 6, &cap);
        const auto log2 = e.renderLogSnapshot();
        check(log2.size() == 3 && log2.back().frame == 10 && e.leaderFrame() == 10 && e.isLeading() &&
              level(cap, 2000*3, 6000) == 0.0, "once: holds on the last frame without repeating its sound");
        e.stopLeading();

        e.setTrackOffset(0, 2);
        gfcAudioTransport o{1, 1, 10, LOOPMODELOOP_ID};
        e.startLeading(o); cap.clear(); out.pump(2000 * 4, &cap); e.stopLeading();
        check(level(cap, 0, 4000) == 0.0 && level(cap, 4000 + 144, 2000 - 144) > 0.49, "offset 2: frames 1-2 silent, frame 3 plays movie frame 0");
    }
    {   // 30000/1001: frames alternate 1601/1602 samples; the stream has no gaps
        auto clip = dcClip(48000 * 2);
        gfcAudioEngine e; gfcFakeAudioOutput out; e.setOutput(&out); out.open(&e);
        e.setTrackClip(0, clip, 30000, 1001, 59); e.setPlayerRate(30000, 1001);
        gfcAudioTransport t{1, 1, 59, LOOPMODELOOP_ID};
        e.startLeading(t); std::vector<float> cap; out.pump(48000, &cap);
        float lo = 1.f; for (int i = 144; i < 48000; ++i) lo = std::min(lo, cap[size_t(2*i)]);
        check(lo > 0.499f && e.leaderFrame() == 30, "29.97: one second streams seamlessly and lands on frame 30");
    }
    std::printf("AUDIO-WALK: pass=%d fail=%d\n", pass, fail);
    return fail;
}

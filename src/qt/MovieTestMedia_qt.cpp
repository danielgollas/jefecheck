// SPDX-License-Identifier: GPL-2.0-or-later
#include "MovieTestMedia_qt.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStringList>

#include "VideoEncoder_qt.h"

namespace jefe::qt::testmedia {

namespace {

const QString kTestsrc2 = QStringLiteral("testsrc2=size=320x180:rate=%1,format=rgb24");
const QString kBars = QStringLiteral("smptehdbars=size=%1:rate=24,format=rgb24");
// prores_ks writes the matrix/range into its frame header only from these
// options; scale's out_color_matrix alone leaves the header "unknown".
const QStringList kTag709 = {"-colorspace", "bt709", "-color_primaries", "bt709",
                             "-color_trc", "bt709", "-color_range", "tv"};

bool runFfmpeg(const QString& ffmpeg, const QStringList& args, QString* err) {
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(ffmpeg, QStringList{"-hide_banner", "-nostdin", "-loglevel", "error"} + args);
    if (!p.waitForStarted(10000) || !p.waitForFinished(120000) ||
        p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (err)
            *err = QStringLiteral("ffmpeg failed for %1: %2")
                       .arg(args.isEmpty() ? QString() : args.last(),
                            QString::fromLocal8Bit(p.readAll()).trimmed());
        return false;
    }
    return true;
}

bool copyFile(const QString& from, const QString& to, QString* err) {
    QFile::remove(to);
    if (QFile::copy(from, to)) return true;
    if (err) *err = QStringLiteral("cannot copy %1 to %2").arg(from, to);
    return false;
}

// First half of a faststart copy: the moov atom (at the front) survives, so
// the file probes 48 frames but decodes only about half of them.
bool writeTruncated(const QString& ffmpeg, const QDir& d, QString* err) {
    const QString fast = d.filePath("truncated_faststart.tmp.mov");
    if (!runFfmpeg(ffmpeg, {"-y", "-i", d.filePath("prores422hq_23976.mov"), "-c", "copy",
                            "-movflags", "+faststart", fast}, err))
        return false;
    QFile in(fast);
    if (!in.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("cannot read %1").arg(fast);
        return false;
    }
    const QByteArray half = in.read(in.size() / 2);
    in.close();
    QFile::remove(fast);
    QFile out(d.filePath("truncated.mov"));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(half) != half.size()) {
        if (err) *err = QStringLiteral("cannot write truncated.mov");
        return false;
    }
    return true;
}

}  // namespace

bool generate(const QString& dir, QString* err) {
    const QString ffmpeg = VideoEncoder_Qt::findFfmpeg();
    if (ffmpeg.isEmpty()) {
        if (err) *err = QStringLiteral("ffmpeg not found");
        return false;
    }
    QDir d(dir);
    if (!d.mkpath(".")) {
        if (err) *err = QStringLiteral("cannot create %1").arg(dir);
        return false;
    }
    auto out = [&](const char* name) { return d.filePath(QString::fromLatin1(name)); };
    const QString bars = kBars.arg("320x180");

    const QList<QStringList> jobs = {
        QStringList{"-y", "-f", "lavfi", "-i", kTestsrc2.arg("24000/1001"), "-frames:v", "48",
         "-vf", "scale=out_color_matrix=bt709:out_range=tv,format=yuv422p10le",
         "-c:v", "prores_ks", "-profile:v", "3", "-timecode", "01:00:00:00"} + kTag709 + QStringList{out("prores422hq_23976.mov")},
        QStringList{"-y", "-f", "lavfi", "-i", kTestsrc2.arg("24"), "-frames:v", "48",
         "-vf", "scale=out_color_matrix=bt709:out_range=tv,format=yuv444p12le",
         "-c:v", "prores_ks", "-profile:v", "4"} + kTag709 + QStringList{out("prores4444_24.mov")},
        {"-y", "-f", "lavfi", "-i", kTestsrc2.arg("25"), "-frames:v", "48",
         "-vf", "scale=out_color_matrix=bt709:out_range=tv,format=yuv420p",
         "-c:v", "libx264", "-g", "12", "-colorspace", "bt709", "-color_primaries", "bt709",
         "-color_trc", "bt709", "-color_range", "tv", out("h264_25.mp4")},
        // Long-GOP clips with the encoders' default GOP and B-frames: a short skip
        // forward decodes on instead of seeking (gfcMovieReader::readFrame).
        {"-y", "-f", "lavfi", "-i", kTestsrc2.arg("25"), "-frames:v", "60",
         "-vf", "format=yuv420p", "-c:v", "libx264", out("h264_longgop_25.mp4")},
        {"-y", "-f", "lavfi", "-i", kTestsrc2.arg("25"), "-frames:v", "60",
         "-vf", "format=yuv420p", "-c:v", "libx265", "-x265-params", "log-level=error", out("hevc_longgop_25.mp4")},
        // Colour-test bars (--video-colour-test). The truth is the exact 8-bit
        // RGB the clips were made from (a 16-bit truth from swscale is itself
        // off by ~0.4 code). Chroma is box-filtered (flags=area) so it does not
        // bleed past the bar edges, and the H.264/odd clips are lossless, so a
        // correct decode lands within rounding of the truth.
        {"-y", "-f", "lavfi", "-i", bars, "-frames:v", "8", "-pix_fmt", "rgb24",
         d.filePath("bars_truth.%04d.png")},
        QStringList{"-y", "-f", "lavfi", "-i", bars, "-frames:v", "8",
         "-vf", "scale=out_color_matrix=bt709:out_range=tv:flags=area,format=yuv422p10le",
         "-c:v", "prores_ks", "-profile:v", "3"} + kTag709 + QStringList{out("bars_709tv.mov")},
        {"-y", "-f", "lavfi", "-i", bars, "-frames:v", "8",
         "-vf", "scale=out_color_matrix=bt601:out_range=pc:flags=area,format=yuv420p",
         "-c:v", "libx264", "-qp", "0", "-g", "4", "-colorspace", "smpte170m", "-color_primaries", "smpte170m",
         "-color_trc", "smpte170m", "-color_range", "pc", out("bars_601full.mp4")},
        {"-y", "-f", "lavfi", "-i", bars, "-frames:v", "8",
         "-vf", "scale=out_color_matrix=bt709:out_range=tv:flags=area,format=yuv420p",
         "-c:v", "libx264", "-qp", "0", "-g", "4",
         "-bsf:v", "h264_metadata=colour_primaries=2:transfer_characteristics=2:matrix_coefficients=2:video_full_range_flag=0",
         out("bars_untagged.mp4")},
        // Odd-sized phone-style frame, 4:2:0 with ceil(w/2) x ceil(h/2) chroma:
        // made at 322x182 and cropped exactly in YUV, so its chroma keeps the
        // 2:1 grid real encoders use (swscale resampling straight to 321x181
        // drifts the chroma grid). Lossless VP9, since H.264/HEVC cannot code
        // odd 4:2:0 sizes; Rec.709 video range, untagged.
        {"-y", "-f", "lavfi", "-i", kBars.arg("322x182"), "-frames:v", "8", "-vf", "crop=321:181:0:0",
         "-pix_fmt", "rgb24", d.filePath("bars_odd_truth.%04d.png")},
        {"-y", "-f", "lavfi", "-i", kBars.arg("322x182"), "-frames:v", "8",
         "-vf", "scale=out_color_matrix=bt709:out_range=tv:flags=area,format=yuv420p,crop=321:181:0:0:exact=1",
         "-c:v", "libvpx-vp9", "-lossless", "1", out("bars_709tv_odd.mp4")},
        {"-y", "-f", "lavfi",
         "-i", "color=c=black:size=320x180:rate=24000/1001,drawbox=w=iw:h=ih:color=white:t=fill:enable='eq(mod(n\\,24)\\,0)',format=yuv422p10le",
         "-f", "lavfi",
         "-i", "aevalsrc='if(lt(mod(floor(t*24000/1001+0.0001)\\,24)\\,1)\\,0.8*sin(2*PI*1000*t)\\,0)':s=48000:c=stereo",
         "-t", "10", "-c:v", "prores_ks", "-profile:v", "3", "-c:a", "pcm_s16le",
         "-timecode", "01:00:00:00", out("flashbeep_23976.mov")},
        {"-y", "-f", "lavfi",
         "-i", "color=c=black:size=320x180:rate=25,drawbox=w=iw:h=ih:color=white:t=fill:enable='eq(mod(n\\,25)\\,0)',format=yuv422p10le",
         "-f", "lavfi",
         "-i", "aevalsrc='if(lt(mod(floor(t*25+0.0001)\\,25)\\,1)\\,0.8*sin(2*PI*1000*t)\\,0)':s=48000:c=stereo",
         "-t", "10", "-c:v", "prores_ks", "-profile:v", "3", "-c:a", "pcm_s16le",
         "-timecode", "01:00:00:00", out("flashbeep_25.mov")},
        QStringList{"-y", "-f", "lavfi", "-i", kTestsrc2.arg("30000/1001"), "-frames:v", "10",
         "-vf", "scale=out_color_matrix=bt709:out_range=tv,format=yuv422p10le",
         "-c:v", "prores_ks", "-profile:v", "3", "-timecode", "00:59:59;28"} + kTag709 + QStringList{out("df_2997.mov")},
        // A real timestamp gap: input frame 10 is dropped and the rest keep
        // their times (pts 9 -> 11), so decoding forward meets a jump.
        {"-y", "-f", "lavfi", "-i", kTestsrc2.arg("25"), "-frames:v", "120",
         "-vf", "select='not(eq(n\\,10))',scale=out_color_matrix=bt709:out_range=tv,format=yuv422p10le",
         "-fps_mode", "passthrough", "-c:v", "prores_ks", "-profile:v", "0", out("gap_25.mov")},
        // Sound only (a podcast-style .mp4): no video stream at all.
        {"-y", "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000", "-t", "2", "-c:a", "aac",
         out("audio_only.mp4")},
        {"-y", "-f", "lavfi", "-i", kTestsrc2.arg("24"),
         "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100", "-frames:v", "48",
         "-vf", "scale=out_color_matrix=bt709:out_range=tv,format=yuv420p", "-c:v", "libx264",
         "-ac", "1", "-c:a", "aac", "-shortest", out("mono441_24.mp4")},
    };
    for (const QStringList& args : jobs)
        if (!runFfmpeg(ffmpeg, args, err)) return false;

    for (const char* copy : {"shot_v003.mov", "a_001.mov", "a_002.mov"})
        if (!copyFile(out("prores422hq_23976.mov"), out(copy), err)) return false;
    if (!writeTruncated(ffmpeg, d, err)) return false;

    QFile empty(out("empty.mov"));
    if (!empty.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = QStringLiteral("cannot write empty.mov");
        return false;
    }
    return true;
}

bool generateCacheMedia(const QString& dir, QString* err) {
    const QString ffmpeg = VideoEncoder_Qt::findFfmpeg();
    if (ffmpeg.isEmpty()) {
        if (err) *err = QStringLiteral("ffmpeg not found");
        return false;
    }
    QDir d(dir);
    if (!d.mkpath(".")) {
        if (err) *err = QStringLiteral("cannot create %1").arg(dir);
        return false;
    }
    auto out = [&](const char* name) { return d.filePath(QString::fromLatin1(name)); };
    const QStringList h264 = {"-vf", "format=yuv420p", "-c:v", "libx264"};
    const QList<QStringList> jobs = {
        // The flashbeep recipe at 24/1: a 100 ms 1 kHz beep at the start of every second.
        QStringList{"-y", "-f", "lavfi", "-i", kTestsrc2.arg("24"),
         "-f", "lavfi", "-i", "aevalsrc='if(lt(mod(t\\,1)\\,0.1)\\,0.8*sin(2*PI*1000*t)\\,0)':s=48000:c=stereo",
         "-frames:v", "240", "-t", "10"} + h264 + QStringList{"-c:a", "aac", out("cache_beep_24.mp4")},
        QStringList{"-y", "-f", "lavfi", "-i", kTestsrc2.arg("24"), "-frames:v", "240"} + h264 +
            QStringList{"-an", out("cache_silent_24.mp4")},
        {"-y", "-f", "lavfi", "-i", kTestsrc2.arg("24"), "-frames:v", "120", "-pix_fmt", "rgb24",
         "-f", "image2", "-start_number", "1", d.filePath("cache_seq.%04d.png")},
        QStringList{"-y", "-f", "lavfi", "-i", QStringLiteral("testsrc2=size=1920x1080:rate=24,format=rgb24"),
         "-frames:v", "48"} + h264 + QStringList{"-an", out("cache_big_24.mp4")},
    };
    for (const QStringList& args : jobs)
        if (!runFfmpeg(ffmpeg, args, err)) return false;
    return true;
}

}  // namespace jefe::qt::testmedia

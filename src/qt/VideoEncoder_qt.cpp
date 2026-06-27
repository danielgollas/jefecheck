#include "VideoEncoder_qt.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

QString VideoEncoder_Qt::findFfmpeg() {
    // 1. Explicit env override (used by tests / power users).
    const QByteArray env = qgetenv("JEFECHECK_FFMPEG");
    if (!env.isEmpty() && QFileInfo(QString::fromLocal8Bit(env)).isExecutable())
        return QString::fromLocal8Bit(env);

    // 2. User preference.
    QSettings s;
    const QString pref = s.value("Render/ffmpegPath").toString();
    if (!pref.isEmpty() && QFileInfo(pref).isExecutable())
        return pref;

    // 3. Bundled alongside the app. macOS: Contents/Resources/ffmpeg (next
    //    to FX/fonts); Linux/Windows: next to the executable.
    const QString appDir = QCoreApplication::applicationDirPath();
#if defined(Q_OS_MAC)
    const QStringList candidates = {
        appDir + "/../Resources/ffmpeg",
        appDir + "/ffmpeg",
    };
#elif defined(Q_OS_WIN)
    const QStringList candidates = { appDir + "/ffmpeg.exe" };
#else
    const QStringList candidates = {
        appDir + "/ffmpeg",
        appDir + "/../lib/jefecheck/ffmpeg",
    };
#endif
    for (const QString& c : candidates) {
        if (QFileInfo(c).isExecutable()) return QDir(c).absolutePath();
    }

    // 4. System PATH.
    const QString onPath = QStandardPaths::findExecutable("ffmpeg");
    if (!onPath.isEmpty()) return onPath;

    return QString();
}

QString VideoEncoder_Qt::containerExt(Codec c) {
    return (c == Codec::ProRes) ? "mov" : "mp4";
}

QString VideoEncoder_Qt::codecLabel(Codec c) {
    switch (c) {
        case Codec::H264:   return "H.264 (MP4)";
        case Codec::H265:   return "H.265 (MP4)";
        case Codec::ProRes: return "ProRes (MOV)";
    }
    return "Video";
}

VideoEncoder_Qt::VideoEncoder_Qt(QObject* parent) : QObject(parent) {}
VideoEncoder_Qt::~VideoEncoder_Qt() = default;

bool VideoEncoder_Qt::isRunning() const {
    return proc_ != nullptr;
}

void VideoEncoder_Qt::start(const Params& p) {
    const QString ffmpeg = findFfmpeg();
    if (ffmpeg.isEmpty()) {
        emit finished(false, "FFmpeg not found. Set its path in Preferences "
                             "or install it on your PATH.");
        return;
    }

    // Target canvas = first frame's size, rounded up to even (h264/h265
    // require even dimensions). All frames are scaled to fit and padded,
    // so a sequence with varying per-frame sizes still encodes cleanly.
    int w = 0, h = 0;
    {
        const QString firstFrame = QString::asprintf(
            p.framePattern.toLocal8Bit().constData(), p.startNumber);
        QImageReader r(firstFrame);
        const QSize sz = r.size();
        if (sz.isValid()) { w = sz.width(); h = sz.height(); }
    }
    if (w <= 0 || h <= 0) {
        emit finished(false, "Could not read the rendered frames to encode.");
        return;
    }
    w += (w & 1);
    h += (h & 1);

    total_ = p.frameCount;
    cancelled_ = false;
    tailLog_.clear();

    const QString vf =
        QString("scale=%1:%2:force_original_aspect_ratio=decrease,"
                "pad=%1:%2:(ow-iw)/2:(oh-ih)/2:black,setsar=1")
            .arg(w).arg(h);

    QStringList args;
    args << "-y"
         << "-framerate" << QString::number(p.fps)
         << "-start_number" << QString::number(p.startNumber)
         << "-i" << p.framePattern
         << "-vf" << vf;

    // x264/x265 preset names by index (Params.preset).
    static const char* kPresets[] = {
        "ultrafast", "superfast", "veryfast", "faster", "medium",
        "slow", "slower", "veryslow", "placebo"
    };
    const int pn = sizeof(kPresets) / sizeof(kPresets[0]);
    const QString preset = kPresets[(p.preset >= 0 && p.preset < pn) ? p.preset : 4];

    switch (p.codec) {
        case Codec::H264: {
            args << "-c:v" << "libx264" << "-preset" << preset
                 << "-pix_fmt" << "yuv420p";
            if (p.bitrateKbps > 0) {
                args << "-b:v" << QString("%1k").arg(p.bitrateKbps);
            } else {
                const int crf = 28 - int((p.quality / 100.0) * 14 + 0.5); // 14..28
                args << "-crf" << QString::number(crf);
            }
            break;
        }
        case Codec::H265: {
            args << "-c:v" << "libx265" << "-preset" << preset
                 << "-pix_fmt" << "yuv420p" << "-tag:v" << "hvc1";
            if (p.bitrateKbps > 0) {
                args << "-b:v" << QString("%1k").arg(p.bitrateKbps);
            } else {
                const int crf = 30 - int((p.quality / 100.0) * 14 + 0.5); // 16..30
                args << "-crf" << QString::number(crf);
            }
            break;
        }
        case Codec::ProRes: {
            // quality → profile: proxy/lt/standard/hq
            int profile = 3;
            if (p.quality < 25)      profile = 0;
            else if (p.quality < 50) profile = 1;
            else if (p.quality < 75) profile = 2;
            args << "-c:v" << "prores_ks"
                 << "-profile:v" << QString::number(profile)
                 << "-pix_fmt" << "yuv422p10le";
            break;
        }
    }
    args << p.outFile;

    proc_ = new QProcess(this);
    proc_->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc_, &QProcess::readyRead, this, &VideoEncoder_Qt::onStderr);
    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { onProcFinished(code); });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (proc_) onProcFinished(-1);
    });
    proc_->start(ffmpeg, args);
}

void VideoEncoder_Qt::cancel() {
    cancelled_ = true;
    if (proc_) {
        proc_->kill();
    }
}

void VideoEncoder_Qt::onStderr() {
    if (!proc_) return;
    const QString chunk = QString::fromLocal8Bit(proc_->readAll());
    tailLog_ += chunk;
    if (tailLog_.size() > 4000) tailLog_ = tailLog_.right(4000);

    // ffmpeg emits "frame=  12 ..." lines; report the latest.
    static const QRegularExpression re("frame=\\s*(\\d+)");
    auto it = re.globalMatch(chunk);
    int last = -1;
    while (it.hasNext()) last = it.next().captured(1).toInt();
    if (last >= 0) emit progress(qMin(last, total_), total_);
}

void VideoEncoder_Qt::onProcFinished(int exitCode) {
    if (!proc_) return;
    onStderr();  // drain any final output
    proc_->deleteLater();
    proc_ = nullptr;

    if (cancelled_) {
        emit finished(false, "Encoding cancelled.");
        return;
    }
    if (exitCode != 0) {
        QString msg = "FFmpeg failed";
        const int nl = tailLog_.lastIndexOf('\n', tailLog_.size() - 2);
        if (nl >= 0) msg += ": " + tailLog_.mid(nl + 1).trimmed();
        emit finished(false, msg);
        return;
    }
    emit finished(true, QString());
}

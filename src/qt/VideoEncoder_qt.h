// Encodes a rendered image sequence into a video via the FFmpeg CLI.
//
// We shell out to `ffmpeg` (QProcess) rather than linking libav* — it keeps
// the GPL app free of the static-link/licensing weight, gives one codepath
// on macOS/Linux/Windows, and composes with the existing sequence render
// (render frames → encode them). Progress is parsed from ffmpeg's stderr
// `frame=` output; Cancel kills the process.
#ifndef JEFECHECK_QT_VIDEO_ENCODER_H
#define JEFECHECK_QT_VIDEO_ENCODER_H

#include <QObject>
#include <QString>

class QProcess;

class VideoEncoder_Qt : public QObject {
    Q_OBJECT
public:
    enum class Codec { H264, H265, ProRes };

    explicit VideoEncoder_Qt(QObject* parent = nullptr);
    ~VideoEncoder_Qt() override;

    // Resolve an ffmpeg executable: $JEFECHECK_FFMPEG → QSettings
    // "Render/ffmpegPath" → bundled (next to the app / in Resources) →
    // system PATH. Empty if none found.
    static QString findFfmpeg();
    static bool available() { return !findFfmpeg().isEmpty(); }

    // The file extension a codec's container uses ("mp4" / "mov").
    static QString containerExt(Codec c);
    static QString codecLabel(Codec c);

    struct Params {
        QString framePattern;   // printf pattern, e.g. /tmp/x/f_%04d.png
        int     startNumber = 1;
        int     frameCount  = 0;  // for progress total
        int     fps         = 24;
        Codec   codec       = Codec::H264;
        int     quality     = 80; // 0..100 (maps to crf / prores profile)
        int     bitrateKbps = 0;  // 0 = constant-quality (CRF); >0 = target bitrate
        int     preset      = 4;  // x264/x265 preset index (0 ultrafast … 8 veryslow)
        QString outFile;          // full output path incl. extension
    };

    // Starts encoding asynchronously. Emits progress()/finished(). Safe to
    // call once per instance per encode.
    void start(const Params& p);
    void cancel();
    bool isRunning() const;

signals:
    void progress(int doneFrames, int totalFrames);
    void finished(bool ok, const QString& message);

private:
    void onStderr();
    void onProcFinished(int exitCode);

    QProcess* proc_ = nullptr;
    int total_ = 0;
    bool cancelled_ = false;
    QString tailLog_;   // last chunk of stderr for error reporting
};

#endif

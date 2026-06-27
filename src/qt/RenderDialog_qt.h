// File → Render… dialog for the Qt port. PR-39a ships a minimal
// modal dialog covering quadrant / format / range / scale / output
// path / prefix / postfix / padding, plus a Render button that calls
// `triggerSyncRender` synchronously (the UI freezes until the render
// completes — async + cancel comes in PR-39b along with a QThread
// driver for `plateManager.renderPlate`).
//
// Format-specific quality knobs (jpeg quality / png level / tiff + exr
// compression / exr depth) are wired via a QStackedWidget keyed on the
// format index. Video codec and "create movie" / "delete frames after"
// remain TODO (FLTK used mencoder, Linux-only).
#ifndef JEFECHECK_QT_RENDER_DIALOG_H
#define JEFECHECK_QT_RENDER_DIALOG_H

#include <QDialog>

#include "SequenceLoadBridge_qt.h"   // jefe::qt::RenderParams (glad-free)

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class GlViewport_Qt;

class RenderDialog_Qt : public QDialog {
    Q_OBJECT
public:
    explicit RenderDialog_Qt(QWidget* parent = nullptr);

private slots:
    void browseForOutputDir();
    void onAnyFieldChanged();
    void onAutoRangeClicked();
    void onRenderClicked();

private:
    void rebuildPreview();
    bool inputsValid() const;
    // Show the quality page that matches the currently-selected format.
    void updateQualityPage();

    // Incremental render driven by the event loop: one frame per
    // QTimer::singleShot(0) so the dialog stays responsive and Cancel is
    // honored between frames. GL stays on the main thread (the viewport's
    // context is thread-affine, so a worker thread isn't safe here).
    void startRender();
    void renderStep();
    void finishRender(bool cancelled);
    void startEncode();             // begins the ffmpeg pass (video formats)
    void cleanupVideoTemp();

    bool rendering_ = false;
    bool cancelRequested_ = false;
    int  renderCur_ = 0;        // next frame to render
    int  renderTo_ = 0;         // last frame (inclusive)
    int  renderDone_ = 0;       // frames written so far
    int  renderTotal_ = 0;
    GlViewport_Qt* renderVp_ = nullptr;
    jefe::qt::RenderParams renderParams_;

    // Video export: when the chosen format is a video codec, frames are
    // rendered as PNG into a temp dir, then encoded with FFmpeg.
    bool    renderIsVideo_ = false;
    int     videoFormatIdx_ = 0;    // index into the format combo
    QString videoOutFile_;
    QString videoTmpDir_;           // temp PNG sequence dir (removed after)
    class VideoEncoder_Qt* videoEncoder_ = nullptr;

    QComboBox* quadrantCombo_ = nullptr;
    QComboBox* formatCombo_ = nullptr;
    QSpinBox* startFrameSpin_ = nullptr;
    QSpinBox* endFrameSpin_ = nullptr;
    QSpinBox* paddingSpin_ = nullptr;
    QDoubleSpinBox* scaleSpin_ = nullptr;
    QLineEdit* pathEdit_ = nullptr;
    QPushButton* browseBtn_ = nullptr;
    QPushButton* autoRangeBtn_ = nullptr;
    QLineEdit* prefixEdit_ = nullptr;
    QLineEdit* postfixEdit_ = nullptr;

    // Format-specific quality controls, swapped by a QStackedWidget keyed
    // on the format combo index (0 JPEG, 1 EXR, 2 TIFF, 3 TGA, 4 BMP, 5 PNG).
    QStackedWidget* qualityStack_ = nullptr;
    QSpinBox* jpegQualitySpin_ = nullptr;
    QSpinBox* pngLevelSpin_ = nullptr;
    QComboBox* tiffCompCombo_ = nullptr;
    QComboBox* exrDepthCombo_ = nullptr;
    QComboBox* exrCompCombo_ = nullptr;
    QSpinBox* videoFpsSpin_ = nullptr;
    QSpinBox* videoQualitySpin_ = nullptr;

    QLabel* previewLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QPushButton* renderBtn_ = nullptr;
    QPushButton* doneBtn_ = nullptr;
};

#endif

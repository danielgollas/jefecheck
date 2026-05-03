// File → Render… dialog for the Qt port. PR-39a ships a minimal
// modal dialog covering quadrant / format / range / scale / output
// path / prefix / postfix / padding, plus a Render button that calls
// `triggerSyncRender` synchronously (the UI freezes until the render
// completes — async + cancel comes in PR-39b along with a QThread
// driver for `plateManager.renderPlate`).
//
// Format-specific quality knobs (jpeg/png/tiff/exr), video codec, and
// "create movie" / "delete frames after" land in PR-39b.
#ifndef JEFECHECK_QT_RENDER_DIALOG_H
#define JEFECHECK_QT_RENDER_DIALOG_H

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

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
    QLabel* previewLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* renderBtn_ = nullptr;
    QPushButton* doneBtn_ = nullptr;
};

#endif

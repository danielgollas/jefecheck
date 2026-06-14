#pragma once

#include <QDialog>

class TrackStrip_Qt;
class QPushButton;
class GlViewport_Qt;

class LoadWindowDialog_Qt : public QDialog {
    Q_OBJECT
public:
    explicit LoadWindowDialog_Qt(GlViewport_Qt* viewport, QWidget* parent = nullptr);

    // Called by MainWindow_Qt when the viewport forwards a drop while
    // this dialog is showing.
    void setTrackFilename(int plateIdx, const QString& path);

protected:
    void showEvent(QShowEvent* e) override;
    void reject()                  override;
    void accept()                  override;

private slots:
    void onTrackEdited(int trackIdx);

private:
    GlViewport_Qt* viewport_ = nullptr;
    TrackStrip_Qt* strips_[4] {nullptr, nullptr, nullptr, nullptr};
    QPushButton*   loadAll_  = nullptr;
};

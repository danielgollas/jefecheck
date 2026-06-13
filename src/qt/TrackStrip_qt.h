#pragma once

#include <QStringList>
#include <QString>
#include <QWidget>

class QLineEdit;
class QPushButton;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QLabel;
class QToolButton;

class TrackStrip_Qt : public QWidget {
    Q_OBJECT
public:
    explicit TrackStrip_Qt(int trackIdx, QWidget* parent = nullptr);

    // Snap widget state to current per-track state via the bridge.
    // Called by LoadWindowDialog_Qt when the modal opens and after
    // every preview re-decode.
    void refreshFromGUI();

    // Refresh the header label and estimates label only (cheaper than
    // refreshFromGUI when widget state hasn't moved).
    void refreshDerivedLabels();

    // Flip header label to a red error state with the given reason.
    void markError(const QString& reason);

    // Called by the dialog when a drop while modal-open targets this strip.
    void setFilenameFromDrop(const QString& path);

    int trackIndex() const { return trackIdx_; }

signals:
    // Emitted on any user-initiated edit. Bridge runs reloadTrackPreview
    // and the dialog refreshes our header/estimates/channel options.
    void trackEdited(int trackIdx);

private slots:
    void onFilenameChanged();
    void onBrowse();
    void onFromChanged(int v);
    void onToChanged(int v);
    void onScaleChanged(int idx);
    void onBitDepthChanged(int idx);
    void onChannelChanged(int idx);
    void onCropToggled(bool on);
    void onReload();
    void onUnload();
    void onRecentSelected(const QString& path);

private:
    void pushRecentPath(const QString& path);
    QStringList loadRecentPaths() const;
    void rebuildRecentMenu();

    int trackIdx_;
    bool refreshing_ = false;

    QLineEdit*   filename_  = nullptr;
    QPushButton* browse_    = nullptr;
    QSpinBox*    from_      = nullptr;
    QSpinBox*    to_        = nullptr;
    QComboBox*   scale_     = nullptr;
    QComboBox*   bitDepth_  = nullptr;
    QComboBox*   channels_  = nullptr;
    QCheckBox*   crop_      = nullptr;
    QPushButton* reload_    = nullptr;
    QPushButton* unload_    = nullptr;
    QToolButton* recent_    = nullptr;
};

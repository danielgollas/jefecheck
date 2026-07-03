// Remote sessions modal dialog for the Qt port. PR-41a ships the
// dialog scaffolding plus connect-as-server / connect-as-client form
// fields. The actual chat log, participant list, and a per-event
// refresh signal land in PR-41b — that work needs the existing
// `gfcnetworkclientgui_qt` / `gfcnetworkservergui_qt` adapters to
// be hooked up to live widgets, which we'll do once the network
// manager exposes a connection-state signal we can subscribe to.
//
// Modal-dialog rather than dock: we already have three left-side
// docks (FX Params, Playlist) and adding a fourth caused the Mac AX
// bridge to drop child widgets out of its tree under sweep load.
// Mirrors the FLTK side, where `remoteWindow.fl` was a separate
// window too.
#ifndef JEFECHECK_QT_REMOTE_PANEL_H
#define JEFECHECK_QT_REMOTE_PANEL_H

#include <QDialog>

class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTextEdit;

class RemoteDialog_Qt : public QDialog {
    Q_OBJECT
public:
    explicit RemoteDialog_Qt(QWidget* parent = nullptr);

public slots:
    // Reads `isRemoteConnected` / `isRemoteServer` and refreshes the
    // status label and button enabled-states. Called on construction
    // and after every connect/disconnect click.
    void refreshConnectionState();

private:
    void onStartServerClicked();
    void onConnectClientClicked();
    void onDisconnectClicked();

    // Server tab.
    QLineEdit* serverNameEdit_ = nullptr;
    QSpinBox* serverPortSpin_ = nullptr;
    QLineEdit* serverPasswordEdit_ = nullptr;
    QPushButton* startServerBtn_ = nullptr;

    // Client tab.
    QLineEdit* clientNameEdit_ = nullptr;
    QLineEdit* clientIPEdit_ = nullptr;
    QSpinBox* clientPortSpin_ = nullptr;
    QLineEdit* clientPasswordEdit_ = nullptr;
    QPushButton* connectClientBtn_ = nullptr;

    // Shared.
    QPushButton* disconnectBtn_ = nullptr;
    QLabel*      statusLabel_ = nullptr;

    // Live state widgets (refreshed by refreshConnectionState).
    QListWidget* participantsList_ = nullptr;
    QLabel*      errorLabel_ = nullptr;
    QTextEdit*   chatLogView_ = nullptr;   // collapsible full chat history
    QGroupBox*   chatLogBox_ = nullptr;    // checkable → collapses chatLogView_
};

#endif

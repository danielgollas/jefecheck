// Remote-session panel for the Qt port. A dockable QWidget (hosted in a
// QDockWidget by MainWindow, like the FX / LUT / Playlist panels) with
// connect-as-server / connect-as-client forms, live status + participant
// list + error line, a collapsible chat log and connection log, and a chat
// input field. Received chat/pointers also render as the GL viewport overlay.
#ifndef JEFECHECK_QT_REMOTE_PANEL_H
#define JEFECHECK_QT_REMOTE_PANEL_H

#include <QWidget>

class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTextEdit;

class RemoteDialog_Qt : public QWidget {
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
    void onChatSubmit();   // send the chat input field's text

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
    QTextEdit*   netLogView_ = nullptr;    // collapsible connection/handshake log
    QGroupBox*   netLogBox_ = nullptr;     // checkable → collapses netLogView_
    QLineEdit*   chatInput_ = nullptr;     // type + Enter to send a chat message
};

#endif

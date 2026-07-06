// Remote-session panel for the Qt port. A dockable QWidget (hosted in a
// QDockWidget by MainWindow, like the FX / LUT / Playlist panels) with
// connect-as-server / connect-as-client forms, live status + participant
// list + error line, a collapsible chat log and connection log, and a chat
// input field. Received chat/pointers also render as the GL viewport overlay.
#ifndef JEFECHECK_QT_REMOTE_PANEL_H
#define JEFECHECK_QT_REMOTE_PANEL_H

#include <QWidget>

#include <string>
#include <vector>

class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QTabWidget;
class QTextEdit;
class QVBoxLayout;
class QWidget;

namespace jefe::qt { struct ChatEntry; }

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

    // Appends only the log lines past `shownCount` to `view` (append-only
    // fast path that preserves scroll/selection); rebuilds if the source
    // shrank. Updates `shownCount` in place.
    void appendNewLogLines(QTextEdit* view,
                           const std::vector<std::string>& lines,
                           int& shownCount);

    // Appends one chat message as a bubble row (alternating alignment,
    // per-user color, HH:MM). System/LOAD types render centered without a bubble.
    void appendChatBubble(const jefe::qt::ChatEntry& e);

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
    QLabel*      statusDot_ = nullptr;     // colored ● reflecting connection state

    // Contextual sections: connect forms show when disconnected, the session
    // (participants + chat) shows when connected. Toggled in refreshConnectionState.
    QWidget*     connectPanel_ = nullptr;  // segmented Host/Join toggle + forms
    QPushButton* hostToggle_ = nullptr;    // segmented control (Host)
    QPushButton* joinToggle_ = nullptr;    // segmented control (Join)
    QWidget*     hostForm_ = nullptr;
    QWidget*     joinForm_ = nullptr;
    QWidget*     sessionBox_ = nullptr;    // participants + chat + disconnect
    QLabel*      participantsHeader_ = nullptr;

    // Live state widgets (refreshed by refreshConnectionState).
    QListWidget* participantsList_ = nullptr;
    QLabel*      errorLabel_ = nullptr;
    QScrollArea* chatScroll_ = nullptr;    // replaces chatLogView_
    QWidget*     chatContent_ = nullptr;   // scroll content
    QVBoxLayout* chatLayout_ = nullptr;    // top-packed; bubbles appended here
    QGroupBox*   chatLogBox_ = nullptr;    // unused (chat is always-visible bubbles now)
    QTextEdit*   netLogView_ = nullptr;    // collapsible connection/handshake log
    QGroupBox*   netLogBox_ = nullptr;     // checkable → collapses netLogView_
    QLineEdit*   chatInput_ = nullptr;     // type + Enter to send a chat message

    // Incremental-refresh caches. refreshConnectionState() runs on every
    // inbound packet (up to ~60Hz while remote pointers stream), so it must
    // not clear+rebuild the append-only log views each time — that churns the
    // widgets and resets the user's scroll position and text selection. Track
    // how many lines/participants were last shown and only apply the delta.
    int          shownChatLines_ = 0;
    int          shownNetLogLines_ = 0;
    int          shownParticipants_ = -1;  // -1 forces the first rebuild
    QString      shownStatusText_;
};

#endif

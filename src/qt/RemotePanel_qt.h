// Remote-session panel for the Qt port. A dockable QWidget (hosted in a
// QDockWidget by MainWindow, like the FX / LUT / Playlist panels) with
// connect-as-server / connect-as-client forms, live status + participant
// list + error line, a collapsible chat log and connection log, and a chat
// input field. Received chat/pointers also render as the GL viewport overlay.
#ifndef JEFECHECK_QT_REMOTE_PANEL_H
#define JEFECHECK_QT_REMOTE_PANEL_H

#include <QWidget>

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QRadioButton;
class QComboBox;
class QCheckBox;
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

    // JEF-27 Cloud mode. Create/Join both run the (potentially 5s-blocking)
    // coordinator connect on a detached worker thread and marshal the result
    // back to onCloudConnectFinished on the GUI thread via a queued invoke
    // (guarded by a QPointer so a mid-connect dialog close can't crash).
    // --ui-preview: populate every Cloud/Join/admission state with sample data
    // so the layout can be reviewed without a live coordinator or a session.
    // Purely presentational; wires nothing.
public:
    void applyUiPreview();
    // Session groups: the "parent" a session is hosted under. Settings persist
    // per group (see RemoteSessionGroups_qt.h).
    void loadGroupIntoForm(const QString& name);
    void saveFormIntoGroup();
    void onNewGroupClicked();
    // Credits are host-only and hosting-only.
    void refreshCreditsVisibility(bool hosting);
    // True while --ui-preview is showing sample data. refreshConnectionState()
    // runs on a timer and would otherwise hide the session view (and with it
    // the admission rows and credits) on the very next tick, because nothing
    // is actually connected.
    //
    // CLEARED by any real connect action (see clearUiPreview). It must not
    // survive one: leaving it latched freezes the whole panel — no
    // participants, no status changes, a dead End Session button — while the
    // session underneath runs perfectly well and invisibly.
    bool uiPreviewActive_ = false;
    /** Drop preview state and let the panel track reality again. */
    void clearUiPreview();
private:
    QString coordinatorUrlSetting() const;
    void onCreateCloudClicked();
    void onJoinCloudClicked();
    void onCloudConnectFinished(bool wasHost);
    void copySessionCodeToClipboard();   // copies remoteSessionCode() to clipboard
    // Runs `work` (a blocking coordinator connect) on a detached worker thread
    // and queues onCloudConnectFinished(wasHost) back on the GUI thread, guarded
    // by a QPointer so a mid-connect close can't dangle.
    void launchCloudConnect(bool wasHost, std::function<void()> work);

    // Appends only the log lines past `shownCount` to `view` (append-only
    // fast path that preserves scroll/selection); rebuilds if the source
    // shrank. Updates `shownCount` in place.
    void appendNewLogLines(QTextEdit* view,
                           const std::vector<std::string>& lines,
                           int& shownCount);

    // Appends one chat message as a bubble row (alternating alignment,
    // per-user color, HH:MM). System/LOAD types render centered without a bubble.
    void appendChatBubble(const jefe::qt::ChatEntry& e);

    // JEF-30: builds one participant row widget (dot + name + rtt + kbps +
    // path) used as the participantsList_ item widget. Object names follow
    // the dotted-leaf scheme (reused across rows, like the chat_* widgets).
    QWidget* buildParticipantHealthRow(const QString& name);
    // Updates an existing row's dot/rtt/kbps/path labels in place (no relayout
    // churn) from a resolved health sample. `hasStats` false -> blank/gray
    // (participant with no matching peer-stats entry: self, or a name that
    // doesn't resolve on the client side — see developer_notes JEF-30).
    void updateParticipantHealthRow(QWidget* row, bool hasStats, bool connected,
                                    long rttMs, double kbps, const QString& path);

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
    QWidget*     connectPanel_ = nullptr;  // segmented Host/Cloud/Join toggle + forms
    QPushButton* hostToggle_ = nullptr;    // segmented control (Host)
    QPushButton* cloudToggle_ = nullptr;   // segmented control (Cloud) — JEF-27
    QPushButton* joinToggle_ = nullptr;    // segmented control (Join)
    QWidget*     hostForm_ = nullptr;
    QWidget*     cloudForm_ = nullptr;
    QWidget*     joinForm_ = nullptr;
    QWidget*     sessionBox_ = nullptr;    // participants + chat + disconnect
    QLabel*      participantsHeader_ = nullptr;

    // JefeCheck Cloud form widgets — JEF-27 / JEF-31. Hosting only; joining a
    // cloud session lives on the Join tab (it needs no account).
    QComboBox*   cloudGroupCombo_ = nullptr;      // parent session group
    QPushButton* cloudNewGroupBtn_ = nullptr;
    QLineEdit*   cloudHostNameEdit_ = nullptr;    // session name -> host nickname
    QCheckBox*   cloudKnockCheck_ = nullptr;      // per-group: require knock
    QLineEdit*   cloudPasswordEdit_ = nullptr;    // per-group: session password
    QSpinBox*    cloudTimeoutSpin_ = nullptr;     // per-group: idle cutoff (min)
    QSpinBox*    cloudMaxPeersSpin_ = nullptr;    // per-group: participant cap
    QPushButton* cloudHostBtn_ = nullptr;         // "Host on JefeCheck Cloud"
    QWidget*     cloudResultBox_ = nullptr;       // hidden until hosting
    QLineEdit*   cloudSessionCodeEdit_ = nullptr; // read-only assigned code
    QPushButton* cloudCopyBtn_ = nullptr;         // copy code to clipboard
    QLabel*      cloudAccountLabel_ = nullptr;    // signed-in email
    // Credits live in the STATUS HEADER, not the Cloud form: shown only while
    // HOSTING, and never to a joiner — joining is free, so a balance would be
    // meaningless to them. See refreshCreditsVisibility().
    QLabel*      creditsLabel_ = nullptr;
    QPushButton* cloudSignOutBtn_ = nullptr;

    // Unified Join tab (JEF-31): session code OR IP address.
    QRadioButton* joinModeCodeRadio_ = nullptr;
    QRadioButton* joinModeIpRadio_ = nullptr;
    QLineEdit*    joinCodeEdit_ = nullptr;
    QWidget*      joinIpRows_ = nullptr;          // Server/Port/Password group
    // Session-view banner keeping the code visible to a connected cloud host.
    QWidget*     cloudCodeBanner_ = nullptr;
    QLabel*      cloudCodeBannerLabel_ = nullptr;

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

    // JEF-30: per-peer byte-delta sample for kbps derivation, keyed by peer
    // display name (RemotePeerStat carries no PeerId). Cleared on disconnect
    // so a later session starts with a clean first-sample state.
    struct PeerHealthSample {
        unsigned long long bytes = 0;
        std::chrono::steady_clock::time_point ts;
        double lastKbps = -1.0;   // -1 = unknown (no stable interval sampled yet)
        bool hasSample = false;
    };
    std::unordered_map<std::string, PeerHealthSample> peerHealthSamples_;
};

#endif

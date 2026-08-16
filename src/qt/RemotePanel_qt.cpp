#include "RemotePanel_qt.h"
#include "qticons.h"
#include "CollapsibleSection_qt.h"
#include "SequenceLoadBridge_qt.h"
#include "RemoteSessionGroups_qt.h"

#include <QApplication>
#include <QClipboard>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QRadioButton>
#include <QComboBox>
#include <QCheckBox>
#include <QInputDialog>
#include <QListWidgetItem>
#include <QSplitter>
#include <QSysInfo>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include <thread>
#include <unordered_map>

namespace {

// JEF-30: session-health colors, matching the existing statusDot_ palette
// (green connected/good, amber warn/relay, gray unknown/no-stats).
const char* kHealthGood = "#5bb07a";
const char* kHealthWarn = "#d6a15b";
const char* kHealthGray = "#6a6a70";

// Scoped stylesheet — a clean dark surface with a muted slate accent for the
// primary (Host/Join) actions. Applied to the panel; children inherit.
const char* kRemoteStyle = R"(
#panel_remote { background: #1c1c1f; }
#panel_remote QLabel { color: #c8c8cc; }
#panel_remote QLabel[role="section"] {
    color: #8a8a90; font-size: 11px; font-weight: 600; padding-top: 2px;
}
#panel_remote QLabel[role="status"] { color: #e8e8ea; font-size: 13px; }
#panel_remote QLineEdit, #panel_remote QSpinBox {
    background: #2a2a2e; border: 1px solid #3a3a40; border-radius: 6px;
    padding: 6px 9px; color: #ececee; selection-background-color: #4a6172;
}
#panel_remote QLineEdit:focus, #panel_remote QSpinBox:focus {
    border: 1px solid #55707f;
}
/* Discreet buttons: small, transparent, thin border with a subtle color hint. */
#panel_remote QPushButton {
    background: transparent; border: 1px solid #3a3a40; border-radius: 5px;
    padding: 4px 12px; font-size: 11px; color: #cfcfd4;
}
#panel_remote QPushButton:hover { background: #2c2c31; border-color: #55707f; color: #e8e8ea; }
#panel_remote QPushButton:disabled { background: transparent; color: #6a6a70; border-color: #333; }
/* Primary (Start hosting / Connect / Send): a slate-tinted border + text hint. */
#panel_remote QPushButton[accent="true"] {
    background: transparent; border: 1px solid #4c6577; color: #a6c0d2; font-weight: 600;
}
#panel_remote QPushButton[accent="true"]:hover { background: #263038; border-color: #5f7d90; color: #cfe0ec; }
#panel_remote QPushButton[danger="true"] {
    background: transparent; border: 1px solid #4a3a3a; color: #c98b82;
    padding: 3px 12px; font-size: 11px;
}
#panel_remote QPushButton[danger="true"]:hover {
    background: #3a2a2a; border-color: #6a4444; color: #e0a097;
}
#panel_remote QPushButton[segment="true"] {
    background: transparent; border: 1px solid #3a3a40; color: #9a9aa0;
    padding: 5px 0; font-weight: 600; border-radius: 0; font-size: 11px;
}
#panel_remote QPushButton[segment="true"][segpos="left"] {
    border-top-left-radius: 6px; border-bottom-left-radius: 6px;
}
#panel_remote QPushButton[segment="true"][segpos="mid"] {
    border-radius: 0; border-left: none;
}
#panel_remote QPushButton[segment="true"][segpos="right"] {
    border-top-right-radius: 6px; border-bottom-right-radius: 6px; border-left: none;
}
#panel_remote QPushButton[segment="true"]:hover { color: #cfcfd4; background: #2a2a2f; }
#panel_remote QPushButton[segment="true"]:checked {
    background: #263038; border-color: #4c6577; color: #bcd2e0;
}
#panel_remote QWidget[card="true"] {
    background: #232327; border: 1px solid #3a3a40; border-radius: 8px;
    margin-top: 6px;
}
#panel_remote QListWidget, #panel_remote QTextEdit {
    background: #202024; border: 1px solid #34343a; border-radius: 8px;
    color: #dcdce0; padding: 4px;
}
/* Chat bubbles: alternating alignment, per-user color, phone-width. */
#panel_remote QScrollArea[chatsurface="true"], #panel_remote QWidget[chatsurface="true"] { background: #202024; border: 1px solid #34343a; border-radius: 8px; }
QFrame#chat_bubble { background: #2a2a2e; border: 1px solid #3a3a40; border-radius: 8px; }
QFrame#chat_bubble[self="true"] { background: #2e2620; border-color: #7a4a1e; }
QLabel#chat_header { color: #9a9aa0; font-size: 10px; background: transparent; border: none; }
QLabel#chat_message { color: #dcdce0; font-size: 12px; background: transparent; border: none; }
QLabel#chat_system { color: #6a6a70; font-size: 10px; font-style: italic; background: transparent; border: none; }
)";

// Build a Host form into `page` and expose its fields. Returns the page widget.
QWidget* makeHostPage(QLineEdit*& nameOut, QSpinBox*& portOut,
                      QLineEdit*& passwordOut, QPushButton*& startBtnOut) {
    auto* page = new QWidget();
    nameOut = new QLineEdit(page);
    nameOut->setObjectName("remote.server.name.edit");
    nameOut->setPlaceholderText("Session name");
    portOut = new QSpinBox(page);
    portOut->setObjectName("remote.server.port.spin");
    portOut->setRange(1024, 65535);
    portOut->setValue(60000);
    passwordOut = new QLineEdit(page);
    passwordOut->setObjectName("remote.server.password.edit");
    passwordOut->setEchoMode(QLineEdit::Password);
    passwordOut->setPlaceholderText("Optional");
    startBtnOut = new QPushButton("Start hosting", page);
    startBtnOut->setObjectName("remote.server.start.button");
    startBtnOut->setProperty("accent", true);

    auto* form = new QFormLayout(page);
    form->setContentsMargins(12, 14, 12, 12);
    form->setSpacing(9);
    form->addRow("Name", nameOut);
    form->addRow("Port", portOut);
    form->addRow("Password", passwordOut);
    form->addRow(QString(), startBtnOut);
    return page;
}

// Google Desktop-app OAuth client (JEF-31).
//
// The "secret" is not a secret: RFC 8252 installed apps are PUBLIC clients
// that cannot keep one, and Google issues this value expecting it to ship in
// the binary. PKCE is what actually protects the exchange. Stated plainly here
// so nobody later mistakes it for a credential worth protecting — or worse,
// tries to "fix" it by hiding it somewhere that gives false comfort.
constexpr const char* kGoogleDesktopClientId =
    "424897654904-s5i61ngt4gm2ir8kihqt4d7qcro5g0db.apps.googleusercontent.com";
constexpr const char* kGoogleDesktopClientSecret = "";

/** Seconds as HH:MM:SS — the same unit and format the admin console uses. */
QString formatDurationHMS(long long seconds) {
    const bool negative = seconds < 0;
    long long total = negative ? -seconds : seconds;
    const long long h = total / 3600;
    const long long m = (total % 3600) / 60;
    const long long s = total % 60;
    return QStringLiteral("%1%2:%3:%4")
        .arg(negative ? "-" : "")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

QLabel* sectionLabel(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setProperty("role", "section");
    return l;
}

// Default coordinator URL: env JEFECHECK_COORDINATOR_URL wins, else the
// QSettings "Remote/coordinatorUrl" the last successful connect saved, else "".
QString defaultCoordinatorUrl() {
    const QString env = QProcessEnvironment::systemEnvironment().value(
        "JEFECHECK_COORDINATOR_URL");
    if (!env.isEmpty()) return env;
    return QSettings().value("Remote/coordinatorUrl").toString();
}

// Build the JefeCheck Cloud page. Mirrors makeHostPage deliberately: a short
// form ending in one accent button, so hosting on the cloud reads as the same
// action as hosting on the LAN.
//
// There is NO password row: the coordinator protocol has no password concept
// (session access is the join code plus, for the host, an access token), so a
// Password field would be a control that silently does nothing.
//
// There is no coordinator-URL row either — that moved to Preferences -> Remote.
// The result block below the button stays hidden until a session exists.
QWidget* makeCloudPage(QComboBox*& groupOut, QPushButton*& newGroupOut,
                       QLineEdit*& hostNameOut, QCheckBox*& knockOut,
                       QLineEdit*& passwordOut, QSpinBox*& timeoutOut,
                       QSpinBox*& maxPeersOut, QPushButton*& hostBtnOut,
                       QWidget*& resultBoxOut, QLineEdit*& codeOut,
                       QPushButton*& copyBtnOut, QLabel*& accountOut,
                       QPushButton*& signOutOut) {
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(12, 14, 12, 12);
    v->setSpacing(9);

    // --- Parent group -----------------------------------------------------
    groupOut = new QComboBox(page);
    groupOut->setObjectName("remote.cloud.group");
    groupOut->setToolTip(
        "Sessions are hosted under a group. Each group remembers its own "
        "admission, password, timeout and capacity settings.");
    newGroupOut = new QPushButton("New…", page);
    newGroupOut->setObjectName("remote.cloud.newGroupBtn");
    auto* groupRow = new QHBoxLayout();
    groupRow->setContentsMargins(0, 0, 0, 0);
    groupRow->setSpacing(6);
    groupRow->addWidget(groupOut, /*stretch*/ 1);
    groupRow->addWidget(newGroupOut);

    hostNameOut = new QLineEdit(page);
    hostNameOut->setObjectName("remote.cloud.hostName");
    hostNameOut->setPlaceholderText("Session name");

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(9);
    form->addRow("Group", groupRow);
    form->addRow("Name", hostNameOut);
    v->addLayout(form);

    // --- Host-side settings, saved on the selected group ------------------
    // Every control below belongs to the GROUP, not the machine: a client
    // review wants knocking and a tight cap, internal dailies want neither,
    // and one global set means re-toggling them every time (which in practice
    // means they end up wrong). Joiners never see any of this.
    v->addWidget(sectionLabel("Session settings", page));

    knockOut = new QCheckBox("Ask me before letting each person in", page);
    knockOut->setObjectName("remote.cloud.knock");
    knockOut->setToolTip(
        "Joiners wait in a lobby until you admit them. With this off, anyone "
        "holding the session code joins immediately.");

    passwordOut = new QLineEdit(page);
    passwordOut->setObjectName("remote.cloud.password");
    passwordOut->setEchoMode(QLineEdit::Password);
    passwordOut->setPlaceholderText("Optional");
    passwordOut->setToolTip(
        "Checked in addition to the session code, before anyone reaches your "
        "lobby.");

    timeoutOut = new QSpinBox(page);
    timeoutOut->setObjectName("remote.cloud.idleTimeout");
    timeoutOut->setRange(0, 480);
    timeoutOut->setSuffix(" min");
    timeoutOut->setSpecialValueText("Never");
    timeoutOut->setToolTip(
        "Closes the session — and stops charging credits — after this much "
        "inactivity. Without it, a sleeping laptop can hold a session open and "
        "keep the meter running.");

    maxPeersOut = new QSpinBox(page);
    maxPeersOut->setObjectName("remote.cloud.maxParticipants");
    maxPeersOut->setRange(0, 64);
    maxPeersOut->setSpecialValueText("Unlimited");

    auto* setForm = new QFormLayout();
    setForm->setContentsMargins(0, 0, 0, 0);
    setForm->setSpacing(9);
    setForm->addRow(QString(), knockOut);
    setForm->addRow("Password", passwordOut);
    setForm->addRow("Idle timeout", timeoutOut);
    setForm->addRow("Max people", maxPeersOut);
    v->addLayout(setForm);

    hostBtnOut = new QPushButton("Host on JefeCheck Cloud", page);
    hostBtnOut->setObjectName("remote.cloud.hostBtn");
    hostBtnOut->setProperty("accent", true);
    v->addWidget(hostBtnOut);

    // --- Result block: hidden until hosting -------------------------------
    resultBoxOut = new QWidget(page);
    resultBoxOut->setObjectName("remote.cloud.resultBox");
    auto* rv = new QVBoxLayout(resultBoxOut);
    rv->setContentsMargins(0, 6, 0, 0);
    rv->setSpacing(9);

    auto* divider = new QFrame(resultBoxOut);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet("color:#3a3a40;");
    rv->addWidget(divider);

    codeOut = new QLineEdit(resultBoxOut);
    codeOut->setObjectName("remote.cloud.sessionCode");
    codeOut->setReadOnly(true);
    codeOut->setPlaceholderText("Session code appears here");
    copyBtnOut = new QPushButton("Copy", resultBoxOut);
    copyBtnOut->setObjectName("remote.cloud.copyBtn");
    auto* codeRow = new QHBoxLayout();
    codeRow->setContentsMargins(0, 0, 0, 0);
    codeRow->setSpacing(6);
    codeRow->addWidget(codeOut, /*stretch*/ 1);
    codeRow->addWidget(copyBtnOut);

    accountOut = new QLabel("—", resultBoxOut);
    accountOut->setObjectName("remote.cloud.account");
    accountOut->setTextFormat(Qt::PlainText);
    signOutOut = new QPushButton("Sign out", resultBoxOut);
    signOutOut->setObjectName("remote.cloud.signOutBtn");
    auto* acctRow = new QHBoxLayout();
    acctRow->setContentsMargins(0, 0, 0, 0);
    acctRow->setSpacing(6);
    acctRow->addWidget(accountOut, /*stretch*/ 1);
    acctRow->addWidget(signOutOut);

    // NOTE: credits are deliberately NOT here. They live in the always-visible
    // status header and appear only while hosting -- a joiner has no balance
    // worth showing, since joining is free.
    auto* rform = new QFormLayout();
    rform->setContentsMargins(0, 0, 0, 0);
    rform->setSpacing(9);
    rform->addRow("Session code", codeRow);
    rform->addRow("Signed in as", acctRow);
    rv->addLayout(rform);

    resultBoxOut->setVisible(false);
    v->addWidget(resultBoxOut);
    return page;
}

// Unified Join page: one place for "someone gave me something to join with",
// switching on WHAT they gave you. Joining a cloud session needs no account,
// so this page has no sign-in affordance.
QWidget* makeJoinPage(QLineEdit*& nameOut, QRadioButton*& modeCodeOut,
                      QRadioButton*& modeIpOut, QLineEdit*& codeOut,
                      QWidget*& ipRowsOut, QLineEdit*& ipOut,
                      QSpinBox*& portOut, QLineEdit*& passwordOut,
                      QPushButton*& connectBtnOut) {
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(12, 14, 12, 12);
    v->setSpacing(9);

    nameOut = new QLineEdit(page);
    nameOut->setObjectName("remote.client.name.edit");
    nameOut->setPlaceholderText("Your nickname");
    auto* nameForm = new QFormLayout();
    nameForm->setContentsMargins(0, 0, 0, 0);
    nameForm->setSpacing(9);
    nameForm->addRow("Nickname", nameOut);
    v->addLayout(nameForm);

    modeCodeOut = new QRadioButton("Session code", page);
    modeCodeOut->setObjectName("remote.join.modeCode");
    modeIpOut = new QRadioButton("IP address", page);
    modeIpOut->setObjectName("remote.join.modeIp");
    modeCodeOut->setChecked(true);
    auto* modeRow = new QHBoxLayout();
    modeRow->setContentsMargins(0, 0, 0, 0);
    modeRow->setSpacing(12);
    modeRow->addWidget(modeCodeOut);
    modeRow->addWidget(modeIpOut);
    modeRow->addStretch(1);
    auto* modeForm = new QFormLayout();
    modeForm->setContentsMargins(0, 0, 0, 0);
    modeForm->setSpacing(9);
    modeForm->addRow("Join with", modeRow);
    v->addLayout(modeForm);

    codeOut = new QLineEdit(page);
    codeOut->setObjectName("remote.join.code");
    codeOut->setPlaceholderText("JEFE-XXXX");
    auto* codeForm = new QFormLayout();
    codeForm->setContentsMargins(0, 0, 0, 0);
    codeForm->setSpacing(9);
    codeForm->addRow("Code", codeOut);
    auto* codeWrap = new QWidget(page);
    codeWrap->setLayout(codeForm);
    v->addWidget(codeWrap);

    // IP mode keeps today's fields verbatim.
    ipOut = new QLineEdit(page);
    ipOut->setObjectName("remote.client.ip.edit");
    ipOut->setPlaceholderText("Server IP / hostname");
    portOut = new QSpinBox(page);
    portOut->setObjectName("remote.client.port.spin");
    portOut->setRange(1024, 65535);
    portOut->setValue(60000);
    passwordOut = new QLineEdit(page);
    passwordOut->setObjectName("remote.client.password.edit");
    passwordOut->setEchoMode(QLineEdit::Password);
    passwordOut->setPlaceholderText("Optional");
    auto* ipForm = new QFormLayout();
    ipForm->setContentsMargins(0, 0, 0, 0);
    ipForm->setSpacing(9);
    ipForm->addRow("Server", ipOut);
    ipForm->addRow("Port", portOut);
    ipForm->addRow("Password", passwordOut);
    ipRowsOut = new QWidget(page);
    ipRowsOut->setLayout(ipForm);
    ipRowsOut->setVisible(false);
    v->addWidget(ipRowsOut);

    connectBtnOut = new QPushButton("Join", page);
    connectBtnOut->setObjectName("remote.client.connect.button");
    connectBtnOut->setProperty("accent", true);
    v->addWidget(connectBtnOut);

    // Exactly one set of fields is visible, so the panel sizes to the mode in
    // use rather than reserving space for both.
    QObject::connect(modeCodeOut, &QRadioButton::toggled, page,
                     [codeWrap, ipRowsOut](bool on) {
                         codeWrap->setVisible(on);
                         ipRowsOut->setVisible(!on);
                     });
    return page;
}

}  // namespace

RemoteDialog_Qt::RemoteDialog_Qt(QWidget* parent) : QWidget(parent) {
    setObjectName("panel_remote");
    setStyleSheet(kRemoteStyle);

    // ---- Status header: colored dot + text -------------------------------
    statusDot_ = new QLabel(QStringLiteral("●"), this);  // ●
    statusDot_->setObjectName("remote.status.dot");
    statusLabel_ = new QLabel("Not connected", this);
    statusLabel_->setObjectName("remote.status.label");
    statusLabel_->setProperty("role", "status");
    auto* statusRow = new QHBoxLayout();
    statusRow->setSpacing(8);
    statusRow->addWidget(statusDot_);
    statusRow->addWidget(statusLabel_, /*stretch*/ 1);

    // Credits: always visible WHILE HOSTING, and only then. A joiner never
    // sees a balance — joining is free, so the number would be both
    // meaningless and misleading to them. refreshCreditsVisibility() owns the
    // hiding; this only builds it.
    creditsLabel_ = new QLabel(QString(), this);
    creditsLabel_->setObjectName("remote.status.credits");
    creditsLabel_->setTextFormat(Qt::PlainText);
    creditsLabel_->setToolTip("Credits remaining on this account.");
    creditsLabel_->setVisible(false);
    statusRow->addWidget(creditsLabel_);

    // ---- Connect section (shown when disconnected): Host / Join ----------
    // A segmented toggle that shows exactly one of the two forms, so the panel
    // sizes to the visible form (no dead space; avoids QTabWidget's stacked
    // layout always sizing to the tallest page).
    hostToggle_ = new QPushButton("Host", this);
    cloudToggle_ = new QPushButton("Cloud", this);
    joinToggle_ = new QPushButton("Join", this);
    for (auto* b : {hostToggle_, cloudToggle_, joinToggle_}) {
        b->setCheckable(true);
        b->setProperty("segment", true);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    hostToggle_->setObjectName("remote.toggle.host");
    cloudToggle_->setObjectName("remote.toggle.cloud");
    joinToggle_->setObjectName("remote.toggle.join");
    hostToggle_->setProperty("segpos", "left");
    cloudToggle_->setProperty("segpos", "mid");
    joinToggle_->setProperty("segpos", "right");
    hostToggle_->setChecked(true);
    auto* segRow = new QHBoxLayout();
    segRow->setContentsMargins(0, 0, 0, 0);
    segRow->setSpacing(0);
    segRow->addWidget(hostToggle_);
    segRow->addWidget(cloudToggle_);
    segRow->addWidget(joinToggle_);

    hostForm_ = makeHostPage(serverNameEdit_, serverPortSpin_,
                             serverPasswordEdit_, startServerBtn_);
    cloudForm_ = makeCloudPage(cloudGroupCombo_, cloudNewGroupBtn_,
                               cloudHostNameEdit_, cloudKnockCheck_,
                               cloudPasswordEdit_, cloudTimeoutSpin_,
                               cloudMaxPeersSpin_, cloudHostBtn_,
                               cloudResultBox_, cloudSessionCodeEdit_,
                               cloudCopyBtn_, cloudAccountLabel_,
                               cloudSignOutBtn_);
    joinForm_ = makeJoinPage(clientNameEdit_, joinModeCodeRadio_,
                             joinModeIpRadio_, joinCodeEdit_, joinIpRows_,
                             clientIPEdit_, clientPortSpin_,
                             clientPasswordEdit_, connectClientBtn_);
    clientNameEdit_->setText(QSysInfo::machineHostName());
    for (auto* f : {hostForm_, cloudForm_, joinForm_}) {
        f->setProperty("card", true);
        f->setAttribute(Qt::WA_StyledBackground, true);  // paint QSS bg on plain QWidget
    }
    cloudForm_->setVisible(false);
    joinForm_->setVisible(false);

    connectPanel_ = new QWidget(this);
    connectPanel_->setObjectName("remote.connect.panel");
    auto* connectLayout = new QVBoxLayout(connectPanel_);
    connectLayout->setContentsMargins(0, 0, 0, 0);
    connectLayout->setSpacing(0);
    connectLayout->addLayout(segRow);
    connectLayout->addWidget(hostForm_);
    connectLayout->addWidget(cloudForm_);
    connectLayout->addWidget(joinForm_);

    // 0 = Host, 1 = Cloud, 2 = Join.
    auto selectMode = [this](int mode) {
        hostToggle_->setChecked(mode == 0);
        cloudToggle_->setChecked(mode == 1);
        joinToggle_->setChecked(mode == 2);
        hostForm_->setVisible(mode == 0);
        cloudForm_->setVisible(mode == 1);
        joinForm_->setVisible(mode == 2);
    };
    connect(hostToggle_,  &QPushButton::clicked, this, [selectMode]() { selectMode(0); });
    connect(cloudToggle_, &QPushButton::clicked, this, [selectMode]() { selectMode(1); });
    connect(joinToggle_,  &QPushButton::clicked, this, [selectMode]() { selectMode(2); });

    // ---- Session section (shown when connected) --------------------------
    participantsHeader_ = sectionLabel("Participants", this);
    participantsList_ = new QListWidget(this);
    participantsList_->setObjectName("remote.participants");
    participantsList_->setMaximumHeight(96);

    auto* chatHeader = sectionLabel("Chat", this);
    chatHeader->setObjectName("remote.chat.header");
    chatScroll_ = new QScrollArea(this);
    chatScroll_->setObjectName("remote.chatscroll");
    chatScroll_->setProperty("chatsurface", true);   // styled via [chatsurface]
    chatScroll_->setWidgetResizable(true);
    chatScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chatScroll_->setFrameShape(QFrame::NoFrame);
    chatContent_ = new QWidget(chatScroll_);
    chatContent_->setObjectName("remote.chatcontent");
    chatContent_->setProperty("chatsurface", true);
    chatContent_->setAttribute(Qt::WA_StyledBackground, true);  // paint QSS bg
    chatLayout_ = new QVBoxLayout(chatContent_);
    chatLayout_->setContentsMargins(6, 6, 6, 6);
    chatLayout_->setSpacing(6);
    chatLayout_->addStretch(1);   // keeps bubbles packed to the top
    chatScroll_->setWidget(chatContent_);
    chatLogBox_ = nullptr;   // chat log is now always visible in the session view

    chatInput_ = new QLineEdit(this);
    chatInput_->setObjectName("remote.chatinput");
    chatInput_->setPlaceholderText("Message…");
    chatInput_->setClearButtonEnabled(true);
    // Dedicated Send button next to the input — the native chat pattern, and it
    // makes the send affordance obvious (Enter also sends).
    auto* sendBtn = new QPushButton(jefe::qticons::send(), "Send", this);  // JEF-19
    sendBtn->setObjectName("remote.chat.send");
    sendBtn->setToolTip("Send the message (Enter also sends)");
    sendBtn->setProperty("accent", true);
    auto* chatInputRow = new QHBoxLayout();
    chatInputRow->setContentsMargins(0, 0, 0, 0);
    chatInputRow->setSpacing(6);
    chatInputRow->addWidget(chatInput_, /*stretch*/ 1);
    chatInputRow->addWidget(sendBtn);

    // End/Leave lives in the session header (top), away from the chat input, so
    // it can't be mistaken for a send button. Text is set contextually in
    // refreshConnectionState (host "End Session" vs client "Leave").
    disconnectBtn_ = new QPushButton("Leave", this);
    disconnectBtn_->setObjectName("remote.disconnect.button");
    disconnectBtn_->setProperty("danger", true);
    auto* sessionHeader = new QHBoxLayout();
    sessionHeader->setContentsMargins(0, 0, 0, 0);
    sessionHeader->addWidget(participantsHeader_, /*stretch*/ 1);
    sessionHeader->addWidget(disconnectBtn_);

    errorLabel_ = new QLabel(QString(), this);
    errorLabel_->setObjectName("remote.error");
    errorLabel_->setStyleSheet("color:#e0836c;");
    errorLabel_->setWordWrap(true);

    // Cloud-host code banner: keeps the assigned session code visible (with a
    // Copy button) while connected, so the host can keep inviting peers. Shown
    // only when connected as a cloud host (remoteSessionCode() non-empty).
    cloudCodeBanner_ = new QWidget(this);
    cloudCodeBanner_->setObjectName("remote.cloud.codeBanner");
    cloudCodeBanner_->setProperty("card", true);
    cloudCodeBanner_->setAttribute(Qt::WA_StyledBackground, true);
    cloudCodeBannerLabel_ = new QLabel(QString(), cloudCodeBanner_);
    cloudCodeBannerLabel_->setObjectName("remote.cloud.codeBanner.label");
    cloudCodeBannerLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* bannerCopyBtn = new QPushButton("Copy", cloudCodeBanner_);
    bannerCopyBtn->setObjectName("remote.cloud.codeBanner.copyBtn");
    auto* bannerLay = new QHBoxLayout(cloudCodeBanner_);
    bannerLay->setContentsMargins(10, 6, 8, 6);
    bannerLay->setSpacing(6);
    bannerLay->addWidget(cloudCodeBannerLabel_, /*stretch*/ 1);
    bannerLay->addWidget(bannerCopyBtn);
    cloudCodeBanner_->setVisible(false);
    connect(bannerCopyBtn, &QPushButton::clicked,
            this, &RemoteDialog_Qt::copySessionCodeToClipboard);

    sessionBox_ = new QWidget(this);
    auto* sessionLayout = new QVBoxLayout(sessionBox_);
    sessionLayout->setContentsMargins(0, 0, 0, 0);
    sessionLayout->setSpacing(6);
    sessionLayout->addLayout(sessionHeader);
    sessionLayout->addWidget(cloudCodeBanner_);
    sessionLayout->addWidget(participantsList_);
    sessionLayout->addWidget(chatHeader);
    sessionLayout->addWidget(chatScroll_, /*stretch*/ 1);
    sessionLayout->addLayout(chatInputRow);

    connect(sendBtn, &QPushButton::clicked, this, &RemoteDialog_Qt::onChatSubmit);

    // ---- Connection log (collapsible section, de-emphasized, at bottom) --
    netLogView_ = new QTextEdit(this);
    netLogView_->setObjectName("remote.netlog");
    netLogView_->setReadOnly(true);
    netLogView_->setMinimumHeight(60);
    // No max height — the splitter below lets the user size the log against
    // the chat area.
    netLogView_->setPlaceholderText(
        "Connection activity (nicknames, sync, connect/disconnect) appears here "
        "once you host or join a session.");
    auto* netLogSection = new CollapsibleSection(tr("Connection log"), this);
    netLogSection->setObjectName("remote.netlogsection");
    netLogSection->setContentWidget(netLogView_);
    netLogBox_ = nullptr;   // replaced by the reusable CollapsibleSection

    // Sensible defaults so local testing needs no typing.
    serverNameEdit_->setText("server");
    clientIPEdit_->setText("127.0.0.1");
    clientNameEdit_->setText(QSysInfo::machineHostName());

    // Chat area (sessionBox_) and the connection log share a vertical splitter
    // so the chat fills available space and the user can drag the boundary to
    // grow/shrink the log. The chat gets the stretch; the log stays compact by
    // default but is freely resizable.
    auto* sessionSplitter = new QSplitter(Qt::Vertical, this);
    sessionSplitter->setObjectName("remote.sessionsplitter");
    sessionSplitter->setChildrenCollapsible(false);
    sessionSplitter->addWidget(sessionBox_);
    sessionSplitter->addWidget(netLogSection);
    sessionSplitter->setStretchFactor(0, 1);   // chat grows
    sessionSplitter->setStretchFactor(1, 0);   // log stays compact
    sessionSplitter->setSizes({400, 120});

    // ---- Assemble --------------------------------------------------------
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 14, 14, 14);
    outer->setSpacing(12);
    outer->addLayout(statusRow);
    outer->addWidget(connectPanel_);
    outer->addWidget(errorLabel_);
    outer->addWidget(sessionSplitter, /*stretch*/ 1);   // fills remaining space

    connect(startServerBtn_, &QPushButton::clicked,
            this, &RemoteDialog_Qt::onStartServerClicked);
    // --- Session groups ---------------------------------------------------
    // Populate before connecting, so filling the combo doesn't fire
    // currentTextChanged and write a half-built form over a stored group.
    cloudGroupCombo_->addItems(jefe::qt::sessionGroupNames());
    cloudGroupCombo_->setCurrentText(jefe::qt::activeSessionGroup());
    loadGroupIntoForm(cloudGroupCombo_->currentText());

    connect(cloudGroupCombo_, &QComboBox::currentTextChanged,
            this, [this](const QString& name) { loadGroupIntoForm(name); });
    connect(cloudNewGroupBtn_, &QPushButton::clicked,
            this, &RemoteDialog_Qt::onNewGroupClicked);
    // Write through on every edit: this panel has no OK/Apply, so a deferred
    // save would silently lose settings when the dock is closed.
    connect(cloudKnockCheck_, &QCheckBox::toggled,
            this, [this](bool) { saveFormIntoGroup(); });
    connect(cloudPasswordEdit_, &QLineEdit::editingFinished,
            this, [this]() { saveFormIntoGroup(); });
    connect(cloudTimeoutSpin_, &QSpinBox::valueChanged,
            this, [this](int) { saveFormIntoGroup(); });
    connect(cloudMaxPeersSpin_, &QSpinBox::valueChanged,
            this, [this](int) { saveFormIntoGroup(); });
    connect(cloudHostNameEdit_, &QLineEdit::editingFinished,
            this, [this]() { saveFormIntoGroup(); });

    connect(cloudHostBtn_, &QPushButton::clicked,
            this, &RemoteDialog_Qt::onCreateCloudClicked);
    // One Join button serves both modes; onJoinCloudClicked dispatches on the
    // radio so the LAN path stays exactly as it was.
    connect(connectClientBtn_, &QPushButton::clicked,
            this, &RemoteDialog_Qt::onJoinCloudClicked);
    // Sign out is per-coordinator, matching how tokens are stored: signing out
    // of JefeCheck Cloud leaves a self-hosted coordinator's session alone.
    cloudSignOutBtn_->setEnabled(false);
    connect(cloudSignOutBtn_, &QPushButton::clicked, this, [this]() {
        if (authSession_ != nullptr) authSession_->signOut();
        cloudAccountLabel_->setText(QStringLiteral("—"));
        cloudSignOutBtn_->setEnabled(false);
    });

    connect(cloudCopyBtn_, &QPushButton::clicked,
            this, &RemoteDialog_Qt::copySessionCodeToClipboard);
    connect(disconnectBtn_, &QPushButton::clicked,
            this, &RemoteDialog_Qt::onDisconnectClicked);
    connect(chatInput_, &QLineEdit::returnPressed,
            this, &RemoteDialog_Qt::onChatSubmit);

    // Latch the bridge shutdown flag on app quit so a detached cloud-connect
    // worker still in flight won't touch networkManager as globals tear down.
    connect(qApp, &QCoreApplication::aboutToQuit, this,
            []() { jefe::qt::beginBridgeShutdown(); });

    refreshConnectionState();
}

void RemoteDialog_Qt::onChatSubmit() {
    const QString text = chatInput_->text().trimmed();
    if (text.isEmpty() || !jefe::qt::isRemoteConnected()) return;
    jefe::qt::sendChatMessageText(text.toStdString());
    chatInput_->clear();
    refreshConnectionState();   // reflect the sent line in the chat log
}

void RemoteDialog_Qt::onStartServerClicked() {
    clearUiPreview();   // a real action always wins over --ui-preview
    jefe::qt::RemoteServerParams p;
    p.serverName = serverNameEdit_->text().toStdString();
    p.port       = serverPortSpin_->value();
    p.password   = serverPasswordEdit_->text().toStdString();
    jefe::qt::connectAsServer(p);
    refreshConnectionState();
}

void RemoteDialog_Qt::onConnectClientClicked() {
    clearUiPreview();   // a real action always wins over --ui-preview
    jefe::qt::RemoteClientParams p;
    p.clientName = clientNameEdit_->text().toStdString();
    p.serverIP   = clientIPEdit_->text().toStdString();
    p.port       = clientPortSpin_->value();
    p.password   = clientPasswordEdit_->text().toStdString();
    jefe::qt::connectAsClient(p);
    refreshConnectionState();
}

void RemoteDialog_Qt::onDisconnectClicked() {
    clearUiPreview();   // a real action always wins over --ui-preview
    jefe::qt::disconnectRemote();
    // Clear the last cloud code so a fresh Create shows a fresh code.
    cloudSessionCodeEdit_->clear();
    cloudCopyBtn_->setEnabled(false);
    refreshConnectionState();
}

// --- Cloud (coordinator) mode — JEF-27 --------------------------------------

// Runs the (potentially 5s-blocking) coordinator connect `work` on a detached
// worker thread so the Qt event loop keeps running, then marshals a call to
// onCloudConnectFinished(wasHost) back onto the GUI thread. A QPointer guards
// against the dialog being destroyed mid-connect (no use-after-free): if the
// dialog is gone when the worker finishes, the queued lambda is a no-op.
void RemoteDialog_Qt::launchCloudConnect(bool wasHost, std::function<void()> work) {
    QPointer<RemoteDialog_Qt> guard(this);
    std::thread([guard, wasHost, work = std::move(work)]() {
        work();   // blocks off the GUI thread
        // Marshal back onto the GUI thread. qApp is the context object (it
        // lives on the main thread), so the lambda runs there; the QPointer
        // tells us whether the dialog is still alive.
        QMetaObject::invokeMethod(qApp, [guard, wasHost]() {
            if (guard) guard->onCloudConnectFinished(wasHost);
        }, Qt::QueuedConnection);
    }).detach();
}

// Coordinator URL now lives in Preferences -> Remote; the Cloud tab is about
// hosting, not configuration. Env still wins, matching makeTransport.
QString RemoteDialog_Qt::coordinatorUrlSetting() const {
    const QString env = QProcessEnvironment::systemEnvironment().value(
        "JEFECHECK_COORDINATOR_URL");
    if (!env.isEmpty()) return env;
    return QSettings().value("Remote/coordinatorUrl").toString();
}

jefe::auth::AuthSession* RemoteDialog_Qt::authSession() {
    if (authSession_ != nullptr) return authSession_;

    if (!tokenStore_) tokenStore_ = jefe::auth::makeTokenStore();

    jefe::auth::AuthConfig cfg;
    const QString ws = coordinatorUrlSetting().trimmed();
    // The HTTP API sits alongside the WebSocket coordinator. Derived rather
    // than configured separately so there is one thing to set, and one thing
    // that can be wrong.
    // The HTTP API sits at the same host as the WebSocket coordinator, minus
    // the API Gateway stage suffix: wss://<id>.execute-api…/dev has its REST
    // twin at https://<id2>.execute-api…. Allow an explicit override for
    // deployments where they are not siblings.
    {
        const QString override =
            QSettings().value("Remote/httpApiBase").toString().trimmed();
        if (!override.isEmpty()) {
            cfg.httpBase = override.toStdString();
        } else {
            QString http = ws;
            http.replace(QStringLiteral("wss://"), QStringLiteral("https://"));
            http.replace(QStringLiteral("ws://"), QStringLiteral("http://"));
            const int lastSlash = http.lastIndexOf(QLatin1Char('/'));
            if (lastSlash > http.indexOf(QStringLiteral("//")) + 1) {
                http = http.left(lastSlash);
            }
            cfg.httpBase = http.toStdString();
        }
    }
    cfg.account = ws.toStdString();
    cfg.googleClientId = kGoogleDesktopClientId;
    cfg.googleClientSecret = kGoogleDesktopClientSecret;

    authSession_ = new jefe::auth::AuthSession(cfg, tokenStore_.get(), this);

    connect(authSession_, &jefe::auth::AuthSession::signedIn, this, [this]() {
        refreshAccountRow();
        if (hostRetryPending_) {
            hostRetryPending_ = false;
            hostWithToken();       // the retry the sign-in was for
        }
    });
    connect(authSession_, &jefe::auth::AuthSession::signInFailed, this,
            [this](QString reason) {
                // ONE attempt. A second automatic try would open another
                // browser window at someone who just declined one.
                hostRetryPending_ = false;
                cloudHostBtn_->setEnabled(true);
                cloudHostBtn_->setText("Host on JefeCheck Cloud");
                errorLabel_->setText("Sign-in failed — " + reason);
                statusLabel_->setText("Not connected");
                shownStatusText_.clear();
            });
    connect(authSession_, &jefe::auth::AuthSession::signedOut, this,
            [this]() { refreshAccountRow(); });

    return authSession_;
}

void RemoteDialog_Qt::refreshAccountRow() {
    if (authSession_ == nullptr) return;
    const QString email = authSession_->email();
    cloudAccountLabel_->setText(email.isEmpty() ? QStringLiteral("—") : email);
    cloudSignOutBtn_->setEnabled(!email.isEmpty());

    const long long secs = authSession_->creditBalanceSeconds();
    if (secs >= 0 && creditsLabel_ != nullptr) {
        creditsLabel_->setText(formatDurationHMS(secs) + " credits");
    }
}

void RemoteDialog_Qt::onCreateCloudClicked() {
    clearUiPreview();   // a real action always wins over --ui-preview
    const QString url = coordinatorUrlSetting().trimmed();
    if (url.isEmpty()) {
        errorLabel_->setText(
            "No coordinator configured — set one in Preferences → Remote.");
        return;
    }

    auto* auth = authSession();

    // Already holding a live access token: host straight away.
    if (!auth->accessToken().empty()) {
        hostWithToken();
        return;
    }

    errorLabel_->clear();
    cloudHostBtn_->setEnabled(false);
    hostRetryPending_ = true;

    if (auth->haveStoredToken()) {
        // Silent path: the usual case on every launch after the first. No
        // browser unless the stored token has been rotated away or revoked.
        cloudHostBtn_->setText("Signing in…");
        statusLabel_->setText("Signing in…");
        shownStatusText_.clear();
        auth->refresh();
        return;
    }

    // First run on this machine: the browser opens once.
    cloudHostBtn_->setText("Waiting for browser…");
    statusLabel_->setText("Complete sign-in in your browser…");
    shownStatusText_.clear();
    auth->signIn();
}

void RemoteDialog_Qt::hostWithToken() {
    const QString url = coordinatorUrlSetting().trimmed();

    cloudHostBtn_->setEnabled(false);
    cloudHostBtn_->setText("Creating session…");
    errorLabel_->clear();
    shownStatusText_.clear();   // force the status label to repaint
    statusLabel_->setText("Creating session…");

    // Persist any in-flight edit before hosting, so the session runs under
    // exactly what the form shows.
    saveFormIntoGroup();

    jefe::qt::RemoteCloudHostParams p;
    p.coordinatorUrl = url.toStdString();
    p.hostName       = cloudHostNameEdit_->text().trimmed().toStdString();
    // Host-side policy from the selected group (the "parent"). Joiners cannot
    // set or override any of this.
    p.requireKnock       = cloudKnockCheck_->isChecked();
    p.sessionPassword    = cloudPasswordEdit_->text().toStdString();
    p.idleTimeoutMinutes = cloudTimeoutSpin_->value();
    p.maxParticipants    = cloudMaxPeersSpin_->value();
    // The access token from the signed-in session. Falls back to the env var
    // when empty, which keeps the pre-sign-in workflow (and the two-window
    // test script) working unchanged.
    if (authSession_ != nullptr) {
        p.authToken = authSession_->accessToken();
    }
    // Legacy note kept for the env fallback in makeTransport: the
    // JEFECHECK_COORDINATOR_TOKEN env fallback in makeTransport fills it for
    // now, so a hand-supplied token already works end to end.
    launchCloudConnect(/*wasHost*/ true, [p]() { jefe::qt::connectAsCloudHost(p); });
}

void RemoteDialog_Qt::onJoinCloudClicked() {
    clearUiPreview();   // a real action always wins over --ui-preview
    // The unified Join tab: session code (cloud) or IP address (LAN).
    if (joinModeIpRadio_ != nullptr && joinModeIpRadio_->isChecked()) {
        onConnectClientClicked();
        return;
    }

    const QString url  = coordinatorUrlSetting().trimmed();
    const QString code = joinCodeEdit_->text().trimmed();
    if (url.isEmpty()) {
        errorLabel_->setText(
            "No coordinator configured — set one in Preferences → Remote.");
        return;
    }
    if (code.isEmpty()) {
        errorLabel_->setText("Enter a session code to join.");
        return;
    }

    connectClientBtn_->setEnabled(false);
    connectClientBtn_->setText("Joining…");
    errorLabel_->clear();
    shownStatusText_.clear();
    statusLabel_->setText("Waiting for the host to admit you…");

    jefe::qt::RemoteCloudJoinParams p;
    p.clientName     = clientNameEdit_->text().toStdString();
    p.coordinatorUrl = url.toStdString();
    p.sessionCode    = code.toStdString();
    launchCloudConnect(/*wasHost*/ false, [p]() { jefe::qt::connectAsCloudClient(p); });
}

void RemoteDialog_Qt::onCloudConnectFinished(bool wasHost) {
    // Restore the buttons regardless of outcome.
    cloudHostBtn_->setEnabled(true);
    cloudHostBtn_->setText("Host on JefeCheck Cloud");
    connectClientBtn_->setEnabled(true);
    connectClientBtn_->setText("Join");

    QString failMsg;
    if (wasHost) {
        const QString code = QString::fromStdString(jefe::qt::remoteSessionCode());
        if (!code.isEmpty()) {
            cloudSessionCodeEdit_->setText(code);
            cloudCopyBtn_->setEnabled(true);
            cloudResultBox_->setVisible(true);
        } else {
            failMsg = "The coordinator did not assign a session code (timed out). "
                      "Check the coordinator URL in Preferences → Remote and try again.";
        }
    }
    shownStatusText_.clear();   // status text may not have changed string-wise
    // refreshConnectionState() rewrites errorLabel_ from remoteErrors(), so set
    // our own failure message AFTER it (only when the backend surfaced none).
    refreshConnectionState();
    if (!failMsg.isEmpty() && errorLabel_->text().isEmpty())
        errorLabel_->setText(failMsg);
}

void RemoteDialog_Qt::copySessionCodeToClipboard() {
    const QString code = QString::fromStdString(jefe::qt::remoteSessionCode());
    if (!code.isEmpty()) QApplication::clipboard()->setText(code);
}

void RemoteDialog_Qt::refreshConnectionState() {
    // --ui-preview paints a state nothing is actually in. This runs on a timer,
    // so without this guard it would hide the session view — and with it the
    // pending-admission rows and the credits — on the next tick, leaving only
    // the Cloud form visible.
    // ONE state, sampled once. Every widget below is a function of `st` and
    // nothing else — no second look at getConnected/getIsServer/the code,
    // which is how the panel previously ended up rendering a session that did
    // not exist.
    const jefe::qt::RemoteUiState st = currentUiState();
    using Phase = jefe::qt::RemotePhase;

    const QString statusText = QString::fromStdString(st.statusText);
    // Only touch the status label / dot when the text actually changed —
    // setStyleSheet forces a re-polish, and this runs at up to tick rate.
    if (statusText != shownStatusText_) {
        shownStatusText_ = statusText;
        statusLabel_->setText(statusText);
        QString dotColor = "#6a6a70";                       // offline
        if (st.inSession)                    dotColor = "#5bb07a";  // green
        else if (st.phase == Phase::Connecting) dotColor = "#d6a15b"; // amber
        statusDot_->setStyleSheet("color:" + dotColor + "; font-size: 13px;");
    }

    // Contextual sections: forms when there is no session, the session view
    // when there is. setVisible / setText are no-ops when unchanged.
    connectPanel_->setVisible(!st.inSession);
    sessionBox_->setVisible(st.inSession);
    // Host ends the session for everyone; a joiner just leaves it.
    disconnectBtn_->setText(st.isHost ? "End Session" : "Leave");

    const bool cloudHosting = st.phase == Phase::HostingCloud;
    cloudCodeBanner_->setVisible(cloudHosting);
    if (cloudHosting) {
        cloudCodeBannerLabel_->setText(
            QStringLiteral("Session code: <b>%1</b>")
                .arg(QString::fromStdString(st.sessionCode).toHtmlEscaped()));
        cloudSessionCodeEdit_->setText(QString::fromStdString(st.sessionCode));
        cloudResultBox_->setVisible(true);
        cloudCopyBtn_->setEnabled(true);
    }

    // Credits are one field of the state now, not a separately-derived guess.
    refreshCreditsVisibility(st.showCredits);

    // Participants change only on join/leave (which changes the count), so
    // rebuild the ROW WIDGETS only when the count moved — not on every packet.
    // The health fields inside each row (rtt/kbps/path/dot) are refreshed
    // every tick below via updateParticipantHealthRow (cheap setText, no
    // relayout).
    const auto participants = jefe::qt::remoteParticipants();
    if ((int)participants.size() != shownParticipants_) {
        shownParticipants_ = (int)participants.size();
        participantsList_->clear();
        for (const auto& name : participants) {
            const QString qname = QString::fromStdString(name);
            auto* item = new QListWidgetItem(participantsList_);
            auto* row = buildParticipantHealthRow(qname);
            item->setSizeHint(row->sizeHint());
            participantsList_->setItemWidget(item, row);
        }
        participantsHeader_->setText(
            QString("Participants (%1)").arg(participantsList_->count()));
    }

    // JEF-30: per-peer session health. Drop stale samples on disconnect (a
    // later reconnect starts clean, so kbps doesn't spike from a huge gap or
    // a peer-name reused across sessions with a stale byte total).
    if (!st.inSession) {
        peerHealthSamples_.clear();
    } else if (!participants.empty()) {
        // Build a name -> stat lookup once, then update each visible row.
        // remotePeerStats() honors gCloudConnectInFlight itself (empty
        // during a cloud connect) — that degrades gracefully to hasStats=false
        // below, same as any other name that doesn't resolve to a peer stat.
        const auto stats = jefe::qt::remotePeerStats();
        std::unordered_map<std::string, const jefe::qt::RemotePeerStat*> byName;
        byName.reserve(stats.size());
        for (const auto& s : stats) byName[s.name] = &s;

        const auto now = std::chrono::steady_clock::now();
        for (int i = 0; i < participantsList_->count(); ++i) {
            auto* item = participantsList_->item(i);
            auto* row = participantsList_->itemWidget(item);
            if (!row) continue;
            const std::string name = participants[static_cast<size_t>(i)];
            auto it = byName.find(name);
            if (it == byName.end()) {
                updateParticipantHealthRow(row, /*hasStats=*/false, false, -1, -1.0, {});
                continue;
            }
            const jefe::qt::RemotePeerStat& s = *it->second;

            // kbps from a byte-delta sample, keyed by name. Guard div-by-zero
            // and a too-short interval (noisy at up to ~60Hz refresh) by only
            // recomputing once at least 150ms have elapsed since the last
            // sample; otherwise keep showing the last stable value so the
            // row doesn't flicker to "—" between recomputes.
            auto& sample = peerHealthSamples_[name];
            double kbps = sample.lastKbps;
            if (!sample.hasSample) {
                sample.bytes = s.bytes;
                sample.ts = now;
                sample.hasSample = true;
                kbps = -1.0;   // first sample: no interval to derive a rate from yet
            } else {
                const double dtSec = std::chrono::duration<double>(now - sample.ts).count();
                if (dtSec >= 0.15) {
                    long long deltaBytes =
                        static_cast<long long>(s.bytes) - static_cast<long long>(sample.bytes);
                    if (deltaBytes < 0) deltaBytes = 0;   // counter reset guard (reconnect)
                    kbps = (static_cast<double>(deltaBytes) * 8.0) / 1000.0 / dtSec;
                    sample.bytes = s.bytes;
                    sample.ts = now;
                    sample.lastKbps = kbps;
                }
            }

            updateParticipantHealthRow(row, /*hasStats=*/true, s.connected, s.rttMs, kbps,
                                       QString::fromStdString(s.path));
        }
    }

    const auto errs = jefe::qt::remoteErrors();
    errorLabel_->setText(errs.empty() ? QString()
                                      : QString::fromStdString(errs.back()));

    // Chat: append new bubbles incrementally (preserve scroll unless at bottom);
    // full rebuild if the log shrank (reconnect reset).
    {
        const auto entries = jefe::qt::remoteChatEntries();
        const int total = (int)entries.size();
        if (total < shownChatLines_) {   // log reset (reconnect): rebuild
            QLayoutItem* it;
            while ((it = chatLayout_->takeAt(0)) != nullptr) {
                if (it->widget()) it->widget()->deleteLater();
                delete it;
            }
            chatLayout_->addStretch(1);
            shownChatLines_ = 0;
        }
        auto* bar = chatScroll_->verticalScrollBar();
        const bool atBottom = bar->value() >= bar->maximum() - 4;
        const int appended = total - shownChatLines_;
        for (int i = shownChatLines_; i < total; ++i)
            appendChatBubble(entries[i]);
        shownChatLines_ = total;
        // Only when new bubbles actually arrived AND the user was at the bottom.
        // refreshConnectionState() runs at up to ~60Hz during a session, so an
        // unconditional post here would queue a no-op every tick while idle.
        if (appended > 0 && atBottom) {
            // Defer so the layout has sized the new bubbles before we scroll.
            QMetaObject::invokeMethod(this, [bar]{ bar->setValue(bar->maximum()); },
                                      Qt::QueuedConnection);
        }
    }

    // Network log is append-only text — append just the new lines.
    appendNewLogLines(netLogView_, jefe::qt::remoteNetworkLog(), shownNetLogLines_);
}

// Appends only lines beyond `shownCount` to `view`, updating `shownCount`.
// If the source shrank below what's shown (e.g. a reconnect cleared the log),
// clears and rebuilds once so the view can't drift out of sync.
void RemoteDialog_Qt::appendNewLogLines(QTextEdit* view,
                                        const std::vector<std::string>& lines,
                                        int& shownCount) {
    const int total = (int)lines.size();
    if (total < shownCount) {         // source reset — rebuild from scratch
        view->clear();
        shownCount = 0;
    }
    for (int i = shownCount; i < total; ++i)
        view->append(QString::fromStdString(lines[i]));
    shownCount = total;
}

namespace {
// packed = (r<<24)|(g<<16)|(b<<8); 0 = unset -> neutral gray.
QString colorToHex(int packed) {
    if (packed == 0) return QStringLiteral("#9a9a9a");
    int r = (packed >> 24) & 0xff, g = (packed >> 16) & 0xff, b = (packed >> 8) & 0xff;
    return QString::asprintf("#%02x%02x%02x", r, g, b);
}
}  // namespace

void RemoteDialog_Qt::appendChatBubble(const jefe::qt::ChatEntry& e) {
    // System / load messages: centered dim line, no bubble.
    if (e.type != 0 /* GFCNETMESSAGETYPE_NORMAL */) {
        auto* sys = new QLabel(QString::fromStdString(e.message), chatContent_);
        sys->setObjectName("chat_system");
        sys->setTextFormat(Qt::PlainText);   // never interpret message as markup
        sys->setAlignment(Qt::AlignHCenter);
        sys->setWordWrap(true);
        chatLayout_->insertWidget(chatLayout_->count() - 1, sys);  // before stretch
        return;
    }

    auto* row = new QWidget(chatContent_);
    auto* rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(0, 0, 0, 0);

    auto* bubble = new QFrame(row);
    bubble->setObjectName("chat_bubble");
    bubble->setProperty("self", e.isSelf);
    bubble->setMaximumWidth(320);
    auto* bLay = new QVBoxLayout(bubble);
    bLay->setContentsMargins(9, 5, 9, 6);
    bLay->setSpacing(1);

    const QString name = e.isSelf ? QStringLiteral("You")
                                  : QString::fromStdString(e.sender);
    auto* header = new QLabel(bubble);
    header->setObjectName("chat_header");
    header->setText(QString("<span style='color:%1'>%2</span> · %3")
                        .arg(colorToHex(e.color), name.toHtmlEscaped(),
                             QString::fromStdString(e.timeHHMM)));
    auto* msg = new QLabel(QString::fromStdString(e.message), bubble);
    msg->setObjectName("chat_message");
    msg->setTextFormat(Qt::PlainText);   // never interpret message as markup
    msg->setWordWrap(true);
    msg->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bLay->addWidget(header);
    bLay->addWidget(msg);

    if (e.isSelf) { rowLay->addStretch(1); rowLay->addWidget(bubble); }
    else          { rowLay->addWidget(bubble); rowLay->addStretch(1); }

    chatLayout_->insertWidget(chatLayout_->count() - 1, row);  // before stretch
}

// JEF-30: builds one participantsList_ row widget: a colored health dot,
// the participant name, and rtt/kbps/path fields (updated in place every
// refresh by updateParticipantHealthRow — the row itself is only rebuilt
// when the participant COUNT changes, matching the existing incremental-
// refresh discipline for this dialog). Object names use the dotted-leaf
// scheme; reused across rows like the chat_* widgets above (this dialog has
// no per-row automated locator today — see tests/ui/jefecheck/locators.py).
QWidget* RemoteDialog_Qt::buildParticipantHealthRow(const QString& name) {
    auto* row = new QWidget();
    row->setObjectName("remote.health.participantRow");
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setSpacing(6);

    auto* dot = new QLabel(row);
    dot->setObjectName("remote.health.dot");
    dot->setFixedSize(9, 9);
    dot->setStyleSheet(QString("border-radius:4px; background:%1;").arg(kHealthGray));

    auto* nameLbl = new QLabel(name, row);
    nameLbl->setObjectName("remote.health.name");

    auto* rttLbl = new QLabel(row);
    rttLbl->setObjectName("remote.health.rtt");
    rttLbl->setMinimumWidth(46);

    auto* kbpsLbl = new QLabel(row);
    kbpsLbl->setObjectName("remote.health.kbps");
    kbpsLbl->setMinimumWidth(72);

    auto* pathLbl = new QLabel(row);
    pathLbl->setObjectName("remote.health.path");

    lay->addWidget(dot);
    lay->addWidget(nameLbl);
    lay->addStretch(1);
    lay->addWidget(rttLbl);
    lay->addWidget(kbpsLbl);
    lay->addWidget(pathLbl);
    return row;
}

// JEF-30: refreshes one row's dot/rtt/kbps/path text+color in place (cheap
// QLabel::setText/setStyleSheet — no layout rebuild). `hasStats` false means
// this participant has no matching remotePeerStats() entry (self, or a name
// that doesn't resolve — client-role nickname resolution is a known gap,
// see JEF-30 Task 1 report); render as a plain gray "connected"/blank row,
// never fabricate numbers.
void RemoteDialog_Qt::updateParticipantHealthRow(QWidget* row, bool hasStats,
                                                  bool connected, long rttMs,
                                                  double kbps, const QString& path) {
    auto* dot    = row->findChild<QLabel*>("remote.health.dot");
    auto* rttLbl = row->findChild<QLabel*>("remote.health.rtt");
    auto* kbpsLbl= row->findChild<QLabel*>("remote.health.kbps");
    auto* pathLbl= row->findChild<QLabel*>("remote.health.path");
    if (!dot || !rttLbl || !kbpsLbl || !pathLbl) return;

    if (!hasStats) {
        dot->setStyleSheet(QString("border-radius:4px; background:%1;").arg(kHealthGray));
        rttLbl->clear();
        kbpsLbl->clear();
        pathLbl->clear();
        return;
    }

    // Path badge + dot color (§3): direct=green, relay=amber, n/a/unknown=gray.
    // RakNet ("n/a") and any not-yet-connected peer show "connected"/"—"
    // without fabricated WebRTC numbers.
    const bool isDirect = (path == "direct");
    const bool isRelay  = (path == "relay" || path == "relay (TURN)");
    const QString dotColor = isDirect ? kHealthGood : isRelay ? kHealthWarn : kHealthGray;
    dot->setStyleSheet(QString("border-radius:4px; background:%1;").arg(dotColor));

    if (!isDirect && !isRelay) {
        // "n/a" (RakNet) or an unresolved path: no WebRTC stats to show.
        rttLbl->clear();
        kbpsLbl->clear();
        pathLbl->setText(connected ? "connected" : "—");
        pathLbl->setStyleSheet(QString("color:%1;").arg(kHealthGray));
        return;
    }

    // RTT color (§3): green <100ms, amber >=100ms, gray if unknown (<0).
    QString rttColor = kHealthGray;
    if (rttMs >= 0) rttColor = (rttMs < 100) ? kHealthGood : kHealthWarn;
    rttLbl->setText(rttMs >= 0 ? QString("%1ms").arg(rttMs) : QStringLiteral("—"));
    rttLbl->setStyleSheet(QString("color:%1;").arg(rttColor));

    kbpsLbl->setText(kbps >= 0.0 ? QString("%1 kbps").arg(kbps, 0, 'f', 0)
                                 : QStringLiteral("—"));

    pathLbl->setText(isRelay ? "relay (TURN)" : "direct");
    pathLbl->setStyleSheet(QString("color:%1;").arg(dotColor));
}

// ---------------------------------------------------------------------------
// Session groups (the "parent" a session is hosted under).
//
// Settings persist PER GROUP rather than globally, because they describe how a
// kind of session is run, not how this machine is configured. Every edit writes
// through immediately — there is no OK/Apply on this panel, so a deferred save
// would silently lose settings when the dock is closed.
// ---------------------------------------------------------------------------
void RemoteDialog_Qt::loadGroupIntoForm(const QString& name) {
    const jefe::qt::SessionGroup g = jefe::qt::loadSessionGroup(name);
    // Block signals: setting these programmatically would otherwise re-enter
    // saveFormIntoGroup() and write the group back mid-load.
    const QSignalBlocker b1(cloudKnockCheck_);
    const QSignalBlocker b2(cloudPasswordEdit_);
    const QSignalBlocker b3(cloudTimeoutSpin_);
    const QSignalBlocker b4(cloudMaxPeersSpin_);
    const QSignalBlocker b5(cloudHostNameEdit_);
    cloudKnockCheck_->setChecked(g.requireKnock);
    cloudPasswordEdit_->setText(g.password);
    cloudTimeoutSpin_->setValue(g.idleTimeoutMinutes);
    cloudMaxPeersSpin_->setValue(g.maxParticipants);
    if (!g.defaultSessionName.isEmpty())
        cloudHostNameEdit_->setText(g.defaultSessionName);
    jefe::qt::setActiveSessionGroup(name);
}

void RemoteDialog_Qt::saveFormIntoGroup() {
    if (cloudGroupCombo_ == nullptr) return;
    const QString name = cloudGroupCombo_->currentText();
    if (name.isEmpty()) return;
    jefe::qt::SessionGroup g = jefe::qt::loadSessionGroup(name);
    g.name = name;
    g.requireKnock = cloudKnockCheck_->isChecked();
    g.password = cloudPasswordEdit_->text();
    g.idleTimeoutMinutes = cloudTimeoutSpin_->value();
    g.maxParticipants = cloudMaxPeersSpin_->value();
    g.defaultSessionName = cloudHostNameEdit_->text().trimmed();
    jefe::qt::saveSessionGroup(g);
}

void RemoteDialog_Qt::onNewGroupClicked() {
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("New session group"),
        tr("Name (e.g. \"Client review\", \"Internal dailies\")"),
        QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (cloudGroupCombo_->findText(name) >= 0) {
        cloudGroupCombo_->setCurrentText(name);
        return;
    }
    // A new group starts from the CURRENT form, not from defaults: the common
    // case is "like this one, but for a different client".
    jefe::qt::SessionGroup g;
    g.name = name;
    g.requireKnock = cloudKnockCheck_->isChecked();
    g.password = cloudPasswordEdit_->text();
    g.idleTimeoutMinutes = cloudTimeoutSpin_->value();
    g.maxParticipants = cloudMaxPeersSpin_->value();
    jefe::qt::saveSessionGroup(g);
    cloudGroupCombo_->addItem(name);
    cloudGroupCombo_->setCurrentText(name);
}

jefe::qt::RemoteUiState RemoteDialog_Qt::currentUiState() const {
    // The override exists so --ui-preview can paint a state nothing is in,
    // WITHOUT stopping the render loop. There is exactly one painting path.
    return uiPreviewActive_ ? previewState_ : jefe::qt::remoteUiState();
}

void RemoteDialog_Qt::clearUiPreview() {
    if (!uiPreviewActive_) return;
    uiPreviewActive_ = false;
    // Drop the sample pending rows; the real ones come from the coordinator.
    if (participantsList_ != nullptr) participantsList_->clear();
    shownParticipants_ = -1;    // -1 forces a full rebuild from real state
    shownStatusText_.clear();   // force the status label to repaint
}

void RemoteDialog_Qt::refreshCreditsVisibility(bool hosting) {
    if (creditsLabel_ == nullptr) return;
    // Host-only AND hosting-only. A remote participant never sees a balance.
    creditsLabel_->setVisible(hosting);
}

// ---------------------------------------------------------------------------
// --ui-preview (JEF-31/37 design review)
//
// Fills the Cloud result block and the participants list with sample data so
// the layouts can be judged without a coordinator, an account, or a session.
// Nothing here is wired: the buttons do exactly what they do today.
// ---------------------------------------------------------------------------
void RemoteDialog_Qt::applyUiPreview() {
    // Describe the state ONCE, then let the normal render path paint it. The
    // refresh timer keeps running: it simply reads this instead of the live
    // managers, so preview and reality cannot drift apart.
    previewState_ = jefe::qt::RemoteUiState{};
    previewState_.phase = jefe::qt::RemotePhase::HostingCloud;
    previewState_.statusText = "PREVIEW — sample data (not a real session)";
    previewState_.sessionCode = "JEFE-6ZDN";
    previewState_.inSession = true;
    previewState_.isHost = true;
    previewState_.showCredits = true;
    uiPreviewActive_ = true;

    // Cloud tab, hosting state.
    cloudToggle_->setChecked(true);
    hostToggle_->setChecked(false);
    joinToggle_->setChecked(false);
    hostForm_->setVisible(false);
    cloudForm_->setVisible(true);
    joinForm_->setVisible(false);

    // Two groups, so the "parent" relationship is visible rather than implied
    // by a single-item combo.
    if (cloudGroupCombo_->findText("Client review") < 0)
        cloudGroupCombo_->addItem("Client review");
    if (cloudGroupCombo_->findText("Internal dailies") < 0)
        cloudGroupCombo_->addItem("Internal dailies");
    cloudGroupCombo_->setCurrentText("Client review");

    cloudHostNameEdit_->setText("Reel 3 grade review");
    cloudKnockCheck_->setChecked(true);
    cloudTimeoutSpin_->setValue(30);
    cloudMaxPeersSpin_->setValue(6);
    cloudSessionCodeEdit_->setText("JEFE-6ZDN");
    cloudCopyBtn_->setEnabled(true);
    cloudAccountLabel_->setText("gollas@gmail.com");
    cloudResultBox_->setVisible(true);

    // Credits in the header, host-only. Same HH:MM:SS as the admin console,
    // and the same unit credits are stored and charged in.
    refreshCreditsVisibility(/*hosting*/ true);
    creditsLabel_->setText("00:59:57 credits");

    // Pending-admission rows (JEF-37). A verified joiner presented a valid
    // token; an unverified one only chose a nickname. displayName is rendered
    // as PlainText because it is peer-supplied.
    struct Knock { const char* name; const char* email; bool verified; };
    const Knock knocks[] = {
        { "Alice Rivera", "alice@studio.com", true },
        { "someone",      nullptr,            false },
    };
    for (const Knock& k : knocks) {
        auto* row = new QWidget();
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(6, 4, 6, 4);
        h->setSpacing(8);

        auto* dot = new QLabel(QStringLiteral("●"), row);
        dot->setStyleSheet("color:#e0a33e;");   // amber: awaiting a decision
        auto* name = new QLabel(QString::fromUtf8(k.name), row);
        name->setTextFormat(Qt::PlainText);
        auto* who = new QLabel(
            k.verified ? QString::fromUtf8(k.email) + QStringLiteral(" ✓")
                       : QStringLiteral("(not signed in)"),
            row);
        who->setTextFormat(Qt::PlainText);
        who->setStyleSheet(k.verified ? "color:#7fb069;" : "color:#8a8a90;");
        auto* admit = new QPushButton("Admit", row);
        admit->setProperty("accent", true);
        auto* deny = new QPushButton("Deny", row);

        h->addWidget(dot);
        h->addWidget(name);
        h->addWidget(who, /*stretch*/ 1);
        h->addWidget(admit);
        h->addWidget(deny);

        auto* item = new QListWidgetItem(participantsList_);
        item->setSizeHint(row->sizeHint());
        participantsList_->setItemWidget(item, row);
    }

    // Show the session view so the participants list is visible at all.
    if (sessionBox_) sessionBox_->setVisible(true);
    if (connectPanel_) connectPanel_->setVisible(true);
    statusLabel_->setText("Hosting on JefeCheck Cloud — 2 waiting to join");
    statusDot_->setStyleSheet("color:#7fb069;");
    participantsHeader_->setText("Participants");
}

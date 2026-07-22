#include "RemotePanel_qt.h"
#include "qticons.h"
#include "CollapsibleSection_qt.h"
#include "SequenceLoadBridge_qt.h"

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
#include <QSplitter>
#include <QSysInfo>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include <thread>

namespace {

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

QWidget* makeJoinPage(QLineEdit*& nameOut, QLineEdit*& ipOut, QSpinBox*& portOut,
                      QLineEdit*& passwordOut, QPushButton*& connectBtnOut) {
    auto* page = new QWidget();
    nameOut = new QLineEdit(page);
    nameOut->setObjectName("remote.client.name.edit");
    nameOut->setPlaceholderText("Your nickname");
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
    connectBtnOut = new QPushButton("Connect", page);
    connectBtnOut->setObjectName("remote.client.connect.button");
    connectBtnOut->setProperty("accent", true);

    auto* form = new QFormLayout(page);
    form->setContentsMargins(12, 14, 12, 12);
    form->setSpacing(9);
    form->addRow("Nickname", nameOut);
    form->addRow("Server", ipOut);
    form->addRow("Port", portOut);
    form->addRow("Password", passwordOut);
    form->addRow(QString(), connectBtnOut);
    return page;
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

// Build the Cloud (coordinator) form: a shared coordinator URL, a "Create
// session" area (button + read-only assigned code + Copy), and a "Join by code"
// area (code + nickname + Join). Exposes its fields via the out-params.
QWidget* makeCloudPage(QLineEdit*& coordUrlOut, QPushButton*& createBtnOut,
                       QLineEdit*& codeOut, QPushButton*& copyBtnOut,
                       QLineEdit*& joinCodeOut, QLineEdit*& joinNameOut,
                       QPushButton*& joinBtnOut) {
    auto* page = new QWidget();
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(12, 14, 12, 12);
    v->setSpacing(9);

    coordUrlOut = new QLineEdit(page);
    coordUrlOut->setObjectName("remote.cloud.coordinatorUrl");
    coordUrlOut->setPlaceholderText("wss://coordinator.example/…");
    coordUrlOut->setText(defaultCoordinatorUrl());
    auto* urlForm = new QFormLayout();
    urlForm->setContentsMargins(0, 0, 0, 0);
    urlForm->setSpacing(9);
    urlForm->addRow("Coordinator", coordUrlOut);
    v->addLayout(urlForm);

    // --- Create -----------------------------------------------------------
    v->addWidget(sectionLabel("Create a session", page));
    createBtnOut = new QPushButton("Create session", page);
    createBtnOut->setObjectName("remote.cloud.createBtn");
    createBtnOut->setProperty("accent", true);
    v->addWidget(createBtnOut);

    codeOut = new QLineEdit(page);
    codeOut->setObjectName("remote.cloud.sessionCode");
    codeOut->setReadOnly(true);
    codeOut->setPlaceholderText("Session code appears here");
    copyBtnOut = new QPushButton("Copy", page);
    copyBtnOut->setObjectName("remote.cloud.copyBtn");
    copyBtnOut->setEnabled(false);
    auto* codeRow = new QHBoxLayout();
    codeRow->setContentsMargins(0, 0, 0, 0);
    codeRow->setSpacing(6);
    codeRow->addWidget(codeOut, /*stretch*/ 1);
    codeRow->addWidget(copyBtnOut);
    v->addLayout(codeRow);

    // --- Divider ----------------------------------------------------------
    auto* divider = new QFrame(page);
    divider->setFrameShape(QFrame::HLine);
    divider->setStyleSheet("color:#3a3a40;");
    v->addWidget(divider);

    // --- Join -------------------------------------------------------------
    v->addWidget(sectionLabel("Join by code", page));
    joinCodeOut = new QLineEdit(page);
    joinCodeOut->setObjectName("remote.cloud.joinCodeEdit");
    joinCodeOut->setPlaceholderText("Session code");
    joinNameOut = new QLineEdit(page);
    joinNameOut->setObjectName("remote.cloud.joinName");
    joinNameOut->setPlaceholderText("Your nickname");
    joinBtnOut = new QPushButton("Join by code", page);
    joinBtnOut->setObjectName("remote.cloud.joinBtn");
    joinBtnOut->setProperty("accent", true);
    auto* joinForm = new QFormLayout();
    joinForm->setContentsMargins(0, 0, 0, 0);
    joinForm->setSpacing(9);
    joinForm->addRow("Code", joinCodeOut);
    joinForm->addRow("Nickname", joinNameOut);
    joinForm->addRow(QString(), joinBtnOut);
    v->addLayout(joinForm);
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
    cloudForm_ = makeCloudPage(cloudCoordUrlEdit_, cloudCreateBtn_,
                               cloudSessionCodeEdit_, cloudCopyBtn_,
                               cloudJoinCodeEdit_, cloudJoinNameEdit_,
                               cloudJoinBtn_);
    joinForm_ = makeJoinPage(clientNameEdit_, clientIPEdit_, clientPortSpin_,
                             clientPasswordEdit_, connectClientBtn_);
    cloudJoinNameEdit_->setText(QSysInfo::machineHostName());
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
    connect(connectClientBtn_, &QPushButton::clicked,
            this, &RemoteDialog_Qt::onConnectClientClicked);
    connect(cloudCreateBtn_, &QPushButton::clicked,
            this, &RemoteDialog_Qt::onCreateCloudClicked);
    connect(cloudJoinBtn_, &QPushButton::clicked,
            this, &RemoteDialog_Qt::onJoinCloudClicked);
    connect(cloudCopyBtn_, &QPushButton::clicked,
            this, &RemoteDialog_Qt::copySessionCodeToClipboard);
    connect(disconnectBtn_, &QPushButton::clicked,
            this, &RemoteDialog_Qt::onDisconnectClicked);
    connect(chatInput_, &QLineEdit::returnPressed,
            this, &RemoteDialog_Qt::onChatSubmit);

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
    jefe::qt::RemoteServerParams p;
    p.serverName = serverNameEdit_->text().toStdString();
    p.port       = serverPortSpin_->value();
    p.password   = serverPasswordEdit_->text().toStdString();
    jefe::qt::connectAsServer(p);
    refreshConnectionState();
}

void RemoteDialog_Qt::onConnectClientClicked() {
    jefe::qt::RemoteClientParams p;
    p.clientName = clientNameEdit_->text().toStdString();
    p.serverIP   = clientIPEdit_->text().toStdString();
    p.port       = clientPortSpin_->value();
    p.password   = clientPasswordEdit_->text().toStdString();
    jefe::qt::connectAsClient(p);
    refreshConnectionState();
}

void RemoteDialog_Qt::onDisconnectClicked() {
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

void RemoteDialog_Qt::onCreateCloudClicked() {
    const QString url = cloudCoordUrlEdit_->text().trimmed();
    if (url.isEmpty()) {
        errorLabel_->setText("Enter a coordinator URL to create a session.");
        return;
    }
    QSettings().setValue("Remote/coordinatorUrl", url);

    // Disable both actions + show progress; the worker re-enables on finish.
    cloudCreateBtn_->setEnabled(false);
    cloudJoinBtn_->setEnabled(false);
    cloudCreateBtn_->setText("Creating session…");
    errorLabel_->clear();
    shownStatusText_.clear();   // force the status label to repaint
    statusLabel_->setText("Creating session…");

    jefe::qt::RemoteCloudHostParams p;
    p.coordinatorUrl = url.toStdString();
    launchCloudConnect(/*wasHost*/ true, [p]() { jefe::qt::connectAsCloudHost(p); });
}

void RemoteDialog_Qt::onJoinCloudClicked() {
    const QString url  = cloudCoordUrlEdit_->text().trimmed();
    const QString code = cloudJoinCodeEdit_->text().trimmed();
    if (url.isEmpty()) {
        errorLabel_->setText("Enter a coordinator URL to join.");
        return;
    }
    if (code.isEmpty()) {
        errorLabel_->setText("Enter a session code to join.");
        return;
    }
    QSettings().setValue("Remote/coordinatorUrl", url);

    cloudCreateBtn_->setEnabled(false);
    cloudJoinBtn_->setEnabled(false);
    cloudJoinBtn_->setText("Joining…");
    errorLabel_->clear();
    shownStatusText_.clear();
    statusLabel_->setText("Joining session…");

    jefe::qt::RemoteCloudJoinParams p;
    p.clientName     = cloudJoinNameEdit_->text().toStdString();
    p.coordinatorUrl = url.toStdString();
    p.sessionCode    = code.toStdString();
    launchCloudConnect(/*wasHost*/ false, [p]() { jefe::qt::connectAsCloudClient(p); });
}

void RemoteDialog_Qt::onCloudConnectFinished(bool wasHost) {
    // Restore the buttons regardless of outcome.
    cloudCreateBtn_->setEnabled(true);
    cloudJoinBtn_->setEnabled(true);
    cloudCreateBtn_->setText("Create session");
    cloudJoinBtn_->setText("Join by code");

    QString failMsg;
    if (wasHost) {
        const QString code = QString::fromStdString(jefe::qt::remoteSessionCode());
        if (!code.isEmpty()) {
            cloudSessionCodeEdit_->setText(code);
            cloudCopyBtn_->setEnabled(true);
        } else {
            failMsg = "The coordinator did not assign a session code (timed out). "
                      "Check the coordinator URL and try again.";
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
    const bool connected = jefe::qt::isRemoteConnected();
    const bool isServer  = jefe::qt::isRemoteServer();

    QString statusText = QString::fromStdString(jefe::qt::remoteStatusText());
    if (statusText.trimmed().isEmpty())
        statusText = connected ? (isServer ? "Hosting" : "Connected") : "Not connected";
    // Only touch the status label / dot when the text actually changed —
    // setStyleSheet forces a re-polish, and this runs at up to tick rate.
    if (statusText != shownStatusText_) {
        shownStatusText_ = statusText;
        statusLabel_->setText(statusText);
        // Dot color: green connected/hosting, amber connecting, gray offline.
        QString dotColor = "#6a6a70";
        if (connected) dotColor = "#5bb07a";
        else if (statusText.contains("Attempt", Qt::CaseInsensitive)) dotColor = "#d6a15b";
        statusDot_->setStyleSheet("color:" + dotColor + "; font-size: 13px;");
    }

    // Contextual sections: forms when offline, session when connected.
    // setVisible / setText are no-ops when unchanged, so these are cheap.
    connectPanel_->setVisible(!connected);
    sessionBox_->setVisible(connected);
    // Host ends the session for everyone; a client just leaves it.
    disconnectBtn_->setText(isServer ? "End Session" : "Leave");

    // Cloud-host code banner: visible only when we're a connected cloud host
    // (remoteSessionCode() is non-empty only in coordinator hosting mode).
    const QString cloudCode = connected && isServer
        ? QString::fromStdString(jefe::qt::remoteSessionCode())
        : QString();
    const bool showBanner = !cloudCode.isEmpty();
    cloudCodeBanner_->setVisible(showBanner);
    if (showBanner)
        cloudCodeBannerLabel_->setText(
            QStringLiteral("Session code: <b>%1</b>").arg(cloudCode.toHtmlEscaped()));

    // Participants change only on join/leave (which changes the count), so
    // rebuild the list only when the count moved — not on every packet.
    const auto participants = jefe::qt::remoteParticipants();
    if ((int)participants.size() != shownParticipants_) {
        shownParticipants_ = (int)participants.size();
        participantsList_->clear();
        for (const auto& name : participants)
            participantsList_->addItem(QString::fromStdString(name));
        participantsHeader_->setText(
            QString("Participants (%1)").arg(participantsList_->count()));
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

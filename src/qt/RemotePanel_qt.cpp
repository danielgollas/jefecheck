#include "RemotePanel_qt.h"
#include "CollapsibleSection_qt.h"
#include "SequenceLoadBridge_qt.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QSysInfo>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

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
#panel_remote QPushButton {
    background: #33333a; border: 1px solid #45454d; border-radius: 6px;
    padding: 7px 14px; color: #e8e8ea;
}
#panel_remote QPushButton:hover { background: #3c3c44; }
#panel_remote QPushButton[accent="true"] {
    background: #3f5666; border: 1px solid #4c6577; color: #eef2f5; font-weight: 600;
}
#panel_remote QPushButton[accent="true"]:hover { background: #486274; }
#panel_remote QPushButton:disabled { background: #2a2a2e; color: #6a6a70; border-color: #333; }
#panel_remote QPushButton[segment="true"] {
    background: #26262b; border: 1px solid #3a3a40; color: #9a9aa0;
    padding: 6px 0; font-weight: 600; border-radius: 0;
}
#panel_remote QPushButton[segment="true"][segpos="left"] {
    border-top-left-radius: 7px; border-bottom-left-radius: 7px;
}
#panel_remote QPushButton[segment="true"][segpos="right"] {
    border-top-right-radius: 7px; border-bottom-right-radius: 7px; border-left: none;
}
#panel_remote QPushButton[segment="true"]:hover { color: #cfcfd4; }
#panel_remote QPushButton[segment="true"]:checked {
    background: #3f5666; border-color: #4c6577; color: #eef2f5;
}
#panel_remote QWidget[card="true"] {
    background: #232327; border: 1px solid #3a3a40; border-radius: 8px;
    margin-top: 6px;
}
#panel_remote QListWidget, #panel_remote QTextEdit {
    background: #202024; border: 1px solid #34343a; border-radius: 8px;
    color: #dcdce0; padding: 4px;
}
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
    joinToggle_ = new QPushButton("Join", this);
    for (auto* b : {hostToggle_, joinToggle_}) {
        b->setCheckable(true);
        b->setProperty("segment", true);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    hostToggle_->setObjectName("remote.toggle.host");
    joinToggle_->setObjectName("remote.toggle.join");
    hostToggle_->setProperty("segpos", "left");
    joinToggle_->setProperty("segpos", "right");
    hostToggle_->setChecked(true);
    auto* segRow = new QHBoxLayout();
    segRow->setContentsMargins(0, 0, 0, 0);
    segRow->setSpacing(0);
    segRow->addWidget(hostToggle_);
    segRow->addWidget(joinToggle_);

    hostForm_ = makeHostPage(serverNameEdit_, serverPortSpin_,
                             serverPasswordEdit_, startServerBtn_);
    joinForm_ = makeJoinPage(clientNameEdit_, clientIPEdit_, clientPortSpin_,
                             clientPasswordEdit_, connectClientBtn_);
    hostForm_->setProperty("card", true);
    joinForm_->setProperty("card", true);
    hostForm_->setAttribute(Qt::WA_StyledBackground, true);  // paint QSS bg on plain QWidget
    joinForm_->setAttribute(Qt::WA_StyledBackground, true);
    joinForm_->setVisible(false);

    connectPanel_ = new QWidget(this);
    connectPanel_->setObjectName("remote.connect.panel");
    auto* connectLayout = new QVBoxLayout(connectPanel_);
    connectLayout->setContentsMargins(0, 0, 0, 0);
    connectLayout->setSpacing(0);
    connectLayout->addLayout(segRow);
    connectLayout->addWidget(hostForm_);
    connectLayout->addWidget(joinForm_);

    auto selectHost = [this](bool host) {
        hostToggle_->setChecked(host);
        joinToggle_->setChecked(!host);
        hostForm_->setVisible(host);
        joinForm_->setVisible(!host);
    };
    connect(hostToggle_, &QPushButton::clicked, this, [selectHost]() { selectHost(true); });
    connect(joinToggle_, &QPushButton::clicked, this, [selectHost]() { selectHost(false); });

    // ---- Session section (shown when connected) --------------------------
    participantsHeader_ = sectionLabel("Participants", this);
    participantsList_ = new QListWidget(this);
    participantsList_->setObjectName("remote.participants");
    participantsList_->setMaximumHeight(96);

    auto* chatHeader = sectionLabel("Chat", this);
    chatHeader->setObjectName("remote.chat.header");
    chatLogView_ = new QTextEdit(this);
    chatLogView_->setObjectName("remote.chatlog");
    chatLogView_->setReadOnly(true);
    chatLogBox_ = nullptr;   // chat log is now always visible in the session view

    chatInput_ = new QLineEdit(this);
    chatInput_->setObjectName("remote.chatinput");
    chatInput_->setPlaceholderText("Message… (Enter to send)");
    chatInput_->setClearButtonEnabled(true);

    disconnectBtn_ = new QPushButton("Leave session", this);
    disconnectBtn_->setObjectName("remote.disconnect.button");

    errorLabel_ = new QLabel(QString(), this);
    errorLabel_->setObjectName("remote.error");
    errorLabel_->setStyleSheet("color:#e0836c;");
    errorLabel_->setWordWrap(true);

    sessionBox_ = new QWidget(this);
    auto* sessionLayout = new QVBoxLayout(sessionBox_);
    sessionLayout->setContentsMargins(0, 0, 0, 0);
    sessionLayout->setSpacing(6);
    sessionLayout->addWidget(participantsHeader_);
    sessionLayout->addWidget(participantsList_);
    sessionLayout->addWidget(chatHeader);
    sessionLayout->addWidget(chatLogView_, /*stretch*/ 1);
    sessionLayout->addWidget(chatInput_);
    sessionLayout->addWidget(disconnectBtn_);

    // ---- Connection log (collapsible section, de-emphasized, at bottom) --
    netLogView_ = new QTextEdit(this);
    netLogView_->setObjectName("remote.netlog");
    netLogView_->setReadOnly(true);
    netLogView_->setMinimumHeight(90);
    netLogView_->setMaximumHeight(160);
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

    // ---- Assemble --------------------------------------------------------
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 14, 14, 14);
    outer->setSpacing(12);
    outer->addLayout(statusRow);
    outer->addWidget(connectPanel_);
    outer->addWidget(sessionBox_);
    outer->addWidget(errorLabel_);
    outer->addWidget(netLogSection);
    outer->addStretch(1);

    connect(startServerBtn_, &QPushButton::clicked,
            this, &RemoteDialog_Qt::onStartServerClicked);
    connect(connectClientBtn_, &QPushButton::clicked,
            this, &RemoteDialog_Qt::onConnectClientClicked);
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
    refreshConnectionState();
}

void RemoteDialog_Qt::refreshConnectionState() {
    const bool connected = jefe::qt::isRemoteConnected();
    const bool isServer  = jefe::qt::isRemoteServer();

    QString statusText = QString::fromStdString(jefe::qt::remoteStatusText());
    if (statusText.trimmed().isEmpty())
        statusText = connected ? (isServer ? "Hosting" : "Connected") : "Not connected";
    statusLabel_->setText(statusText);
    // Dot color: green connected/hosting, amber connecting, gray offline.
    QString dotColor = "#6a6a70";
    if (connected) dotColor = "#5bb07a";
    else if (statusText.contains("Attempt", Qt::CaseInsensitive)) dotColor = "#d6a15b";
    statusDot_->setStyleSheet("color:" + dotColor + "; font-size: 13px;");

    // Contextual sections: forms when offline, session when connected.
    connectPanel_->setVisible(!connected);
    sessionBox_->setVisible(connected);

    participantsList_->clear();
    for (const auto& name : jefe::qt::remoteParticipants())
        participantsList_->addItem(QString::fromStdString(name));
    participantsHeader_->setText(
        QString("Participants (%1)").arg(participantsList_->count()));

    const auto errs = jefe::qt::remoteErrors();
    errorLabel_->setText(errs.empty() ? QString()
                                      : QString::fromStdString(errs.back()));

    chatLogView_->clear();
    for (const auto& line : jefe::qt::remoteChatLog())
        chatLogView_->append(QString::fromStdString(line));

    netLogView_->clear();
    for (const auto& line : jefe::qt::remoteNetworkLog())
        netLogView_->append(QString::fromStdString(line));
}

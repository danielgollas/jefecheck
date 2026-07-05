#include "RemotePanel_qt.h"
#include "CollapsibleSection_qt.h"
#include "SequenceLoadBridge_qt.h"

#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
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
QScrollArea#remote.chatscroll, QWidget#remote.chatcontent { background: #202024; border: 1px solid #34343a; border-radius: 8px; }
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
    chatScroll_ = new QScrollArea(this);
    chatScroll_->setObjectName("remote.chatscroll");
    chatScroll_->setWidgetResizable(true);
    chatScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chatScroll_->setFrameShape(QFrame::NoFrame);
    chatContent_ = new QWidget(chatScroll_);
    chatContent_->setObjectName("remote.chatcontent");
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
    auto* sendBtn = new QPushButton("Send", this);
    sendBtn->setObjectName("remote.chat.send");
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

    sessionBox_ = new QWidget(this);
    auto* sessionLayout = new QVBoxLayout(sessionBox_);
    sessionLayout->setContentsMargins(0, 0, 0, 0);
    sessionLayout->setSpacing(6);
    sessionLayout->addLayout(sessionHeader);
    sessionLayout->addWidget(participantsList_);
    sessionLayout->addWidget(chatHeader);
    sessionLayout->addWidget(chatScroll_, /*stretch*/ 1);
    sessionLayout->addLayout(chatInputRow);

    connect(sendBtn, &QPushButton::clicked, this, &RemoteDialog_Qt::onChatSubmit);

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
        for (int i = shownChatLines_; i < total; ++i)
            appendChatBubble(entries[i]);
        shownChatLines_ = total;
        if (atBottom) {
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
    msg->setWordWrap(true);
    msg->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bLay->addWidget(header);
    bLay->addWidget(msg);

    if (e.isSelf) { rowLay->addStretch(1); rowLay->addWidget(bubble); }
    else          { rowLay->addWidget(bubble); rowLay->addStretch(1); }

    chatLayout_->insertWidget(chatLayout_->count() - 1, row);  // before stretch
}

#include "RemotePanel_qt.h"
#include "SequenceLoadBridge_qt.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {

QGroupBox* makeServerGroup(QWidget* parent,
                           QLineEdit*& nameOut,
                           QSpinBox*& portOut,
                           QLineEdit*& passwordOut,
                           QPushButton*& startBtnOut) {
    auto* group = new QGroupBox("Host (Server)", parent);
    group->setObjectName("remote.server.group");

    nameOut = new QLineEdit(group);
    nameOut->setObjectName("remote.server.name.edit");
    nameOut->setPlaceholderText("Session name");

    portOut = new QSpinBox(group);
    portOut->setObjectName("remote.server.port.spin");
    portOut->setRange(1024, 65535);
    portOut->setValue(60000);

    passwordOut = new QLineEdit(group);
    passwordOut->setObjectName("remote.server.password.edit");
    passwordOut->setEchoMode(QLineEdit::Password);
    passwordOut->setPlaceholderText("Optional");

    startBtnOut = new QPushButton("Start server", group);
    startBtnOut->setObjectName("remote.server.start.button");

    auto* form = new QFormLayout(group);
    form->addRow("Name:", nameOut);
    form->addRow("Port:", portOut);
    form->addRow("Password:", passwordOut);
    form->addRow("", startBtnOut);
    return group;
}

QGroupBox* makeClientGroup(QWidget* parent,
                           QLineEdit*& nameOut,
                           QLineEdit*& ipOut,
                           QSpinBox*& portOut,
                           QLineEdit*& passwordOut,
                           QPushButton*& connectBtnOut) {
    auto* group = new QGroupBox("Join (Client)", parent);
    group->setObjectName("remote.client.group");

    nameOut = new QLineEdit(group);
    nameOut->setObjectName("remote.client.name.edit");
    nameOut->setPlaceholderText("Your nickname");

    ipOut = new QLineEdit(group);
    ipOut->setObjectName("remote.client.ip.edit");
    ipOut->setPlaceholderText("Server IP / hostname");

    portOut = new QSpinBox(group);
    portOut->setObjectName("remote.client.port.spin");
    portOut->setRange(1024, 65535);
    portOut->setValue(60000);

    passwordOut = new QLineEdit(group);
    passwordOut->setObjectName("remote.client.password.edit");
    passwordOut->setEchoMode(QLineEdit::Password);
    passwordOut->setPlaceholderText("Optional");

    connectBtnOut = new QPushButton("Connect", group);
    connectBtnOut->setObjectName("remote.client.connect.button");

    auto* form = new QFormLayout(group);
    form->addRow("Nickname:", nameOut);
    form->addRow("Server IP:", ipOut);
    form->addRow("Port:", portOut);
    form->addRow("Password:", passwordOut);
    form->addRow("", connectBtnOut);
    return group;
}

}  // namespace

RemoteDialog_Qt::RemoteDialog_Qt(QWidget* parent) : QDialog(parent) {
    setObjectName("dialog.remote");
    setWindowTitle("Remote Session");
    setModal(false);
    resize(420, 520);

    auto* serverBox = makeServerGroup(this,
        serverNameEdit_, serverPortSpin_, serverPasswordEdit_, startServerBtn_);
    auto* clientBox = makeClientGroup(this,
        clientNameEdit_, clientIPEdit_, clientPortSpin_, clientPasswordEdit_,
        connectClientBtn_);

    disconnectBtn_ = new QPushButton("Disconnect", this);
    disconnectBtn_->setObjectName("remote.disconnect.button");

    auto* doneBtn = new QPushButton("Done", this);
    doneBtn->setObjectName("remote.done.button");

    statusLabel_ = new QLabel("Not connected", this);
    statusLabel_->setObjectName("remote.status.label");
    statusLabel_->setStyleSheet("color: #888; font-style: italic;");

    participantsList_ = new QListWidget(this);
    participantsList_->setObjectName("remote.participants");
    participantsList_->setMaximumHeight(120);

    errorLabel_ = new QLabel(QString(), this);
    errorLabel_->setObjectName("remote.error");
    errorLabel_->setStyleSheet("color:#e06c75;");
    errorLabel_->setWordWrap(true);

    chatLogBox_ = new QGroupBox(tr("Chat log"), this);
    chatLogBox_->setObjectName("remote.chatlogbox");
    chatLogBox_->setCheckable(true);
    chatLogBox_->setChecked(false);
    auto* chatLayout = new QVBoxLayout(chatLogBox_);
    chatLogView_ = new QTextEdit(chatLogBox_);
    chatLogView_->setObjectName("remote.chatlog");
    chatLogView_->setReadOnly(true);
    chatLayout->addWidget(chatLogView_);
    connect(chatLogBox_, &QGroupBox::toggled, chatLogView_, &QWidget::setVisible);
    chatLogView_->setVisible(false);

    auto* footer = new QHBoxLayout();
    footer->setContentsMargins(0, 0, 0, 0);
    footer->addWidget(statusLabel_, /*stretch*/ 1);
    footer->addWidget(disconnectBtn_);
    footer->addWidget(doneBtn);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    outer->setSpacing(8);
    outer->addWidget(serverBox);
    outer->addWidget(clientBox);
    outer->addWidget(participantsList_);
    outer->addWidget(errorLabel_);
    outer->addWidget(chatLogBox_);
    outer->addLayout(footer);

    connect(startServerBtn_, &QPushButton::clicked,
            this, &RemoteDialog_Qt::onStartServerClicked);
    connect(connectClientBtn_, &QPushButton::clicked,
            this, &RemoteDialog_Qt::onConnectClientClicked);
    connect(disconnectBtn_, &QPushButton::clicked,
            this, &RemoteDialog_Qt::onDisconnectClicked);
    connect(doneBtn, &QPushButton::clicked,
            this, &QDialog::accept);

    refreshConnectionState();
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

    statusLabel_->setText(QString::fromStdString(jefe::qt::remoteStatusText()));

    startServerBtn_->setEnabled(!connected);
    connectClientBtn_->setEnabled(!connected);
    disconnectBtn_->setEnabled(connected);

    participantsList_->clear();
    for (const auto& name : jefe::qt::remoteParticipants())
        participantsList_->addItem(QString::fromStdString(name));

    const auto errs = jefe::qt::remoteErrors();
    errorLabel_->setText(errs.empty() ? QString()
                                      : QString::fromStdString(errs.back()));

    chatLogView_->clear();
    for (const auto& line : jefe::qt::remoteChatLog())
        chatLogView_->append(QString::fromStdString(line));
}

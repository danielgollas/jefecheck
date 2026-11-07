#pragma once
// One-shot loopback listener for the OAuth redirect: bound to 127.0.0.1 (never Any) on an
// OS-assigned port, one request then closed, with a hard timeout.

#include <QObject>
#include <QPointer>
#include <QString>

#include <string>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace jefe::auth {

class LoopbackServer : public QObject {
    Q_OBJECT
public:
    explicit LoopbackServer(QObject* parent = nullptr);
    ~LoopbackServer() override;

    /**
     * Bind 127.0.0.1 on an OS-assigned port and wait for one redirect.
     * `expectedState` must match the `state` in the redirect exactly.
     * Returns false if the socket could not be bound.
     */
    bool start(const std::string& expectedState, int timeoutMs = 120000);

    /** Assigned port, or 0 before a successful start(). */
    quint16 port() const;

    /** "http://127.0.0.1:<port>" — the redirect_uri to send to Google. */
    std::string redirectUri() const;

    /** Stop listening and drop any half-open connection. Idempotent. */
    void stop();

signals:
    /**
     * Fired exactly once per start(): on a valid redirect, on a rejected one
     * (bad state, Google error), or on timeout. `code` is set only when ok.
     */
    void received(bool ok, QString code, QString error);

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket* socket);
    void finish(bool ok, const QString& code, const QString& error);

    // QPointer, not a raw pointer: the socket is deleteLater'd when the peer
    // disconnects, so a raw pointer here dangles and stop()/the destructor
    // then dereference freed memory (segfault, reliably).
    QPointer<QTcpServer> server_;
    QPointer<QTcpSocket> socket_;
    QPointer<QTimer> timeout_;
    std::string expectedState_;
    QByteArray buffer_;
    bool done_ = false;
};

/** Headless self-test (--loopback-test). Returns 0 on success. */
int loopbackSelfTest(int argc, char* argv[]);

}  // namespace jefe::auth

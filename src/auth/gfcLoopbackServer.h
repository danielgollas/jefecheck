#pragma once
// ---------------------------------------------------------------------------
// One-shot loopback listener for the OAuth redirect (JEF-31).
//
// The system browser sends Google's redirect here, carrying `code` and
// `state`. The listener exists ONLY for the duration of one sign-in attempt.
//
// Three properties are load-bearing, and all three are easy to get wrong:
//
//   * Bound to 127.0.0.1, NEVER QHostAddress::Any. `Any` would publish the
//     sign-in listener to the whole LAN.
//   * Port 0 — the OS assigns. There is then no fixed port for a hostile local
//     process to squat ahead of us. Google accepts any port on a loopback
//     redirect URI, so nothing needs registering.
//   * Exactly one request, then closed; plus a hard timeout. A listener that
//     outlives its sign-in is a standing local attack surface for no benefit.
//
// Anything on the machine can connect here — that is inherent to loopback
// redirect, and it is why PKCE (not the listener's privacy) is what actually
// protects the exchange. The `state` check is what stops another local process
// from completing ITS sign-in into our session.
// ---------------------------------------------------------------------------

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

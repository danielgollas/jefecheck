#include "gfcLoopbackServer.h"

#include "gfcOAuthPkce.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cstdio>
#include <functional>

namespace jefe::auth {
namespace {

/**
 * Cap on how much of the request we will read before giving up.
 *
 * The peer is anything on the machine, not necessarily a browser, so an
 * unbounded read is a trivial local memory-exhaustion lever. A real redirect
 * is a few hundred bytes.
 */
constexpr int kMaxRequestBytes = 8192;

/** The page the browser shows once we have what we need. */
const char* kResponseBody =
    "<!doctype html><meta charset=utf-8>"
    "<title>JefeCheck</title>"
    "<body style=\"font-family:system-ui;padding:3rem;text-align:center\">"
    "<h2>You're signed in.</h2>"
    "<p>You can close this tab and return to JefeCheck.</p>";

int g_pass = 0;
int g_fail = 0;

void check(bool cond, const char* what) {
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::fprintf(stderr, "LOOPBACK-TEST FAIL: %s\n", what);
    }
}

/** Spin the event loop until `pred` holds or the deadline passes. */
bool waitFor(const std::function<bool()>& pred, int ms) {
    QDeadlineTimer deadline(ms);
    while (!pred() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return pred();
}

}  // namespace

LoopbackServer::LoopbackServer(QObject* parent) : QObject(parent) {}

LoopbackServer::~LoopbackServer() { stop(); }

bool LoopbackServer::start(const std::string& expectedState, int timeoutMs) {
    expectedState_ = expectedState;
    done_ = false;
    buffer_.clear();

    server_ = new QTcpServer(this);
    connect(server_, &QTcpServer::newConnection, this,
            &LoopbackServer::onNewConnection);

    // LocalHost, NOT Any: binding all interfaces would expose the sign-in
    // listener to the LAN. Port 0 lets the OS choose, so no fixed port exists
    // for a hostile local process to squat before us.
    if (!server_->listen(QHostAddress::LocalHost, 0)) {
        delete server_.data();
        server_ = nullptr;
        return false;
    }

    if (timeoutMs > 0) {
        timeout_ = new QTimer(this);
        timeout_->setSingleShot(true);
        connect(timeout_, &QTimer::timeout, this, [this]() {
            // A listener must never outlive its sign-in attempt: the user
            // closed the tab, or never finished, and leaving it open is a
            // standing local attack surface for no benefit.
            finish(false, QString(), QStringLiteral("timeout"));
        });
        timeout_->start(timeoutMs);
    }
    return true;
}

quint16 LoopbackServer::port() const {
    return server_ ? server_->serverPort() : 0;
}

std::string LoopbackServer::redirectUri() const {
    return "http://127.0.0.1:" + std::to_string(port());
}

void LoopbackServer::onNewConnection() {
    if (!server_) return;
    QTcpSocket* s = server_->nextPendingConnection();
    if (s == nullptr) return;

    // One request only. Anything arriving after we are done is dropped
    // immediately rather than parsed.
    if (done_ || socket_) {
        s->close();
        s->deleteLater();
        return;
    }

    socket_ = s;
    connect(socket_, &QTcpSocket::readyRead, this,
            [this, s]() { onReadyRead(s); });
    connect(socket_, &QTcpSocket::disconnected, socket_, &QObject::deleteLater);
}

void LoopbackServer::onReadyRead(QTcpSocket* socket) {
    if (done_ || socket == nullptr) return;

    buffer_.append(socket->readAll());
    if (buffer_.size() > kMaxRequestBytes) {
        finish(false, QString(), QStringLiteral("request too large"));
        return;
    }
    // Wait for a complete request line; the query is all we need, so there is
    // no reason to wait for headers to finish.
    const int eol = buffer_.indexOf("\r\n");
    if (eol < 0 && buffer_.indexOf('\n') < 0) return;

    const int cut = eol >= 0 ? eol : buffer_.indexOf('\n');
    const std::string line = buffer_.left(cut).toStdString();

    const RedirectResult r = parseRedirect(line, expectedState_);

    // Always serve a page, even on refusal: the user is staring at a browser
    // tab and a hung request tells them nothing. The page is deliberately the
    // same either way — a distinct failure page would report to whoever sent
    // the request whether their guess at `state` was close.
    const QByteArray body = QByteArray(kResponseBody);
    QByteArray head = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    head += "Connection: close\r\n\r\n";
    socket->write(head);
    socket->write(body);
    socket->flush();
    socket->disconnectFromHost();

    if (r.ok) {
        finish(true, QString::fromStdString(r.code), QString());
    } else {
        finish(false, QString(), QString::fromStdString(r.error));
    }
}

void LoopbackServer::finish(bool ok, const QString& code, const QString& error) {
    if (done_) return;   // exactly one `received` per start()
    done_ = true;
    if (timeout_) timeout_->stop();
    // Stop listening BEFORE emitting: a slot that starts another sign-in must
    // not race the old socket.
    if (server_) server_->close();
    emit received(ok, code, error);
}

void LoopbackServer::stop() {
    done_ = true;
    // Every member is a QPointer, so a null check here is also a
    // still-alive check — the socket in particular may already have been
    // deleteLater'd by its own `disconnected` signal.
    if (timeout_) timeout_->stop();
    if (server_) {
        server_->close();
        server_->deleteLater();
        server_ = nullptr;
    }
    if (socket_) {
        socket_->close();
        socket_ = nullptr;
    }
}

int loopbackSelfTest(int argc, char* argv[]) {
    // Needs an event loop (unlike --pkce-test), but not a GUI.
    QCoreApplication app(argc, argv);
    g_pass = 0;
    g_fail = 0;

    // --- happy path -------------------------------------------------------
    {
        LoopbackServer server;
        check(server.start("STATE123"), "binds a port");
        check(server.port() != 0, "OS assigned a nonzero port");
        check(server.redirectUri().rfind("http://127.0.0.1:", 0) == 0,
              "redirect uri is loopback");

        bool fired = false, gotOk = false;
        QString gotCode;
        QObject::connect(&server, &LoopbackServer::received,
                         [&](bool ok, QString code, QString) {
                             fired = true; gotOk = ok; gotCode = code;
                         });

        QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost, server.port());
        check(client.waitForConnected(3000), "accepts a loopback connection");
        client.write("GET /?code=xyz789&state=STATE123 HTTP/1.1\r\n"
                     "Host: 127.0.0.1\r\n\r\n");
        client.flush();

        check(waitFor([&] { return fired; }, 3000), "emitted received");
        check(gotOk && gotCode == "xyz789", "delivered the code");

        // The browser must get a page it can read, not a dropped connection.
        //
        // bytesAvailable() first: the response very likely already arrived
        // while the waitFor() above was pumping events, and waitForReadyRead
        // would then block for its full timeout waiting for MORE data that is
        // never coming — reporting a failure with the response sitting in the
        // buffer.
        const bool haveResponse =
            client.bytesAvailable() > 0 || client.waitForReadyRead(3000);
        check(haveResponse, "served a response");
        check(client.readAll().contains("close this tab"), "response is the closing page");
    }

    // --- wrong state ------------------------------------------------------
    {
        LoopbackServer server;
        check(server.start("EXPECTED"), "binds for the state test");

        bool fired = false, ok = true;
        QString code = "unset";
        QObject::connect(&server, &LoopbackServer::received,
                         [&](bool o, QString c, QString) {
                             fired = true; ok = o; code = c;
                         });

        QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost, server.port());
        client.waitForConnected(3000);
        client.write("GET /?code=xyz&state=WRONG HTTP/1.1\r\n\r\n");
        client.flush();

        check(waitFor([&] { return fired; }, 3000), "emitted on wrong state");
        // The security assertion: a mismatched state yields NO code, so a
        // local process cannot log us into an account we did not choose.
        check(!ok && code.isEmpty(), "REJECTS a mismatched state, no code leaked");
    }

    // --- google error -----------------------------------------------------
    {
        LoopbackServer server;
        server.start("S");
        bool fired = false; QString err;
        QObject::connect(&server, &LoopbackServer::received,
                         [&](bool, QString, QString e) { fired = true; err = e; });
        QTcpSocket client;
        client.connectToHost(QHostAddress::LocalHost, server.port());
        client.waitForConnected(3000);
        client.write("GET /?error=access_denied&state=S HTTP/1.1\r\n\r\n");
        client.flush();
        check(waitFor([&] { return fired; }, 3000), "emitted on google error");
        check(err == "access_denied", "surfaces google's error verbatim");
    }

    // --- timeout ----------------------------------------------------------
    {
        LoopbackServer server;
        server.start("S", /*timeoutMs*/ 300);
        bool fired = false; QString err;
        QObject::connect(&server, &LoopbackServer::received,
                         [&](bool, QString, QString e) { fired = true; err = e; });
        check(waitFor([&] { return fired; }, 3000), "times out on its own");
        check(err == "timeout", "reports a timeout");
    }

    // --- one shot ---------------------------------------------------------
    {
        LoopbackServer server;
        server.start("S");
        int count = 0;
        QObject::connect(&server, &LoopbackServer::received,
                         [&](bool, QString, QString) { ++count; });
        QTcpSocket c1;
        c1.connectToHost(QHostAddress::LocalHost, server.port());
        c1.waitForConnected(3000);
        c1.write("GET /?code=a&state=S HTTP/1.1\r\n\r\n");
        c1.flush();
        waitFor([&] { return count > 0; }, 3000);

        // A second delivery must not fire `received` again — the caller has
        // already moved on to the token exchange.
        QTcpSocket c2;
        c2.connectToHost(QHostAddress::LocalHost, server.port());
        c2.waitForConnected(500);
        c2.write("GET /?code=b&state=S HTTP/1.1\r\n\r\n");
        c2.flush();
        waitFor([&] { return count > 1; }, 500);
        check(count == 1, "emits exactly once per start()");
    }

    std::printf("LOOPBACK-TEST: pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

}  // namespace jefe::auth

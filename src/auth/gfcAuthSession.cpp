#include "gfcAuthSession.h"

#include "gfcLoopbackServer.h"
#include "gfcOAuthPkce.h"
#include "gfcTokenStore.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QHostAddress>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include <cstdio>
#include <functional>

namespace jefe::auth {
namespace {

constexpr const char* kGoogleTokenEndpoint = "https://oauth2.googleapis.com/token";

int g_pass = 0;
int g_fail = 0;

void check(bool cond, const char* what) {
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::fprintf(stderr, "AUTH-TEST FAIL: %s\n", what);
    }
}

bool waitFor(const std::function<bool()>& pred, int ms) {
    QDeadlineTimer deadline(ms);
    while (!pred() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return pred();
}

}  // namespace

struct AuthSession::Impl {
    AuthConfig config;
    TokenStore* store = nullptr;
    QNetworkAccessManager* net = nullptr;

    std::string accessToken;
    QString email;
    long long balanceSeconds = -1;

    QPointer<LoopbackServer> listener;
    std::string verifier;
    bool busy = false;
};

AuthSession::AuthSession(AuthConfig config, TokenStore* store, QObject* parent)
    : QObject(parent), d_(std::make_unique<Impl>()) {
    d_->config = std::move(config);
    d_->store = store;
    d_->net = new QNetworkAccessManager(this);
}

AuthSession::~AuthSession() = default;

bool AuthSession::haveStoredToken() const {
    return d_->store != nullptr && !d_->store->load(d_->config.account).empty();
}

std::string AuthSession::accessToken() const { return d_->accessToken; }
QString AuthSession::email() const { return d_->email; }
long long AuthSession::creditBalanceSeconds() const { return d_->balanceSeconds; }

void AuthSession::signOut() {
    d_->accessToken.clear();
    d_->email.clear();
    d_->balanceSeconds = -1;
    if (d_->store != nullptr) d_->store->erase(d_->config.account);
    emit signedOut();
}

void AuthSession::signIn() {
    // One attempt at a time: a second browser window would race the first and
    // leave an orphaned listener bound.
    if (d_->busy) {
        emit signInFailed(QStringLiteral("a sign-in is already in progress"));
        return;
    }
    d_->busy = true;

    const PkcePair pkce = generatePkce();
    const std::string state = generateState();
    d_->verifier = pkce.verifier;

    auto* listener = new LoopbackServer(this);
    d_->listener = listener;
    if (!listener->start(state)) {
        d_->busy = false;
        listener->deleteLater();
        emit signInFailed(QStringLiteral("could not open a local port for sign-in"));
        return;
    }

    const std::string redirectUri = listener->redirectUri();

    connect(listener, &LoopbackServer::received, this,
            [this, listener, redirectUri](bool ok, QString code, QString error) {
                listener->deleteLater();
                if (!ok) {
                    d_->busy = false;
                    // Google's own reason when it gave one; ours otherwise.
                    emit signInFailed(error.isEmpty()
                                          ? QStringLiteral("sign-in was not completed")
                                          : error);
                    return;
                }
                exchangeWithGoogle(code.toStdString(), redirectUri);
            });

    QDesktopServices::openUrl(QUrl(QString::fromStdString(
        buildAuthUrl(d_->config.googleClientId, redirectUri, pkce.challenge, state))));
}

void AuthSession::exchangeWithGoogle(const std::string& code,
                                     const std::string& redirectUri) {
    QUrlQuery form;
    form.addQueryItem("code", QString::fromStdString(code));
    form.addQueryItem("client_id", QString::fromStdString(d_->config.googleClientId));
    form.addQueryItem("client_secret",
                      QString::fromStdString(d_->config.googleClientSecret));
    form.addQueryItem("redirect_uri", QString::fromStdString(redirectUri));
    form.addQueryItem("grant_type", "authorization_code");
    // The verifier proves we are the same process that started the flow. This
    // is what makes a stolen authorization code useless.
    form.addQueryItem("code_verifier", QString::fromStdString(d_->verifier));

    QNetworkRequest req{QUrl(QString::fromLatin1(kGoogleTokenEndpoint))};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "application/x-www-form-urlencoded");

    QNetworkReply* reply =
        d_->net->post(req, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        d_->verifier.clear();   // single use; never keep it around

        const QJsonObject obj =
            QJsonDocument::fromJson(reply->readAll()).object();
        const QString idToken = obj.value("id_token").toString();
        if (idToken.isEmpty()) {
            d_->busy = false;
            const QString desc = obj.value("error_description").toString();
            emit signInFailed(desc.isEmpty() ? QStringLiteral("Google did not return an identity")
                                             : desc);
            return;
        }
        exchangeWithCoordinator(idToken);
    });
}

void AuthSession::exchangeWithCoordinator(const QString& googleIdToken) {
    QJsonObject body;
    body.insert("google_id_token", googleIdToken);

    QNetworkRequest req{QUrl(QString::fromStdString(d_->config.httpBase + "/auth/exchange"))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = d_->net->post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        d_->busy = false;
        applyAuthResult(QJsonDocument::fromJson(reply->readAll()).object(),
                        QStringLiteral("this account could not be verified"));
    });
}

void AuthSession::refresh() {
    if (d_->store == nullptr) {
        emit signInFailed(QStringLiteral("no token storage"));
        return;
    }
    const std::string stored = d_->store->load(d_->config.account);
    if (stored.empty()) {
        emit signInFailed(QStringLiteral("not signed in"));
        return;
    }

    QJsonObject body;
    body.insert("refresh_token", QString::fromStdString(stored));

    QNetworkRequest req{QUrl(QString::fromStdString(d_->config.httpBase + "/auth/refresh"))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = d_->net->post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (status == 401 || status == 403) {
            // Rejected or reused. Clear it and report ONCE — the coordinator
            // rotates refresh tokens and treats reuse as a compromise signal,
            // so retrying is both useless and hostile. The caller decides
            // whether to open a browser; doing it here would surprise the user
            // with a window they did not ask for.
            d_->store->erase(d_->config.account);
            d_->accessToken.clear();
            emit signInFailed(QStringLiteral("your session expired — sign in again"));
            return;
        }
        applyAuthResult(QJsonDocument::fromJson(reply->readAll()).object(),
                        QStringLiteral("could not refresh your session"));
    });
}

void AuthSession::applyAuthResult(const QJsonObject& obj, const QString& failMsg) {
    const QString access = obj.value("access_token").toString();
    if (access.isEmpty()) {
        emit signInFailed(failMsg);
        return;
    }
    d_->accessToken = access.toStdString();

    const QString refreshTok = obj.value("refresh_token").toString();
    if (!refreshTok.isEmpty() && d_->store != nullptr) {
        d_->store->save(d_->config.account, refreshTok.toStdString());
    }

    const QJsonObject user = obj.value("user").toObject();
    if (!user.isEmpty()) {
        d_->email = user.value("email").toString();
        if (user.contains("creditBalanceSeconds")) {
            d_->balanceSeconds =
                static_cast<long long>(user.value("creditBalanceSeconds").toDouble(-1));
        }
    }
    emit signedIn();
}

// ---------------------------------------------------------------------------
// Self-test: a local HTTP stub stands in for the coordinator, so no live
// network call happens. The Google leg is not exercised here (it needs a real
// browser and a real consent screen) — the pieces it depends on are covered by
// --pkce-test and --loopback-test.
// ---------------------------------------------------------------------------
namespace {

/** Minimal one-shot HTTP stub returning a canned JSON body per path. */
class StubServer : public QObject {
public:
    explicit StubServer(QObject* parent = nullptr) : QObject(parent) {
        server_ = new QTcpServer(this);
        server_->listen(QHostAddress::LocalHost, 0);
        QObject::connect(server_, &QTcpServer::newConnection, this, [this]() {
            QTcpSocket* s = server_->nextPendingConnection();
            QObject::connect(s, &QTcpSocket::readyRead, s, [this, s]() {
                const QByteArray req = s->readAll();
                ++requests;
                lastBody = req.mid(req.indexOf("\r\n\r\n") + 4);

                QByteArray body = nextBody;
                QByteArray head = "HTTP/1.1 " + QByteArray::number(nextStatus) +
                                  " X\r\nContent-Type: application/json\r\n";
                head += "Content-Length: " + QByteArray::number(body.size()) +
                        "\r\nConnection: close\r\n\r\n";
                s->write(head);
                s->write(body);
                s->flush();
                s->disconnectFromHost();
            });
        });
    }

    std::string base() const {
        return "http://127.0.0.1:" + std::to_string(server_->serverPort());
    }

    QByteArray nextBody = "{}";
    int nextStatus = 200;
    int requests = 0;
    QByteArray lastBody;

private:
    QTcpServer* server_ = nullptr;
};

}  // namespace

int authSelfTest(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    g_pass = 0;
    g_fail = 0;

    const QString dir = QDir::temp().filePath(QStringLiteral("jefe-auth-test"));
    QDir(dir).removeRecursively();
    auto store = makeFileTokenStore(dir.toStdString());

    StubServer stub;
    AuthConfig cfg;
    cfg.httpBase = stub.base();
    cfg.googleClientId = "cid";
    cfg.googleClientSecret = "csecret";
    cfg.account = "wss://test.invalid/dev";

    // --- refresh with no stored token ------------------------------------
    {
        AuthSession s(cfg, store.get());
        bool failed = false;
        QObject::connect(&s, &AuthSession::signInFailed,
                         [&](QString) { failed = true; });
        check(!s.haveStoredToken(), "starts with no stored token");
        s.refresh();
        check(waitFor([&] { return failed; }, 2000),
              "refresh without a stored token fails immediately");
        check(stub.requests == 0, "and makes no network call");
    }

    // --- successful refresh ----------------------------------------------
    {
        store->save(cfg.account, "stored-refresh");
        stub.nextStatus = 200;
        stub.nextBody =
            R"({"access_token":"acc1","refresh_token":"rot1",)"
            R"("user":{"email":"a@b.com","creditBalanceSeconds":3597}})";

        AuthSession s(cfg, store.get());
        bool ok = false;
        QObject::connect(&s, &AuthSession::signedIn, [&]() { ok = true; });
        check(s.haveStoredToken(), "sees the stored token");
        s.refresh();
        check(waitFor([&] { return ok; }, 3000), "refresh succeeds");
        check(s.accessToken() == "acc1", "access token is held in memory");
        check(s.email() == "a@b.com", "identity is captured");
        check(s.creditBalanceSeconds() == 3597, "credit balance is captured");
        // Rotation: the NEW refresh token must replace the old one, or the
        // next refresh presents a reused token and gets locked out.
        check(store->load(cfg.account) == "rot1", "rotated refresh token is stored");
    }

    // --- rejected refresh -------------------------------------------------
    {
        stub.nextStatus = 401;
        stub.nextBody = R"({"error":"invalid-refresh"})";

        AuthSession s(cfg, store.get());
        int failures = 0;
        QObject::connect(&s, &AuthSession::signInFailed,
                         [&](QString) { ++failures; });
        const int before = stub.requests;
        s.refresh();
        check(waitFor([&] { return failures > 0; }, 3000), "rejected refresh fails");
        // Exactly one attempt: the coordinator treats refresh reuse as a
        // compromise signal, so a retry loop is both useless and hostile.
        check(stub.requests == before + 1, "does NOT retry a rejected refresh");
        check(!s.haveStoredToken(), "clears the stored token so it cannot be reused");
        check(s.accessToken().empty(), "drops the in-memory access token");
    }

    // --- sign out ---------------------------------------------------------
    {
        store->save(cfg.account, "another");
        AuthSession s(cfg, store.get());
        bool out = false;
        QObject::connect(&s, &AuthSession::signedOut, [&]() { out = true; });
        s.signOut();
        check(out, "signOut emits");
        check(!s.haveStoredToken(), "signOut clears storage");
        check(s.accessToken().empty(), "signOut clears memory");
    }

    QDir(dir).removeRecursively();
    std::printf("AUTH-TEST: pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

}  // namespace jefe::auth

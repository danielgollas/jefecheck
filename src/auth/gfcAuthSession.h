#pragma once
// ---------------------------------------------------------------------------
// Desktop sign-in session (JEF-31).
//
// Owns the ACCESS token in memory and the REFRESH token in the TokenStore, and
// drives the two network steps the rest of the flow needs:
//
//   signIn()  — full browser round trip:
//               PKCE -> loopback listener -> system browser -> code
//               -> Google token endpoint (id_token)
//               -> coordinator /auth/exchange (our own JWT + refresh token)
//   refresh() — silent: coordinator /auth/refresh with the stored token.
//
// Everything is asynchronous and signal-based. Nothing here blocks the GUI
// thread: the Remote dialog already runs its connects off-thread with a
// QPointer guard (RemotePanel_qt.cpp), and a modal wait during sign-in would
// freeze the app for as long as the user takes to click through Google.
//
// A rejected refresh token clears storage and reports failure ONCE. It never
// retries in a loop: the coordinator rotates refresh tokens and treats reuse
// as a compromise signal, so hammering it is both useless and hostile.
// ---------------------------------------------------------------------------

#include <QObject>
#include <QString>

#include <memory>
#include <string>

class QNetworkAccessManager;
class QJsonObject;

namespace jefe::auth {

class TokenStore;

struct AuthConfig {
    /** Base URL of the coordinator's HTTP API (no trailing slash). */
    std::string httpBase;
    /** Google Desktop-app client id. */
    std::string googleClientId;
    /**
     * Google's client "secret" for a Desktop client.
     *
     * Ships in the binary and is NOT a security boundary — RFC 8252 installed
     * apps are public clients, and PKCE is what protects the exchange. Named
     * plainly rather than hidden, so nobody mistakes it for one.
     */
    std::string googleClientSecret;
    /** Account key for the TokenStore: the coordinator's WS URL. */
    std::string account;
};

class AuthSession : public QObject {
    Q_OBJECT
public:
    AuthSession(AuthConfig config, TokenStore* store, QObject* parent = nullptr);
    ~AuthSession() override;

    /** True when a refresh token is stored — i.e. signIn() can be skipped. */
    bool haveStoredToken() const;

    /** Current access token, or "" when none is live. */
    std::string accessToken() const;

    /** Identity from the last successful exchange/refresh. */
    QString email() const;
    /** Credit balance in seconds from the last exchange/refresh; -1 unknown. */
    long long creditBalanceSeconds() const;

    /** Full browser sign-in. Emits signedIn or signInFailed exactly once. */
    void signIn();

    /**
     * Silent refresh using the stored token. Emits signedIn on success; on
     * rejection clears the stored token and emits signInFailed so the caller
     * can fall back to signIn() — deliberately NOT automatic, so a caller
     * never triggers a surprise browser window.
     */
    void refresh();

    /** Forget everything, in memory and on disk. */
    void signOut();

signals:
    void signedIn();
    void signInFailed(QString reason);
    void signedOut();

private:
    void exchangeWithGoogle(const std::string& code, const std::string& redirectUri);
    void exchangeWithCoordinator(const QString& googleIdToken);
    void applyAuthResult(const QJsonObject& obj, const QString& failMsg);

    struct Impl;
    std::unique_ptr<Impl> d_;
};

/** Headless self-test (--auth-test) against a local stub. Returns 0 on success. */
int authSelfTest(int argc, char* argv[]);

}  // namespace jefe::auth

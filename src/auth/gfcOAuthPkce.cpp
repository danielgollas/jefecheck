#include "gfcOAuthPkce.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

#include <cstdio>

namespace jefe::auth {
namespace {

// Google's endpoint for the installed-app / loopback flow.
constexpr const char* kAuthEndpoint = "https://accounts.google.com/o/oauth2/v2/auth";

// RFC 7636 §4.1 unreserved set. Deliberately NOT base64: a verifier is sent
// as a form field and must survive percent-encoding untouched.
constexpr const char* kVerifierAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";

/**
 * `QRandomGenerator::system()` is the OS CSPRNG.
 *
 * NOT `global()`, which is a seeded PRNG intended for non-security use: a
 * predictable verifier would defeat the entire point of PKCE, and a
 * predictable `state` would reopen the login-injection hole it closes.
 */
std::string randomFromAlphabet(int length, const char* alphabet, size_t alphabetLen) {
    std::string out;
    out.reserve(static_cast<size_t>(length));
    for (int i = 0; i < length; ++i) {
        const quint32 r = QRandomGenerator::system()->bounded(
            static_cast<quint32>(alphabetLen));
        out.push_back(alphabet[r]);
    }
    return out;
}

int g_pass = 0;
int g_fail = 0;

void check(bool cond, const char* what) {
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::fprintf(stderr, "PKCE-TEST FAIL: %s\n", what);
    }
}

}  // namespace

std::string base64UrlNoPad(const unsigned char* data, size_t len) {
    const QByteArray raw(reinterpret_cast<const char*>(data),
                         static_cast<qsizetype>(len));
    // Base64UrlEncoding swaps +/ for -_ ; OmitTrailingEquals drops the padding
    // that RFC 7636 requires be absent.
    return raw.toBase64(QByteArray::Base64UrlEncoding |
                        QByteArray::OmitTrailingEquals).toStdString();
}

std::string sha256Base64Url(const std::string& input) {
    const QByteArray digest = QCryptographicHash::hash(
        QByteArray::fromStdString(input), QCryptographicHash::Sha256);
    return base64UrlNoPad(
        reinterpret_cast<const unsigned char*>(digest.constData()),
        static_cast<size_t>(digest.size()));
}

PkcePair generatePkce() {
    PkcePair p;
    // 64 chars sits mid-range of RFC 7636's 43-128 and gives ~380 bits.
    p.verifier = randomFromAlphabet(64, kVerifierAlphabet, 64);
    p.challenge = sha256Base64Url(p.verifier);
    return p;
}

std::string generateState() {
    static constexpr const char* kHex = "0123456789abcdef";
    return randomFromAlphabet(32, kHex, 16);
}

std::string buildAuthUrl(const std::string& clientId,
                         const std::string& redirectUri,
                         const std::string& challenge,
                         const std::string& state) {
    QUrlQuery q;
    q.addQueryItem("client_id", QString::fromStdString(clientId));
    q.addQueryItem("redirect_uri", QString::fromStdString(redirectUri));
    q.addQueryItem("response_type", "code");
    // openid+email+profile only: non-sensitive scopes, so the consent screen
    // needs no Google verification review.
    q.addQueryItem("scope", "openid email profile");
    q.addQueryItem("code_challenge", QString::fromStdString(challenge));
    q.addQueryItem("code_challenge_method", "S256");
    q.addQueryItem("state", QString::fromStdString(state));

    QUrl url(QString::fromLatin1(kAuthEndpoint));
    url.setQuery(q);
    return url.toString(QUrl::FullyEncoded).toStdString();
}

RedirectResult parseRedirect(const std::string& requestLine,
                             const std::string& expectedState) {
    RedirectResult r;

    // "GET /?code=…&state=… HTTP/1.1" -> the middle field.
    const QString line = QString::fromStdString(requestLine);
    const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 2) {
        r.error = "malformed request line";
        return r;
    }

    const QUrl target(parts.at(1), QUrl::StrictMode);
    if (!target.isValid()) {
        r.error = "malformed request target";
        return r;
    }
    const QUrlQuery q(target.query());

    // Google reports user refusal / consent failure here rather than by
    // withholding the code, so surface its reason instead of a generic one.
    const QString err = q.queryItemValue("error", QUrl::FullyDecoded);
    if (!err.isEmpty()) {
        r.error = err.toStdString();
        return r;
    }

    // STATE FIRST. A response carrying a valid-looking code but the wrong
    // state is an injection attempt, not a near-miss — never look at its code.
    const QString state = q.queryItemValue("state", QUrl::FullyDecoded);
    if (state.isEmpty() || state.toStdString() != expectedState) {
        r.error = "state mismatch";
        return r;
    }

    const QString code = q.queryItemValue("code", QUrl::FullyDecoded);
    if (code.isEmpty()) {
        r.error = "no code in redirect";
        return r;
    }

    r.ok = true;
    r.code = code.toStdString();
    return r;
}

int pkceSelfTest() {
    g_pass = 0;
    g_fail = 0;

    // RFC 7636 Appendix B, the canonical verifier/challenge vector. If this
    // derivation is wrong, Google rejects every exchange with invalid_grant
    // and the error points at the token endpoint rather than at this function.
    check(sha256Base64Url("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") ==
              "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
          "RFC 7636 B.1 challenge vector");

    // base64url alphabet: - and _ rather than + and /, and no padding.
    const unsigned char raw[] = {0xfb, 0xff, 0xbe};
    check(base64UrlNoPad(raw, 3) == "-_--", "base64url alphabet, no padding");

    const PkcePair p = generatePkce();
    check(p.verifier.size() >= 43 && p.verifier.size() <= 128,
          "verifier length inside the RFC range");
    check(p.verifier.find_first_not_of(kVerifierAlphabet) == std::string::npos,
          "verifier uses only unreserved characters");
    check(p.challenge == sha256Base64Url(p.verifier), "challenge matches verifier");
    check(generatePkce().verifier != p.verifier, "verifier is not constant");
    check(generateState() != generateState(), "state is not constant");

    const std::string url =
        buildAuthUrl("cid.apps.googleusercontent.com", "http://127.0.0.1:5599",
                     "CHAL", "STATE");
    check(url.find("code_challenge=CHAL") != std::string::npos, "url carries the challenge");
    check(url.find("code_challenge_method=S256") != std::string::npos, "url pins S256");
    check(url.find("state=STATE") != std::string::npos, "url carries state");
    check(url.find("response_type=code") != std::string::npos, "url requests a code");
    check(url.find("127.0.0.1") != std::string::npos, "url carries the loopback redirect");
    check(url.find("openid") != std::string::npos, "url requests openid scope");

    // --- redirect parsing -------------------------------------------------
    const RedirectResult good =
        parseRedirect("GET /?code=abc123&state=STATE HTTP/1.1", "STATE");
    check(good.ok && good.code == "abc123", "parses the code on a matching state");

    const RedirectResult wrong =
        parseRedirect("GET /?code=abc123&state=OTHER HTTP/1.1", "STATE");
    check(!wrong.ok && wrong.code.empty(), "REJECTS a mismatched state");

    const RedirectResult missing =
        parseRedirect("GET /?code=abc123 HTTP/1.1", "STATE");
    check(!missing.ok, "rejects an absent state");

    const RedirectResult noCode =
        parseRedirect("GET /?state=STATE HTTP/1.1", "STATE");
    check(!noCode.ok, "rejects a missing code");

    const RedirectResult denied =
        parseRedirect("GET /?error=access_denied&state=STATE HTTP/1.1", "STATE");
    check(!denied.ok && denied.error == "access_denied", "surfaces Google's error");

    // An error response must not be salvaged even if it carries a code.
    const RedirectResult errWithCode =
        parseRedirect("GET /?error=access_denied&code=abc&state=STATE HTTP/1.1", "STATE");
    check(!errWithCode.ok, "an error response is refused even with a code present");

    check(!parseRedirect("not a request line", "STATE").ok,
          "malformed request line does not throw");
    check(!parseRedirect("", "STATE").ok, "empty input does not throw");

    // A percent-encoded code must decode; Google's codes contain '/' and '-'.
    const RedirectResult enc =
        parseRedirect("GET /?code=4%2F0Ab_c-d&state=STATE HTTP/1.1", "STATE");
    check(enc.ok && enc.code == "4/0Ab_c-d", "percent-encoded code is decoded");

    std::printf("PKCE-TEST: pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

}  // namespace jefe::auth

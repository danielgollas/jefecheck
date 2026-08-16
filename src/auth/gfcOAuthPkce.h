#pragma once
// ---------------------------------------------------------------------------
// PKCE codec for desktop Google sign-in (JEF-31).
//
// PURE: no sockets, no files, no Qt event loop. Everything here is string and
// crypto work, so the whole flow's trickiest parts are testable headlessly
// (--pkce-test) rather than only by clicking through a browser.
//
// The flow this serves is RFC 8252 (OAuth for native apps):
//
//   1. generatePkce()  -> a random verifier + its S256 challenge
//   2. generateState() -> a random anti-injection nonce
//   3. buildAuthUrl()  -> opened in the SYSTEM browser (never a webview:
//                         Google blocks embedded webviews, and an embedded one
//                         could read the user's password)
//   4. the browser redirects to http://127.0.0.1:<port>/?code=…&state=…
//   5. parseRedirect() -> validates `state`, extracts `code`
//   6. the code is exchanged with the verifier (network step, not here)
//
// Security note: the authorization code is delivered over plain loopback HTTP
// and lands in browser history. That is fine and is what makes PKCE necessary
// rather than optional — a stolen code is useless without the verifier, which
// never leaves this process and never appears in any URL.
// ---------------------------------------------------------------------------

#include <string>

namespace jefe::auth {

struct PkcePair {
    /** RFC 7636 code_verifier: 43-128 chars from [A-Za-z0-9-._~]. */
    std::string verifier;
    /** base64url(SHA-256(verifier)), unpadded. */
    std::string challenge;
};

/** Fresh verifier + challenge from the system CSPRNG. */
PkcePair generatePkce();

/**
 * Random `state` nonce.
 *
 * Not decoration: without it, any local process could drive its own sign-in
 * and hand the resulting code to our listener, logging the user into an
 * account they did not choose.
 */
std::string generateState();

/** base64url encoding (RFC 4648 §5) with padding stripped. */
std::string base64UrlNoPad(const unsigned char* data, size_t len);

/** base64url(SHA-256(input)), unpadded — the S256 challenge transform. */
std::string sha256Base64Url(const std::string& input);

/**
 * Google's authorization endpoint URL for the installed-app flow.
 * `redirectUri` is the loopback address the listener bound to.
 */
std::string buildAuthUrl(const std::string& clientId,
                         const std::string& redirectUri,
                         const std::string& challenge,
                         const std::string& state);

struct RedirectResult {
    bool ok = false;
    std::string code;   // set when ok
    std::string error;  // Google's error, or a local reason, when !ok
};

/**
 * Parse the first line of the browser's HTTP request ("GET /?… HTTP/1.1").
 *
 * Returns ok only when `state` matches `expectedState` exactly AND a code is
 * present. Never throws: malformed input is a refusal, not an exception.
 */
RedirectResult parseRedirect(const std::string& requestLine,
                             const std::string& expectedState);

/** Headless self-test (--pkce-test). Returns 0 on success. */
int pkceSelfTest();

}  // namespace jefe::auth

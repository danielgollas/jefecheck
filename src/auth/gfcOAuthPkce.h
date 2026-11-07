#pragma once
// PKCE codec for desktop Google sign-in (RFC 8252): pure string/crypto work,
// testable headlessly (--pkce-test). The verifier never leaves the process.

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

/** Random `state` nonce (anti login-injection). */
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

/** Parse the first line of the browser's HTTP request ("GET /?… HTTP/1.1").
 *  ok only when `state` matches exactly and a code is present; never throws. */
RedirectResult parseRedirect(const std::string& requestLine,
                             const std::string& expectedState);

/** Headless self-test (--pkce-test). Returns 0 on success. */
int pkceSelfTest();

}  // namespace jefe::auth

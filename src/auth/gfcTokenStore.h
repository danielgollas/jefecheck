#pragma once
// ---------------------------------------------------------------------------
// Refresh-token storage for desktop sign-in (JEF-31).
//
// ONLY the refresh token is persisted. The access token stays in memory: it
// expires in an hour and is cheap to re-mint, so writing it to disk would add
// a long-lived copy of a credential for no benefit.
//
// Keyed by coordinator URL, so a token for JefeCheck Cloud and one for a
// self-hosted coordinator can coexist and can never be sent to the wrong
// service.
//
// Backends:
//   macOS    Keychain (Security.framework)          — in the SDK
//   Windows  Credential Manager (wincred.h)          — in the SDK
//   Linux    0600 file under $XDG_CONFIG_HOME        — see below
//
// Linux deliberately does NOT use libsecret. It would require a running
// secret-service daemon, which breaks headless and CI use, in exchange for
// protection that mostly matters against another user on the same machine —
// a threat the 0600 file already covers. This is the weaker backend and is
// documented as such rather than presented as equivalent.
// ---------------------------------------------------------------------------

#include <memory>
#include <string>

namespace jefe::auth {

class TokenStore {
public:
    virtual ~TokenStore() = default;

    /** Store (or replace) the refresh token for `account`. */
    virtual bool save(const std::string& account, const std::string& refreshToken) = 0;

    /** Load it, or "" when absent. Absence is not an error. */
    virtual std::string load(const std::string& account) = 0;

    /** Remove it. Returns false when there was nothing to remove. */
    virtual bool erase(const std::string& account) = 0;
};

/** The platform-native store (file-backed on Linux). */
std::unique_ptr<TokenStore> makeTokenStore();

/**
 * File-backed store rooted at `dir`. Used by the self-test on every platform,
 * and the real backend on Linux.
 */
std::unique_ptr<TokenStore> makeFileTokenStore(const std::string& dir);

/** Headless self-test (--tokenstore-test). Returns 0 on success. */
int tokenStoreSelfTest();

}  // namespace jefe::auth

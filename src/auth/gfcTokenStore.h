// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Refresh-token storage for desktop sign-in (refresh token only; the access token stays in memory).
// Keyed by coordinator URL. Backends: Keychain (macOS), Credential Manager (Windows), a 0600 file on Linux.

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

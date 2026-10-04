// Windows Credential Manager backend for the refresh token (JEF-31).
//
// wincred.h ships with the Windows SDK, so this adds no dependency. Credentials
// are stored per-user and encrypted by the OS; the target name carries the
// coordinator URL so a hosted-service token and a self-hosted one stay
// separate.

#include "gfcTokenStore.h"

#include <windows.h>
#include <wincred.h>

#include <string>
#include <vector>

namespace jefe::auth {
namespace {

std::wstring targetFor(const std::string& account) {
    const std::string target = "JefeCheck:" + account;
    if (target.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, target.c_str(),
                                      static_cast<int>(target.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, target.c_str(),
                        static_cast<int>(target.size()), w.data(), n);
    return w;
}

class WinCredTokenStore : public TokenStore {
public:
    bool save(const std::string& account, const std::string& refreshToken) override {
        std::wstring target = targetFor(account);

        CREDENTIALW cred = {};
        cred.Type = CRED_TYPE_GENERIC;
        cred.TargetName = target.data();
        cred.CredentialBlobSize = static_cast<DWORD>(refreshToken.size());
        cred.CredentialBlob = reinterpret_cast<LPBYTE>(
            const_cast<char*>(refreshToken.data()));
        // LOCAL_MACHINE, not ENTERPRISE: a refresh token is a credential for
        // this install and must not roam to the user's other machines.
        cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

        // CredWriteW overwrites an existing target, so unlike the Keychain
        // path there is no duplicate case to handle.
        return CredWriteW(&cred, 0) != FALSE;
    }

    std::string load(const std::string& account) override {
        std::wstring target = targetFor(account);
        PCREDENTIALW cred = nullptr;
        if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) == FALSE) {
            // ERROR_NOT_FOUND is simply "no token stored" — an absent
            // credential is not a failure.
            return {};
        }
        std::string out(reinterpret_cast<const char*>(cred->CredentialBlob),
                        cred->CredentialBlobSize);
        CredFree(cred);
        return out;
    }

    bool erase(const std::string& account) override {
        std::wstring target = targetFor(account);
        return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) != FALSE;
    }
};

}  // namespace

std::unique_ptr<TokenStore> makeTokenStore() {
    return std::make_unique<WinCredTokenStore>();
}

}  // namespace jefe::auth

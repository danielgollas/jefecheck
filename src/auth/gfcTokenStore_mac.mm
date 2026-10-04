// macOS Keychain backend for the refresh token (JEF-31).
//
// Security.framework ships with the SDK, so this adds no dependency. The item
// is a generic password under service "JefeCheck", with the coordinator URL as
// the account — which is what lets a hosted-service token and a self-hosted
// one coexist without ever being confused for each other.

#include "gfcTokenStore.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

namespace jefe::auth {
namespace {

constexpr const char* kService = "JefeCheck";

NSData* toData(const std::string& s) {
    return [NSData dataWithBytes:s.data() length:s.size()];
}

NSString* toStr(const std::string& s) {
    return [NSString stringWithUTF8String:s.c_str()];
}

/** Query matching exactly one account's item. */
NSMutableDictionary* baseQuery(const std::string& account) {
    NSMutableDictionary* q = [NSMutableDictionary dictionary];
    q[(__bridge id)kSecClass] = (__bridge id)kSecClassGenericPassword;
    q[(__bridge id)kSecAttrService] = toStr(kService);
    q[(__bridge id)kSecAttrAccount] = toStr(account);
    return q;
}

class KeychainTokenStore : public TokenStore {
public:
    bool save(const std::string& account, const std::string& refreshToken) override {
        @autoreleasepool {
            NSMutableDictionary* q = baseQuery(account);
            q[(__bridge id)kSecValueData] = toData(refreshToken);
            // Available only when the device is unlocked, and never migrated
            // to another machine by a backup: a refresh token is a credential
            // for THIS install, not a document.
            q[(__bridge id)kSecAttrAccessible] =
                (__bridge id)kSecAttrAccessibleWhenUnlockedThisDeviceOnly;

            OSStatus st = SecItemAdd((__bridge CFDictionaryRef)q, NULL);

            // The classic bug here: treating a re-save as a failure. Every
            // token refresh replaces an existing item, so duplicate is the
            // COMMON path, not an error — fall through to update.
            if (st == errSecDuplicateItem) {
                NSMutableDictionary* attrs = [NSMutableDictionary dictionary];
                attrs[(__bridge id)kSecValueData] = toData(refreshToken);
                st = SecItemUpdate((__bridge CFDictionaryRef)baseQuery(account),
                                   (__bridge CFDictionaryRef)attrs);
            }
            return st == errSecSuccess;
        }
    }

    std::string load(const std::string& account) override {
        @autoreleasepool {
            NSMutableDictionary* q = baseQuery(account);
            q[(__bridge id)kSecReturnData] = @YES;
            q[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;

            CFTypeRef out = NULL;
            const OSStatus st = SecItemCopyMatching((__bridge CFDictionaryRef)q, &out);
            if (st != errSecSuccess || out == NULL) return {};   // absent is fine

            NSData* data = (__bridge_transfer NSData*)out;
            return std::string(static_cast<const char*>(data.bytes), data.length);
        }
    }

    bool erase(const std::string& account) override {
        @autoreleasepool {
            const OSStatus st =
                SecItemDelete((__bridge CFDictionaryRef)baseQuery(account));
            // itemNotFound is "nothing to remove", which the interface reports
            // as false rather than as a failure.
            return st == errSecSuccess;
        }
    }
};

}  // namespace

std::unique_ptr<TokenStore> makeTokenStore() {
    return std::make_unique<KeychainTokenStore>();
}

}  // namespace jefe::auth

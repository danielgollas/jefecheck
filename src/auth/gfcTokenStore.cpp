#include "gfcTokenStore.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>

#include <cstdio>

namespace jefe::auth {
namespace {

int g_pass = 0;
int g_fail = 0;

void check(bool cond, const char* what) {
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::fprintf(stderr, "TOKENSTORE-TEST FAIL: %s\n", what);
    }
}

/**
 * Filename for an account.
 *
 * SHA-256 of the account string, NOT the string itself. The account is a URL
 * containing ':' and '/', and on some platforms worse; hashing sidesteps every
 * filesystem-illegal character AND makes path traversal structurally
 * impossible, rather than relying on sanitising a value that arrives from
 * configuration. Pinned by a test that uses "wss://x/../../etc/passwd".
 */
QString fileNameFor(const std::string& account) {
    const QByteArray digest = QCryptographicHash::hash(
        QByteArray::fromStdString(account), QCryptographicHash::Sha256);
    return QString::fromLatin1(digest.toHex()) + QStringLiteral(".tok");
}

class FileTokenStore : public TokenStore {
public:
    explicit FileTokenStore(QString dir) : dir_(std::move(dir)) {}

    bool save(const std::string& account, const std::string& refreshToken) override {
        QDir d(dir_);
        if (!d.exists() && !d.mkpath(QStringLiteral("."))) return false;
        // 0700 on the directory: the file permissions below are pointless if
        // the directory itself is traversable and writable by others.
        QFile::setPermissions(dir_, QFile::ReadOwner | QFile::WriteOwner |
                                        QFile::ExeOwner);

        const QString path = d.filePath(fileNameFor(account));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        // Tighten permissions BEFORE writing: a token must never exist on disk,
        // even briefly, under the default umask.
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        const QByteArray bytes = QByteArray::fromStdString(refreshToken);
        const bool ok = f.write(bytes) == bytes.size();
        f.close();
        return ok;
    }

    std::string load(const std::string& account) override {
        QFile f(QDir(dir_).filePath(fileNameFor(account)));
        if (!f.open(QIODevice::ReadOnly)) return {};   // absent is not an error
        const QByteArray bytes = f.readAll();
        f.close();
        return bytes.toStdString();
    }

    bool erase(const std::string& account) override {
        const QString path = QDir(dir_).filePath(fileNameFor(account));
        if (!QFileInfo::exists(path)) return false;
        return QFile::remove(path);
    }

private:
    QString dir_;
};

}  // namespace

std::unique_ptr<TokenStore> makeFileTokenStore(const std::string& dir) {
    return std::make_unique<FileTokenStore>(QString::fromStdString(dir));
}

#if !defined(__APPLE__) && !defined(_WIN32)
std::unique_ptr<TokenStore> makeTokenStore() {
    // Linux: the file backend, under XDG config. See the header for why this
    // is not libsecret.
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return makeFileTokenStore(base.toStdString());
}
#endif

int tokenStoreSelfTest() {
    g_pass = 0;
    g_fail = 0;

    const QString dir = QDir::temp().filePath(QStringLiteral("jefe-tokenstore-test"));
    QDir(dir).removeRecursively();

    auto store = makeFileTokenStore(dir.toStdString());

    check(store->load("wss://a").empty(), "absent account loads empty");
    check(store->save("wss://a", "refresh-A"), "save succeeds");
    check(store->load("wss://a") == "refresh-A", "round-trips");
    check(store->save("wss://a", "refresh-A2"), "overwrite succeeds");
    check(store->load("wss://a") == "refresh-A2", "overwrite replaces");

    // Per-coordinator isolation: the hosted service and a self-hosted
    // coordinator must never see each other's tokens.
    check(store->save("wss://b", "refresh-B"), "second account saves");
    check(store->load("wss://a") == "refresh-A2", "accounts do not collide");
    check(store->load("wss://b") == "refresh-B", "accounts do not collide (2)");

    // The account is a URL from configuration. It must not be usable as a path.
    const std::string hostile = "wss://x/../../etc/passwd";
    check(store->save(hostile, "t"), "hostile account name saves");
    check(store->load(hostile) == "t", "hostile account name round-trips");
    check(!QFileInfo::exists(QStringLiteral("/etc/passwd.tok")),
          "no traversal outside the store directory");
    // Everything written must live directly in the store dir, one file deep.
    const QStringList entries =
        QDir(dir).entryList(QDir::Files | QDir::NoDotAndDotDot);
    check(entries.size() == 3, "one file per account, all in the store dir");

    // Permissions: owner-only. Skipped on Windows, which has no POSIX bits.
#ifndef _WIN32
    const QString one = QDir(dir).filePath(entries.first());
    const QFile::Permissions perms = QFile(one).permissions();
    check(!(perms & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther |
                     QFile::WriteOther)),
          "token file is not readable by group or other");
#endif

    check(store->erase("wss://a"), "erase reports success");
    check(store->load("wss://a").empty(), "erased account is gone");
    check(!store->erase("wss://a"), "erasing twice reports false");
    check(store->load("wss://b") == "refresh-B", "erase left other accounts alone");

    QDir(dir).removeRecursively();

    // --- native backend ---------------------------------------------------
    // Exercise whatever makeTokenStore() actually returns, so the Keychain /
    // Credential Manager code is not merely compiled but run. Uses a
    // throwaway account and erases it, so it leaves nothing behind in the
    // user's real keychain.
    //
    // On Linux this is the file backend again, which is fine — it then checks
    // that the XDG-rooted construction works, which the temp-dir cases do not.
    {
        auto native = makeTokenStore();
        const std::string acct = "wss://tokenstore-selftest.invalid/dev";
        native->erase(acct);   // leftovers from an interrupted run

        check(native->load(acct).empty(), "native: absent account loads empty");
        check(native->save(acct, "native-A"), "native: save succeeds");
        check(native->load(acct) == "native-A", "native: round-trips");
        // Re-saving is the COMMON path (every token refresh replaces the
        // stored value); on macOS this is the errSecDuplicateItem branch that
        // a naive implementation reports as a failure.
        check(native->save(acct, "native-B"), "native: overwrite succeeds");
        check(native->load(acct) == "native-B", "native: overwrite replaces");
        check(native->erase(acct), "native: erase succeeds");
        check(native->load(acct).empty(), "native: erased account is gone");
    }

    std::printf("TOKENSTORE-TEST: pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}

}  // namespace jefe::auth

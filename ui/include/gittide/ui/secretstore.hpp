#pragma once
#include <QHash>
#include <QString>
#include <qcorotask.h>

namespace gittide::ui {

/// Async access to the OS keychain for secret values — HTTPS tokens and SSH-key
/// passphrases that must never be written to GitTide's own files. Abstract so a
/// headless environment (no keyring) degrades gracefully and tests inject an
/// in-memory implementation.
///
/// All operations run on the UI-thread event loop and are `co_await`-ed *before*
/// a git op is dispatched to the worker, so the worker's synchronous credential
/// callback only ever reads an already-populated `Credentials` POD.
class SecretStore
{
public:
    virtual ~SecretStore() = default;

    /// The secret for `key`, or an empty QString if absent / on backend error.
    virtual QCoro::Task<QString> read(QString key) = 0;
    /// Store (overwrite) the secret; true on success.
    virtual QCoro::Task<bool> write(QString key, QString value) = 0;
    /// Delete the secret; true on success (or if it was already absent).
    virtual QCoro::Task<bool> remove(QString key) = 0;
};

/// SecretStore backed by the platform keychain via QtKeychain (macOS Keychain,
/// Linux libsecret, Windows Credential Store). All secrets share one service
/// name; the `key` is the per-secret account (e.g. "host-token:<id>").
class KeychainSecretStore : public SecretStore
{
public:
    explicit KeychainSecretStore(QString service = QStringLiteral("GitTide"));

    QCoro::Task<QString> read(QString key) override;
    QCoro::Task<bool>    write(QString key, QString value) override;
    QCoro::Task<bool>    remove(QString key) override;

private:
    QString m_service;
};

/// In-memory SecretStore for tests and headless fallback — never persists.
class InMemorySecretStore : public SecretStore
{
public:
    QCoro::Task<QString> read(QString key) override { co_return m_map.value(key); }
    QCoro::Task<bool>    write(QString key, QString value) override
    {
        m_map.insert(key, value);
        co_return true;
    }
    QCoro::Task<bool> remove(QString key) override
    {
        m_map.remove(key);
        co_return true;
    }

private:
    QHash<QString, QString> m_map;
};

} // namespace gittide::ui

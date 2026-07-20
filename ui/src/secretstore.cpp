#include <gittide/ui/secretstore.hpp>

#include <memory>

#include <qcorosignal.h>

#if __has_include(<qt6keychain/keychain.h>)
#  include <qt6keychain/keychain.h>
#else
#  include <keychain.h>
#endif

#include "gittide/log.hpp"

namespace gittide::ui {

KeychainSecretStore::KeychainSecretStore(QString service)
    : m_service(std::move(service))
{
}

QCoro::Task<QString> KeychainSecretStore::read(QString key)
{
    auto job = std::make_unique<QKeychain::ReadPasswordJob>(m_service);
    job->setAutoDelete(false);
    job->setKey(key);
    job->start();
    co_await qCoro(job.get(), &QKeychain::Job::finished);

    if (job->error() != QKeychain::NoError)
    {
        // Absent is normal (first use); anything else is a backend problem worth a note.
        if (job->error() != QKeychain::EntryNotFound)
            logf(LogLevel::Debug, logcat::AUTH, "keychain read '{}' failed: {}", key.toStdString(),
                 job->errorString().toStdString());
        co_return QString();
    }
    co_return job->textData();
}

QCoro::Task<bool> KeychainSecretStore::write(QString key, QString value)
{
    auto job = std::make_unique<QKeychain::WritePasswordJob>(m_service);
    job->setAutoDelete(false);
    job->setKey(key);
    job->setTextData(value);
    job->start();
    co_await qCoro(job.get(), &QKeychain::Job::finished);

    if (job->error() != QKeychain::NoError)
    {
        logf(LogLevel::Warning, logcat::AUTH, "keychain write '{}' failed: {}", key.toStdString(),
             job->errorString().toStdString());
        co_return false;
    }
    co_return true;
}

QCoro::Task<bool> KeychainSecretStore::remove(QString key)
{
    auto job = std::make_unique<QKeychain::DeletePasswordJob>(m_service);
    job->setAutoDelete(false);
    job->setKey(key);
    job->start();
    co_await qCoro(job.get(), &QKeychain::Job::finished);

    if (job->error() != QKeychain::NoError && job->error() != QKeychain::EntryNotFound)
    {
        logf(LogLevel::Warning, logcat::AUTH, "keychain delete '{}' failed: {}", key.toStdString(),
             job->errorString().toStdString());
        co_return false;
    }
    co_return true;
}

} // namespace gittide::ui

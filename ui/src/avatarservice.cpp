#include "gittide/ui/avatarservice.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <qcorosignal.h>

namespace gittide::ui {

namespace {
// Disk-cache freshness. A hit is trusted for a week; a "no gravatar" miss is
// re-probed sooner in case the author registers one.
constexpr qint64 kHitTtlSecs  = 7 * 24 * 60 * 60;
constexpr qint64 kMissTtlSecs = 24 * 60 * 60;
// Fetch well above the ~24px display size so a HiDPI (Retina, DPR 2–3) row
// downscales a crisp image instead of upscaling a soft one. A 128px PNG is a few
// KB and is cached, so the headroom is nearly free.
constexpr int    kSize        = 128;
} // namespace

AvatarService::AvatarService(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

QString AvatarService::emailHash(const QString& email) const
{
    const QByteArray norm = email.trimmed().toLower().toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(norm, QCryptographicHash::Md5).toHex());
}

void AvatarService::setNetworkEnabled(bool on)
{
    if (m_networkEnabled == on)
        return;
    m_networkEnabled = on;
    emit networkEnabledChanged();
}

QString AvatarService::cachePath(const QString& hash) const
{
    return m_cacheDir + QLatin1Char('/') + hash + QStringLiteral(".png");
}

QString AvatarService::missPath(const QString& hash) const
{
    return m_cacheDir + QLatin1Char('/') + hash + QStringLiteral(".miss");
}

QCoro::Task<QImage> AvatarService::avatarFor(QString email)
{
    if (email.trimmed().isEmpty())
        co_return QImage();
    co_return co_await avatarForHash(emailHash(email));
}

QCoro::Task<QImage> AvatarService::avatarForHash(QString hash)
{
    if (hash.isEmpty())
        co_return QImage();

    // 1. In-memory cache.
    if (const auto it = m_memCache.constFind(hash); it != m_memCache.constEnd())
        co_return it.value();

    // 2. On-disk cache, TTL-checked (a hit, or a fresh negative marker).
    if (!m_cacheDir.isEmpty())
    {
        const QDateTime now = QDateTime::currentDateTime();
        const QFileInfo hit(cachePath(hash));
        if (hit.exists() && hit.lastModified().secsTo(now) < kHitTtlSecs)
        {
            QImage img;
            if (img.load(cachePath(hash)))
            {
                m_memCache.insert(hash, img);
                co_return img;
            }
        }
        const QFileInfo miss(missPath(hash));
        if (miss.exists() && miss.lastModified().secsTo(now) < kMissTtlSecs)
            co_return QImage();
    }

    // 3. Network (Gravatar), unless disabled.
    if (!m_networkEnabled)
        co_return QImage();

    QImage img = co_await fetchFromNetwork(hash);
    if (!img.isNull())
        m_memCache.insert(hash, img);
    co_return img;
}

QCoro::Task<QImage> AvatarService::fetchFromNetwork(QString hash)
{
    // Probe with d=404 so a missing avatar returns 404 (rather than Gravatar's
    // default image); on 404 retry with d=identicon, which always renders.
    const QString probeUrl =
        m_gravatarBase + QLatin1Char('/') + hash + QStringLiteral("?d=404&s=%1").arg(kSize);

    auto fetch = [this](const QString& url) -> QCoro::Task<QNetworkReply*>
    {
        QNetworkRequest req{QUrl(url)};
        req.setRawHeader("User-Agent", "GitTide");
        QNetworkReply* reply = m_nam->get(req);
        co_await qCoro(reply, &QNetworkReply::finished);
        co_return reply;
    };

    QNetworkReply* reply = co_await fetch(probeUrl);
    int            status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray     body   = reply->readAll();
    reply->deleteLater();

    QImage img;
    if (status == 200 && img.loadFromData(body))
    {
        writeDiskCache(hash, body);
        co_return img;
    }

    if (status == 404)
    {
        const QString identiconUrl =
            m_gravatarBase + QLatin1Char('/') + hash + QStringLiteral("?d=identicon&s=%1").arg(kSize);
        QNetworkReply* reply2 = co_await fetch(identiconUrl);
        const int      status2 = reply2->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body2 = reply2->readAll();
        reply2->deleteLater();
        if (status2 == 200 && img.loadFromData(body2))
        {
            writeDiskCache(hash, body2);
            co_return img;
        }
    }

    // Nothing usable — record a negative marker so we don't re-probe every scroll.
    if (!m_cacheDir.isEmpty())
    {
        QDir().mkpath(m_cacheDir);
        QFile f(missPath(hash));
        if (f.open(QIODevice::WriteOnly))
            f.close();
    }
    co_return QImage();
}

void AvatarService::writeDiskCache(const QString& hash, const QByteArray& png)
{
    if (m_cacheDir.isEmpty())
        return;
    QDir().mkpath(m_cacheDir);
    QFile f(cachePath(hash));
    if (f.open(QIODevice::WriteOnly))
    {
        f.write(png);
        f.close();
    }
    // Drop any stale negative marker.
    QFile::remove(missPath(hash));
}

} // namespace gittide::ui

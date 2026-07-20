#pragma once
#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <qcorotask.h>

class QNetworkAccessManager;

namespace gittide::ui {

/// Resolves a commit author's email to an avatar image, for the History rows.
///
/// `ui/`-only (avatars are not a `core/` concern — no Qt in core, and core stays
/// offline/deterministic for tests). Resolution order, first hit wins:
///   1. in-memory cache (keyed by md5(email));
///   2. on-disk cache under the cache dir (with a TTL, plus a negative-cache
///      marker so a missing avatar is not re-probed on every scroll);
///   3. the network (Gravatar `d=404` probe → on 404, `d=identicon` which always
///      renders), gated by `networkEnabled`.
/// A null `QImage` means "no avatar" — the QML `Avatar` shows its initials disc.
///
/// v1 is Gravatar-only; forge (GitHub/GitLab) sources are a later increment that
/// prepends a step to the chain. Repeat authors resolve from cache, and the QML
/// `QQuickPixmapCache` dedups concurrent identical `image://avatar/<hash>` sources
/// across virtualized rows, so a large history never stampedes the network.
/// Network access and the cache dir are injectable so offscreen tests never touch
/// the real network or the user's cache.
class AvatarService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool networkEnabled READ networkEnabled WRITE setNetworkEnabled NOTIFY networkEnabledChanged)
public:
    explicit AvatarService(QObject* parent = nullptr);

    /// Resolve the avatar for `email`. Never throws; yields a null `QImage` for an
    /// empty email, when network is disabled with nothing cached, or on failure.
    QCoro::Task<QImage> avatarFor(QString email);

    /// Resolve directly from a precomputed md5 hash (what the image provider holds
    /// as the request id). Same chain as avatarFor minus the email→hash step.
    QCoro::Task<QImage> avatarForHash(QString hash);

    /// md5 of the trimmed, lowercased email as lowercase hex — the Gravatar key
    /// and the `image://avatar/<hash>` provider id. Invokable so QML can build the
    /// image source URL for a row's `authorEmail`.
    Q_INVOKABLE QString emailHash(const QString& email) const;

    bool networkEnabled() const { return m_networkEnabled; }
    void setNetworkEnabled(bool on);

    /// Test/composition injection points.
    void setNetworkManager(QNetworkAccessManager* nam) { m_nam = nam; }
    void setCacheDir(const QString& dir) { m_cacheDir = dir; }
    void setGravatarBase(const QString& base) { m_gravatarBase = base; } // e.g. https://www.gravatar.com/avatar

signals:
    void networkEnabledChanged();

private:
    QCoro::Task<QImage> fetchFromNetwork(QString hash);
    void                writeDiskCache(const QString& hash, const QByteArray& png);
    QString             cachePath(const QString& hash) const;
    QString             missPath(const QString& hash) const;

    QNetworkAccessManager* m_nam;
    QString                m_cacheDir;
    QString                m_gravatarBase   = QStringLiteral("https://www.gravatar.com/avatar");
    bool                   m_networkEnabled = true;
    QHash<QString, QImage> m_memCache; // hash → decoded image
};

} // namespace gittide::ui

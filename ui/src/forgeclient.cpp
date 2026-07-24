#include <gittide/ui/forgeclient.hpp>

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <chrono>
#include <qcorosignal.h>

namespace gittide::ui {

ForgeClient::ForgeClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

QCoro::Task<ForgeAccount> ForgeClient::validate(QString kind, QString apiBase, QString token)
{
    ForgeAccount acc;

    if (apiBase.isEmpty())
        apiBase = (kind == QLatin1String("gitlab")) ? QStringLiteral("https://gitlab.com/api/v4")
                                                     : QStringLiteral("https://api.github.com");

    QNetworkRequest req{QUrl(apiBase + QStringLiteral("/user"))};
    req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token.toUtf8());
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("User-Agent", "GitTide");
    // Per-request (not on the NAM) so an injected manager is bounded too: an
    // unresponsive forge cannot hang token validation forever.
    req.setTransferTimeout(std::chrono::seconds(30));

    QNetworkReply* reply = m_nam->get(req);
    co_await qCoro(reply, &QNetworkReply::finished);
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError)
    {
        acc.error = (status == 401 || status == 403) ? QStringLiteral("Token rejected (%1)").arg(status)
                                                     : reply->errorString();
        co_return acc;
    }

    QJsonParseError    pe;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
    {
        acc.error = QStringLiteral("Unexpected API response");
        co_return acc;
    }

    const QJsonObject o = doc.object();
    acc.login = (kind == QLatin1String("gitlab")) ? o.value(QStringLiteral("username")).toString()
                                                   : o.value(QStringLiteral("login")).toString();
    acc.name  = o.value(QStringLiteral("name")).toString();
    acc.email = o.value(QStringLiteral("email")).toString();
    acc.ok    = !acc.login.isEmpty();
    if (!acc.ok)
        acc.error = QStringLiteral("API response missing a login");
    co_return acc;
}

} // namespace gittide::ui

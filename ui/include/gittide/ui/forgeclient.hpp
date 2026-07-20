#pragma once
#include <QObject>
#include <QString>
#include <qcorotask.h>

class QNetworkAccessManager;

namespace gittide::ui {

/// Result of validating a forge token against the host API.
struct ForgeAccount
{
    bool    ok = false;
    QString login; // github "login" / gitlab "username"
    QString name;
    QString email; // may be empty (private / unset)
    QString error; // human-readable reason when !ok
};

/// Validates a GitHub/GitLab (or self-hosted) personal-access token against the
/// host's REST API and returns the account identity. `ui/`-only — uses
/// `QNetworkAccessManager` + `QJsonDocument` (nlohmann stays private to `core/`).
class ForgeClient : public QObject
{
    Q_OBJECT
public:
    explicit ForgeClient(QObject* parent = nullptr);

    /// GET {apiBase}/user with a bearer token; parses login/name/email per `kind`
    /// ("github" | "gitlab"; empty apiBase defaults per kind). Never throws — a
    /// failure yields `ForgeAccount{ok=false, error=...}`.
    QCoro::Task<ForgeAccount> validate(QString kind, QString apiBase, QString token);

    /// Inject a network manager (tests point it at a local server).
    void setNetworkManager(QNetworkAccessManager* nam) { m_nam = nam; }

private:
    QNetworkAccessManager* m_nam;
};

} // namespace gittide::ui

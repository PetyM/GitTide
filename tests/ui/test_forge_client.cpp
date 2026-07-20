#include <QtTest/QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <qcorotask.h>

#include "gittide/ui/forgeclient.hpp"

using gittide::ui::ForgeAccount;
using gittide::ui::ForgeClient;

namespace {
// Minimal one-shot HTTP server: replies to any request with a fixed status + body.
class FakeHttp : public QObject
{
public:
    FakeHttp(int status, QByteArray json)
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, this,
                         [this, status, json]
                         {
                             QTcpSocket* s = m_server.nextPendingConnection();
                             QObject::connect(s, &QTcpSocket::readyRead, s,
                                              [s, status, json]
                                              {
                                                  s->readAll(); // ignore the request
                                                  QByteArray resp = "HTTP/1.1 " + QByteArray::number(status) + " X\r\n";
                                                  resp += "Content-Type: application/json\r\n";
                                                  resp += "Content-Length: " + QByteArray::number(json.size()) + "\r\n";
                                                  resp += "Connection: close\r\n\r\n";
                                                  resp += json;
                                                  s->write(resp);
                                                  s->flush();
                                                  s->disconnectFromHost();
                                              });
                         });
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }

private:
    QTcpServer m_server;
};
} // namespace

class TestForgeClient : public QObject
{
    Q_OBJECT
private slots:
    void github_validate_parses_login_name_email()
    {
        FakeHttp http(200, R"({"login":"octocat","name":"The Octocat","email":"o@x.com"})");
        ForgeClient  client;
        ForgeAccount acc = QCoro::waitFor(client.validate(QStringLiteral("github"), http.base(), QStringLiteral("tok")));
        QVERIFY(acc.ok);
        QCOMPARE(acc.login, QStringLiteral("octocat"));
        QCOMPARE(acc.name, QStringLiteral("The Octocat"));
        QCOMPARE(acc.email, QStringLiteral("o@x.com"));
    }

    void gitlab_validate_uses_username_field()
    {
        FakeHttp http(200, R"({"username":"tanuki","name":"Tanuki","email":"t@x.com"})");
        ForgeClient  client;
        ForgeAccount acc = QCoro::waitFor(client.validate(QStringLiteral("gitlab"), http.base(), QStringLiteral("tok")));
        QVERIFY(acc.ok);
        QCOMPARE(acc.login, QStringLiteral("tanuki"));
    }

    void unauthorized_is_reported_not_ok()
    {
        FakeHttp http(401, R"({"message":"Bad credentials"})");
        ForgeClient  client;
        ForgeAccount acc = QCoro::waitFor(client.validate(QStringLiteral("github"), http.base(), QStringLiteral("bad")));
        QVERIFY(!acc.ok);
        QVERIFY(acc.error.contains(QStringLiteral("401")));
    }
};

#include "test_forge_client.moc"

#include <QtTest/QtTest>
#include <QBuffer>
#include <QImage>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <qcorotask.h>

#include <QQuickImageResponse>
#include <QQuickTextureFactory>

#include "gittide/ui/avatarimageprovider.hpp"
#include "gittide/ui/avatarservice.hpp"

using gittide::ui::AvatarImageProvider;
using gittide::ui::AvatarService;

namespace {
// One PNG byte-blob shared by the fake servers (a 2x2 red image).
QByteArray onePng()
{
    QImage img(2, 2, QImage::Format_ARGB32);
    img.fill(Qt::red);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

// Counting HTTP server. Replies 404 to any request whose path/query contains
// "d=404" (the Gravatar "no image" probe), else 200 + a PNG. Tracks request count.
class FakeAvatarHttp : public QObject
{
public:
    FakeAvatarHttp()
    {
        m_png = onePng();
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, this,
                         [this]
                         {
                             QTcpSocket* s = m_server.nextPendingConnection();
                             QObject::connect(s, &QTcpSocket::readyRead, s,
                                              [this, s]
                                              {
                                                  const QByteArray req = s->readAll();
                                                  ++m_requests;
                                                  const bool probe404 = req.contains("d=404");
                                                  QByteArray body = probe404 ? QByteArray() : m_png;
                                                  int status      = probe404 ? 404 : 200;
                                                  QByteArray resp = "HTTP/1.1 " + QByteArray::number(status) + " X\r\n";
                                                  resp += "Content-Type: image/png\r\n";
                                                  resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
                                                  resp += "Connection: close\r\n\r\n";
                                                  resp += body;
                                                  s->write(resp);
                                                  s->flush();
                                                  s->disconnectFromHost();
                                              });
                         });
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1/avatar").arg(m_server.serverPort()); }
    int requests() const { return m_requests; }

private:
    QTcpServer m_server;
    QByteArray m_png;
    int        m_requests = 0;
};
} // namespace

class TestAvatarService : public QObject
{
    Q_OBJECT
private slots:
    void email_hash_is_md5_of_normalized_email()
    {
        // Gravatar hashes the trimmed, lowercased email. Known MD5 vector.
        AvatarService svc;
        QCOMPARE(svc.emailHash(QStringLiteral("  MyEmailAddress@example.com ")),
                 svc.emailHash(QStringLiteral("myemailaddress@example.com")));
        QCOMPARE(svc.emailHash(QStringLiteral("myemailaddress@example.com")),
                 QStringLiteral("0bc83cb571cd1c50ba6f3e8a78ef1346"));
    }

    void disabled_network_returns_null_and_makes_no_request()
    {
        QTemporaryDir dir;
        FakeAvatarHttp http;
        AvatarService svc;
        svc.setCacheDir(dir.path());
        svc.setGravatarBase(http.base());
        svc.setNetworkEnabled(false);

        QImage img = QCoro::waitFor(svc.avatarFor(QStringLiteral("a@b.com")));
        QVERIFY(img.isNull());
        QCOMPARE(http.requests(), 0);
    }

    void fetches_then_serves_from_memory_cache()
    {
        QTemporaryDir dir;
        FakeAvatarHttp http;
        AvatarService svc;
        svc.setCacheDir(dir.path());
        svc.setGravatarBase(http.base());

        QImage first = QCoro::waitFor(svc.avatarFor(QStringLiteral("a@b.com")));
        QVERIFY(!first.isNull());
        const int afterFirst = http.requests();
        QVERIFY(afterFirst >= 1);

        // Second call for the same email → served from cache, no new request.
        QImage second = QCoro::waitFor(svc.avatarFor(QStringLiteral("a@b.com")));
        QVERIFY(!second.isNull());
        QCOMPARE(http.requests(), afterFirst);
    }

    void falls_back_to_identicon_on_404()
    {
        QTemporaryDir dir;
        FakeAvatarHttp http; // 404s the d=404 probe, 200s everything else
        AvatarService svc;
        svc.setCacheDir(dir.path());
        svc.setGravatarBase(http.base());

        QImage img = QCoro::waitFor(svc.avatarFor(QStringLiteral("nobody@nowhere.com")));
        QVERIFY(!img.isNull());
        // Two requests: the d=404 probe (404) then the d=identicon fetch (200).
        QCOMPARE(http.requests(), 2);
    }

    void persists_to_disk_cache_across_instances()
    {
        QTemporaryDir dir;
        FakeAvatarHttp http;
        {
            AvatarService svc;
            svc.setCacheDir(dir.path());
            svc.setGravatarBase(http.base());
            QImage img = QCoro::waitFor(svc.avatarFor(QStringLiteral("a@b.com")));
            QVERIFY(!img.isNull());
        }
        const int afterWarm = http.requests();

        // A fresh service over the same cache dir serves from disk with no network.
        AvatarService svc2;
        svc2.setCacheDir(dir.path());
        svc2.setGravatarBase(http.base());
        QImage img = QCoro::waitFor(svc2.avatarFor(QStringLiteral("a@b.com")));
        QVERIFY(!img.isNull());
        QCOMPARE(http.requests(), afterWarm);
    }

    void provider_resolves_a_hash_to_an_image()
    {
        QTemporaryDir dir;
        FakeAvatarHttp http;
        AvatarService svc;
        svc.setCacheDir(dir.path());
        svc.setGravatarBase(http.base());

        AvatarImageProvider provider(&svc);
        const QString hash = svc.emailHash(QStringLiteral("a@b.com"));
        QQuickImageResponse* resp = provider.requestImageResponse(hash, QSize(24, 24));
        QVERIFY(resp != nullptr);

        QSignalSpy done(resp, &QQuickImageResponse::finished);
        QVERIFY(done.wait(15000));

        QScopedPointer<QQuickTextureFactory> tex(resp->textureFactory());
        QVERIFY(!tex.isNull());
        QVERIFY(!tex->textureSize().isEmpty());
    }

    void provider_yields_transparent_image_when_disabled()
    {
        QTemporaryDir dir;
        FakeAvatarHttp http;
        AvatarService svc;
        svc.setCacheDir(dir.path());
        svc.setGravatarBase(http.base());
        svc.setNetworkEnabled(false);

        AvatarImageProvider provider(&svc);
        QQuickImageResponse* resp = provider.requestImageResponse(svc.emailHash(QStringLiteral("a@b.com")), QSize());
        QSignalSpy done(resp, &QQuickImageResponse::finished);
        QVERIFY(done.wait(15000));
        // Null image → no texture factory → initials show through in QML.
        QScopedPointer<QQuickTextureFactory> tex(resp->textureFactory());
        QVERIFY(tex.isNull());
        QCOMPARE(http.requests(), 0);
    }
};

#include "test_avatar_service.moc"

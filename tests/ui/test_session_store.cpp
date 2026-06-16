#include <QObject>
#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "gitgui/ui/SessionStore.hpp"

using gitgui::ui::SessionStore;
using gitgui::ui::WindowSession;

class TestSessionStore : public QObject {
    Q_OBJECT
private slots:
    void json_round_trip_preserves_windows() {
        SessionStore s;
        s.windows.push_back(WindowSession{
            .projectId = "id-a",
            .geometry = QByteArray("GEOM\x00\x01", 6),
            .lastActiveRepo = "/home/u/api"});

        const QByteArray json = s.toJson();
        const SessionStore back = SessionStore::fromJson(json);

        QCOMPARE(back.windows.size(), std::size_t(1));
        QCOMPARE(back.windows[0].projectId, QStringLiteral("id-a"));
        QCOMPARE(back.windows[0].geometry, QByteArray("GEOM\x00\x01", 6));
        QCOMPARE(back.windows[0].lastActiveRepo, QStringLiteral("/home/u/api"));
    }

    void corrupt_json_yields_empty_store() {
        const SessionStore back = SessionStore::fromJson(QByteArray("{not json"));
        QCOMPARE(back.windows.size(), std::size_t(0));
    }

    void save_then_load_from_disk() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.filePath(QStringLiteral("session.json"));

        SessionStore s;
        s.windows.push_back(WindowSession{
            .projectId = "id-b", .geometry = QByteArray(), .lastActiveRepo = ""});
        QVERIFY(s.save(file));

        const SessionStore back = SessionStore::load(file);
        QCOMPARE(back.windows.size(), std::size_t(1));
        QCOMPARE(back.windows[0].projectId, QStringLiteral("id-b"));
    }

    void load_missing_file_yields_empty_store() {
        const SessionStore back = SessionStore::load(QStringLiteral("/no/such/session.json"));
        QCOMPARE(back.windows.size(), std::size_t(0));
    }
};

#include "test_session_store.moc"

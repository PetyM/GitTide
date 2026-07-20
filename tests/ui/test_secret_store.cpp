#include <QtTest/QtTest>
#include <qcorotask.h>

#include "gittide/ui/secretstore.hpp"

using gittide::ui::InMemorySecretStore;

class TestSecretStore : public QObject
{
    Q_OBJECT
private slots:
    void in_memory_read_write_remove()
    {
        InMemorySecretStore s;
        QVERIFY(QCoro::waitFor(s.read(QStringLiteral("k"))).isEmpty());
        QVERIFY(QCoro::waitFor(s.write(QStringLiteral("k"), QStringLiteral("secret"))));
        QCOMPARE(QCoro::waitFor(s.read(QStringLiteral("k"))), QStringLiteral("secret"));
        QVERIFY(QCoro::waitFor(s.remove(QStringLiteral("k"))));
        QVERIFY(QCoro::waitFor(s.read(QStringLiteral("k"))).isEmpty());
    }

    // NB: no automated round-trip against the real KeychainSecretStore — a real
    // keychain op can block on an OS access-permission prompt that a headless
    // offscreen run cannot answer (it hangs). The platform glue is thin QtKeychain
    // and is verified manually per the plan's Phase 2 checklist.
};

#include "test_secret_store.moc"

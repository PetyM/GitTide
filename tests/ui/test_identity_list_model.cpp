#include <QAbstractItemModelTester>
#include <QtTest/QtTest>

#include "gittide/credentialsstore.hpp"
#include "gittide/ui/identitylistmodel.hpp"

using gittide::CredentialsStore;
using gittide::ui::IdentityListModel;

class TestIdentityListModel : public QObject
{
    Q_OBJECT
private slots:
    void exposes_name_email_id_and_global()
    {
        CredentialsStore store;
        auto&            a = store.addIdentity("Work Me", "me@work.com");
        store.addIdentity("Home Me", "me@home.com");
        store.setGlobalIdentity(a.id);

        IdentityListModel        model(&store);
        QAbstractItemModelTester tester(&model); // validates model correctness
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0), IdentityListModel::NameRole).toString(), QStringLiteral("Work Me"));
        QCOMPARE(model.data(model.index(0), IdentityListModel::EmailRole).toString(), QStringLiteral("me@work.com"));
        QCOMPARE(model.data(model.index(0), IdentityListModel::IdRole).toString(), QString::fromStdString(a.id));
        QVERIFY(model.data(model.index(0), IdentityListModel::IsGlobalRole).toBool());
        QVERIFY(!model.data(model.index(1), IdentityListModel::IsGlobalRole).toBool());
    }

    void refresh_reflects_additions()
    {
        CredentialsStore  store;
        IdentityListModel model(&store);
        QCOMPARE(model.rowCount(), 0);
        store.addIdentity("X", "x@y.z");
        model.refresh();
        QCOMPARE(model.rowCount(), 1);
    }
};

#include "test_identity_list_model.moc"

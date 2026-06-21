#include <QtTest>
#include <QSignalSpy>

#include "gittide/ui/repoviewmodel.hpp"
#include "support/temprepo.hpp"

using namespace gittide::ui;

class TestQmlSync : public QObject
{
    Q_OBJECT
private slots:
    void aheadBehindMapToProperties();
    void pullRebaseRoundTrips();
};

void TestQmlSync::aheadBehindMapToProperties()
{
    using namespace gittide::test;
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    repo.addBareRemote("origin");
    repo.pushBranch("origin", "master");
    repo.writeFile("a.txt", "two");
    repo.commitAll("c2"); // ahead 1

    RepoViewModel vm;
    vm.open(QString::fromStdString(repo.path().generic_string()));
    QTRY_COMPARE_WITH_TIMEOUT(vm.aheadCount(), 1, 5000);
    QCOMPARE(vm.behindCount(), 0);
    QVERIFY(vm.hasUpstream());
}

void TestQmlSync::pullRebaseRoundTrips()
{
    using namespace gittide::test;
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");

    RepoViewModel vm;
    vm.open(QString::fromStdString(repo.path().generic_string()));
    vm.setPullRebase(true);
    QTRY_VERIFY_WITH_TIMEOUT(vm.pullRebase(), 5000);
}

#include "test_qml_sync.moc"

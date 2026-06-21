#include <QtTest>
#include <QSignalSpy>

#include "gittide/gitrepo.hpp"
#include "gittide/ui/historylistmodel.hpp"
#include "gittide/ui/repoviewmodel.hpp"
#include "support/temprepo.hpp"

using namespace gittide::ui;

class TestQmlSync : public QObject
{
    Q_OBJECT
private slots:
    void aheadBehindMapToProperties();
    void pullRebaseRoundTrips();
    void detachedHeadCannotPublish();
    void checkoutRemoteBranchCreatesTrackingLocal();
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

void TestQmlSync::detachedHeadCannotPublish()
{
    using namespace gittide::test;
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    repo.writeFile("a.txt", "two");
    repo.commitAll("c2");

    RepoViewModel vm;
    vm.open(QString::fromStdString(repo.path().generic_string()));

    // Wait until history is loaded so we can retrieve oids.
    QSignalSpy historySpy(vm.history(), &QAbstractItemModel::modelReset);
    QVERIFY(historySpy.wait(5000));
    QVERIFY(vm.history()->rowCount(QModelIndex()) >= 2);

    // Initially on a named branch.
    QTRY_VERIFY_WITH_TIMEOUT(vm.onBranch(), 5000);

    // Check out the first (older) commit — row 1 in the history model — to get a detached HEAD.
    const QString c1Oid = vm.history()->data(vm.history()->index(1, 0), HistoryListModel::OidRole).toString();
    QVERIFY(!c1Oid.isEmpty());

    QSignalSpy branchSpy(&vm, &RepoViewModel::branchChanged);
    vm.checkoutCommit(c1Oid);
    QVERIFY(branchSpy.wait(5000));

    // Now detached — onBranch must be false.
    QTRY_VERIFY_WITH_TIMEOUT(!vm.onBranch(), 5000);

    // publishBranch() must not crash and must emit operationFailed instead of pushing.
    QSignalSpy failSpy(&vm, &RepoViewModel::operationFailed);
    vm.publishBranch();
    // The guard is synchronous — the signal fires immediately (no wait needed).
    QCOMPARE(failSpy.count(), 1);
    QVERIFY(failSpy.at(0).at(0).toString().contains(QStringLiteral("detached")));
    // onBranch must still be false.
    QVERIFY(!vm.onBranch());
}

void TestQmlSync::checkoutRemoteBranchCreatesTrackingLocal()
{
    using namespace gittide::test;

    // Source repo with master + a remote-only "feature" branch, pushed to a bare.
    TempRepo src;
    src.setIdentity("Test", "test@example.com");
    src.writeFile("a.txt", "one");
    src.commitAll("c1");
    auto bare = src.addBareRemote("origin");
    src.pushBranch("origin", "master");
    auto srcRepo = gittide::GitRepo::open(src.path());
    QVERIFY(srcRepo.has_value());
    QVERIFY(srcRepo->createBranch("feature", "").has_value());
    src.pushBranch("origin", "feature");

    // A fresh clone has refs/remotes/origin/feature but no local "feature".
    TempRepo clone;
    clone.cloneFrom(bare);

    RepoViewModel vm;
    vm.open(QString::fromStdString(clone.path().generic_string()));
    QTRY_VERIFY_WITH_TIMEOUT(vm.onBranch(), 5000);

    QSignalSpy branchSpy(&vm, &RepoViewModel::branchChanged);
    vm.checkoutRemoteBranch(QStringLiteral("origin/feature"));

    // The DWIM checkout creates a local "feature" tracking origin/feature.
    QTRY_COMPARE_WITH_TIMEOUT(vm.currentBranch(), QStringLiteral("feature"), 5000);
    QVERIFY(vm.onBranch());
    QTRY_VERIFY_WITH_TIMEOUT(vm.hasUpstream(), 5000);
    QCOMPARE(vm.aheadCount(), 0);
    QCOMPARE(vm.behindCount(), 0);
}

#include "test_qml_sync.moc"

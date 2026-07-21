// Tests for the title-bar menu bar components AppMenuBar.qml + MenuBarButton.qml
// (Plan 29, Task 7). The bar is not yet mounted into TitleBar (Task 8), so this
// test instantiates AppMenuBar.qml DIRECTLY via QQmlComponent — loaded by its QRC
// URL so same-dir App* types (AppMenu/AppMenuItem) resolve — with a theme context
// property and a stub repo/appSettings. It asserts the three menu-bar buttons exist
// and each owns its AppMenu.
//
// What is tested:
//   AppMenuBar instantiates and exposes File/Edit/Repository MenuBarButtons,
//   each with a non-null `menu`.

#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QObject>
#include <memory>

#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/repolistmodel.hpp"
#include "gittide/ui/thememanager.hpp"

using gittide::ui::QmlTheme;
using gittide::ui::RepoListModel;
using gittide::ui::ThemeManager;

// StubBranchModelR / StubHistoryModelR are declared in
// test_qml_rebase_entrypoints.cpp, which is #included before this file in the
// single-translation-unit tests/ui/main.cpp aggregator — reuse them here so the
// Main.qml-loading test below has live branches/history models.

// ---------------------------------------------------------------------------
// Repo stub. Exposes the enable-binding properties AppMenuBar reads plus the
// per-repo action invokables/counters; and — for the mounted-bar test that loads
// Main.qml — the full property surface Main.qml's subtree binds to (mirrors
// RebaseEntryStub). The bar's action signals are wired to the repo by the host
// (TitleBar) and Main.qml.
// ---------------------------------------------------------------------------
class MenuBarRepoStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool repoOpen         READ repoOpen         CONSTANT)
    Q_PROPERTY(bool rebaseInProgress READ rebaseInProgress CONSTANT)
    Q_PROPERTY(bool mergeInProgress  READ mergeInProgress  CONSTANT)
    Q_PROPERTY(bool dirty            READ dirty            CONSTANT)
    Q_PROPERTY(bool stashAvailable   READ stashAvailable   CONSTANT)
    // --- Remaining surface Main.qml's subtree binds to (mirrors RebaseEntryStub). ---
    Q_PROPERTY(QString mergedRef         READ mergedRef            CONSTANT)
    Q_PROPERTY(int conflictedCount       READ conflictedCount      CONSTANT)
    Q_PROPERTY(bool hasSubmoduleConflicts READ hasSubmoduleConflicts CONSTANT)
    Q_PROPERTY(QString rebaseOnto        READ rebaseOnto           CONSTANT)
    Q_PROPERTY(int rebaseStep            READ rebaseStep           CONSTANT)
    Q_PROPERTY(int rebaseTotal           READ rebaseTotal          CONSTANT)
    Q_PROPERTY(QString rebaseStepSummary READ rebaseStepSummary    CONSTANT)
    Q_PROPERTY(int rebaseConflictedCount READ rebaseConflictedCount CONSTANT)
    Q_PROPERTY(bool rebaseHasSubmoduleConflicts READ rebaseHasSubmoduleConflicts CONSTANT)
    Q_PROPERTY(QString currentBranch     READ currentBranch        NOTIFY stubChanged)
    Q_PROPERTY(QObject* branches         READ branches             CONSTANT)
    Q_PROPERTY(QObject* history          READ history              CONSTANT)
    Q_PROPERTY(QObject* changedFiles     READ changedFiles         CONSTANT)
    Q_PROPERTY(QObject* diffLines        READ diffLines            CONSTANT)
    Q_PROPERTY(int checkedCount          READ checkedCount         CONSTANT)
    Q_PROPERTY(int aheadCount            READ aheadCount           CONSTANT)
    Q_PROPERTY(int behindCount           READ behindCount          CONSTANT)
    Q_PROPERTY(bool hasUpstream          READ hasUpstream          CONSTANT)
    Q_PROPERTY(QString upstreamName      READ upstreamName         CONSTANT)
    Q_PROPERTY(bool syncing              READ syncing              CONSTANT)
    Q_PROPERTY(bool onBranch             READ onBranch             CONSTANT)
    Q_PROPERTY(bool pullRebase           READ pullRebase           CONSTANT)
    Q_PROPERTY(qreal syncProgress        READ syncProgress         CONSTANT)
    Q_PROPERTY(int syncReceived          READ syncReceived         CONSTANT)
    Q_PROPERTY(int syncTotal             READ syncTotal            CONSTANT)
    Q_PROPERTY(QString repoPath          READ repoPath             CONSTANT)
    Q_PROPERTY(QString activeFile        READ activeFile           CONSTANT)
    Q_PROPERTY(QString selectedCommit    READ selectedCommit       CONSTANT)
    Q_PROPERTY(QString activeCommitFile  READ activeCommitFile     CONSTANT)
    Q_PROPERTY(QObject* commitFiles      READ commitFiles          CONSTANT)
    Q_PROPERTY(QObject* commitDiff       READ commitDiff           CONSTANT)
public:
    explicit MenuBarRepoStub(QObject* parent = nullptr)
        : QObject(parent)
        , m_branchModel(new StubBranchModelR(this))
        , m_historyModel(new StubHistoryModelR(this))
    {
    }

    bool repoOpen() const { return true; }
    bool rebaseInProgress() const { return false; }
    bool mergeInProgress() const { return false; }
    bool dirty() const { return true; }
    bool stashAvailable() const { return true; }

    QString mergedRef() const { return {}; }
    int conflictedCount() const { return 0; }
    bool hasSubmoduleConflicts() const { return false; }
    QString rebaseOnto() const { return {}; }
    int rebaseStep() const { return 0; }
    int rebaseTotal() const { return 0; }
    QString rebaseStepSummary() const { return {}; }
    int rebaseConflictedCount() const { return 0; }
    bool rebaseHasSubmoduleConflicts() const { return false; }
    QString currentBranch() const { return QStringLiteral("main"); }
    QObject* branches() const { return m_branchModel; }
    QObject* history() const { return m_historyModel; }
    QObject* changedFiles() const { return nullptr; }
    QObject* diffLines() const { return nullptr; }
    int checkedCount() const { return 0; }
    int aheadCount() const { return 0; }
    int behindCount() const { return 0; }
    bool hasUpstream() const { return false; }
    QString upstreamName() const { return {}; }
    bool syncing() const { return false; }
    bool onBranch() const { return true; }
    bool pullRebase() const { return false; }
    qreal syncProgress() const { return 0.0; }
    int syncReceived() const { return 0; }
    int syncTotal() const { return 0; }
    QString repoPath() const { return {}; }
    QString activeFile() const { return {}; }
    QString selectedCommit() const { return {}; }
    QString activeCommitFile() const { return {}; }
    QObject* commitFiles() const { return nullptr; }
    QObject* commitDiff() const { return nullptr; }

    Q_INVOKABLE void discardAll() { ++m_discardAllCalls; }
    Q_INVOKABLE void stashChanges() { ++m_stashCalls; }
    Q_INVOKABLE void popStash() { ++m_popCalls; }
    Q_INVOKABLE void openRepoFolder() { ++m_openFolderCalls; }
    Q_INVOKABLE void undoLastCommit() { ++m_undoCalls; }
    Q_INVOKABLE void startMerge(const QString&) { ++m_startMergeCalls; }
    Q_INVOKABLE void applyPullDefault(bool) {}
    Q_INVOKABLE void close() {}

    int m_discardAllCalls = 0, m_stashCalls = 0, m_popCalls = 0,
        m_openFolderCalls = 0, m_undoCalls = 0, m_startMergeCalls = 0;

signals:
    void stubChanged();
    void changed();

private:
    StubBranchModelR*  m_branchModel;
    StubHistoryModelR* m_historyModel;
};

// ---------------------------------------------------------------------------

class TestQmlMenuBar : public QObject
{
    Q_OBJECT

    QObject* loadMain(QQmlApplicationEngine& engine, QmlTheme& theme,
                      RepoListModel& repoModel, MenuBarRepoStub& stub)
    {
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
        engine.rootContext()->setContextProperty(QStringLiteral("repoModel"), &repoModel);
        engine.rootContext()->setContextProperty(QStringLiteral("projectController"), QVariant());
        engine.rootContext()->setContextProperty(QStringLiteral("projectModel"), QVariant());
        engine.rootContext()->setContextProperty(QStringLiteral("repoVm"), &stub);
        engine.rootContext()->setContextProperty(QStringLiteral("log"), QVariant());
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        if (engine.rootObjects().isEmpty())
            return nullptr;
        return engine.rootObjects().first();
    }

private slots:

    void app_menu_bar_exposes_three_buttons_each_with_a_menu()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

        MenuBarRepoStub repo;

        QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/AppMenuBar.qml")));
        QVERIFY2(comp.errorString().isEmpty(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> bar(comp.create());
        QVERIFY2(bar != nullptr, qPrintable(comp.errorString()));

        bar->setProperty("repo", QVariant::fromValue(static_cast<QObject*>(&repo)));
        bar->setProperty("appSettings", QVariant());

        for (const QString& name : {QStringLiteral("menuBtnFile"),
                                    QStringLiteral("menuBtnEdit"),
                                    QStringLiteral("menuBtnRepository")})
        {
            QObject* btn = bar->findChild<QObject*>(name);
            QVERIFY2(btn != nullptr, qPrintable(name + " button not found"));
            QObject* menu = btn->property("menu").value<QObject*>();
            QVERIFY2(menu != nullptr, qPrintable(name + " has no menu"));
        }
        QVERIFY(bar->findChild<QObject*>(QStringLiteral("menuBtnView")) == nullptr);
    }

    // -----------------------------------------------------------------
    // With the bar mounted in TitleBar (Task 8), loading Main.qml exposes the
    // appMenuBar; triggering its items routes through TitleBar/Main to the VM.
    // Discard-all routes through a confirm dialog; merge through the picker.
    // -----------------------------------------------------------------
    void menu_bar_items_invoke_repo_actions()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        MenuBarRepoStub stub;

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* bar = root->findChild<QObject*>(QStringLiteral("appMenuBar"));
        QVERIFY2(bar != nullptr, "appMenuBar not found under Main root");

        // Repository ▸ Stash all changes → repoVm.stashChanges()
        QObject* stash = bar->findChild<QObject*>(QStringLiteral("stashItem"));
        QVERIFY2(stash != nullptr, "stashItem not found");
        QVERIFY(QMetaObject::invokeMethod(stash, "triggered"));
        QTest::qWait(50);
        QCOMPARE(stub.m_stashCalls, 1);

        // File ▸ Open repository folder → repoVm.openRepoFolder()
        QObject* openFolder = bar->findChild<QObject*>(QStringLiteral("openRepoFolderItem"));
        QVERIFY2(openFolder != nullptr, "openRepoFolderItem not found");
        QVERIFY(QMetaObject::invokeMethod(openFolder, "triggered"));
        QTest::qWait(50);
        QCOMPARE(stub.m_openFolderCalls, 1);

        // Repository ▸ Pop latest stash → repoVm.popStash()
        QObject* popStash = bar->findChild<QObject*>(QStringLiteral("popStashItem"));
        QVERIFY2(popStash != nullptr, "popStashItem not found");
        QVERIFY(QMetaObject::invokeMethod(popStash, "triggered"));
        QTest::qWait(50);
        QCOMPARE(stub.m_popCalls, 1);

        // Edit ▸ Undo last commit → repoVm.undoLastCommit()
        QObject* undo = bar->findChild<QObject*>(QStringLiteral("undoLastCommitItem"));
        QVERIFY2(undo != nullptr, "undoLastCommitItem not found");
        QVERIFY(QMetaObject::invokeMethod(undo, "triggered"));
        QTest::qWait(50);
        QCOMPARE(stub.m_undoCalls, 1);

        // Edit ▸ Discard all changes routes through the confirm dialog: trigger
        // the item, then accept the dialog → repoVm.discardAll().
        QObject* discard = bar->findChild<QObject*>(QStringLiteral("discardAllItem"));
        QVERIFY2(discard != nullptr, "discardAllItem not found");
        QVERIFY(QMetaObject::invokeMethod(discard, "triggered"));
        QTest::qWait(50);
        QCOMPARE(stub.m_discardAllCalls, 0); // not until the dialog is confirmed
        QObject* discardDlg = root->findChild<QObject*>(QStringLiteral("discardAllDialog"));
        QVERIFY2(discardDlg != nullptr, "discardAllDialog not found");
        QTRY_VERIFY(discardDlg->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(discardDlg, "accept"));
        QTest::qWait(50);
        QCOMPARE(stub.m_discardAllCalls, 1);

        // Repository ▸ Merge opens the branch picker dialog.
        QObject* merge = bar->findChild<QObject*>(QStringLiteral("mergeItem"));
        QVERIFY2(merge != nullptr, "mergeItem not found");
        QVERIFY(QMetaObject::invokeMethod(merge, "triggered"));
        QTest::qWait(50);
        QObject* mergeDlg = root->findChild<QObject*>(QStringLiteral("mergeTargetDialog"));
        QVERIFY2(mergeDlg != nullptr, "mergeTargetDialog not found");
        QTRY_VERIFY(mergeDlg->property("opened").toBool());
        mergeDlg->setProperty("selectedRef", QStringLiteral("some-branch"));
        QVERIFY(QMetaObject::invokeMethod(mergeDlg, "accept"));
        QTest::qWait(50);
        QCOMPARE(stub.m_startMergeCalls, 1);
    }
};

#include "test_qml_menu_bar.moc"

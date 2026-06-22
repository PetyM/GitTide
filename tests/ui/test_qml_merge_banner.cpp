// Tests for MergeBanner.qml — the merge-in-progress banner above the Changes list.
// Uses a stub QObject repo to drive the VM-facing properties without needing a real
// git repo or AsyncRepo infrastructure. The banner is loaded via Main.qml so all
// context properties (theme, repoVm) are wired identically to production.
//
// HOW IT WORKS:
//   The stub repo is a QObject with the four Q_PROPERTYs that MergeBanner reads.
//   We set those properties on the stub, assign the stub to repoVm context, load
//   Main.qml, and findChild on the banner / its buttons.

#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSignalSpy>

#include "gittide/ui/qmlcontext.hpp"
#include "gittide/ui/qmltheme.hpp"
#include "gittide/ui/repolistmodel.hpp"
#include "gittide/ui/repoviewmodel.hpp"
#include "gittide/ui/thememanager.hpp"

using namespace gittide::ui;

// ---------------------------------------------------------------------------
// Stub repo object — exposes the four read properties that MergeBanner binds.
// Wraps a RepoViewModel so the QML engine gets all the bindings it expects
// (changedFiles model, currentBranch, etc.) while we override just the merge
// state. We set merge properties via the actual RepoViewModel internals rather
// than a separate stub, to keep the test realistic. However, the VM's merge
// state is driven by AsyncRepo; there is no public setter. We therefore use a
// thin proxy QObject that shadows only the four banner properties.
// ---------------------------------------------------------------------------
class MergeStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool repoOpen             READ repoOpen             CONSTANT)
    Q_PROPERTY(bool mergeInProgress      READ mergeInProgress      NOTIFY stubChanged)
    Q_PROPERTY(QString mergedRef         READ mergedRef            NOTIFY stubChanged)
    Q_PROPERTY(int conflictedCount       READ conflictedCount      NOTIFY stubChanged)
    Q_PROPERTY(bool hasSubmoduleConflicts READ hasSubmoduleConflicts NOTIFY stubChanged)
    Q_PROPERTY(QString currentBranch     READ currentBranch        NOTIFY stubChanged)
    // Minimal no-op invokables so the banner's button handlers don't crash.
    Q_INVOKABLE void abortMerge() {}
    Q_INVOKABLE void commitMerge(const QString&) {}
    Q_INVOKABLE void retryMergeDeinitSubmodules() {}
    // Extra properties / models that WorkingPane / ChangesPane bind — supply
    // stub-quality nulls so QML doesn't emit binding errors.
    Q_PROPERTY(QObject* changedFiles     READ changedFiles         CONSTANT)
    Q_PROPERTY(QObject* diffLines        READ diffLines            CONSTANT)
    Q_PROPERTY(int checkedCount          READ checkedCount         CONSTANT)
    Q_INVOKABLE void setAllFilesChecked(bool) {}
    Q_INVOKABLE void setFileChecked(int, bool) {}
    Q_INVOKABLE void selectFile(const QString&) {}
    Q_INVOKABLE void commit(const QString&, const QString&) {}
    Q_SIGNAL void committedOk();
    Q_SIGNAL void historyListModelChanged();
    Q_PROPERTY(QObject* historyListModel READ historyListModel CONSTANT)
    Q_PROPERTY(QObject* graphColumnModel READ graphColumnModel CONSTANT)

public:
    explicit MergeStub(QObject* parent = nullptr) : QObject(parent) {}

    bool repoOpen() const { return true; }
    bool mergeInProgress() const { return m_mergeInProgress; }
    QString mergedRef() const { return m_mergedRef; }
    int conflictedCount() const { return m_conflictedCount; }
    bool hasSubmoduleConflicts() const { return m_hasSubmoduleConflicts; }
    QString currentBranch() const { return QStringLiteral("main"); }
    QObject* changedFiles() const { return nullptr; }
    QObject* diffLines() const { return nullptr; }
    int checkedCount() const { return 0; }
    QObject* historyListModel() const { return nullptr; }
    QObject* graphColumnModel() const { return nullptr; }

    void setMergeInProgress(bool v) { m_mergeInProgress = v; emit stubChanged(); }
    void setMergedRef(const QString& v) { m_mergedRef = v; emit stubChanged(); }
    void setConflictedCount(int v) { m_conflictedCount = v; emit stubChanged(); }
    void setHasSubmoduleConflicts(bool v) { m_hasSubmoduleConflicts = v; emit stubChanged(); }

signals:
    void stubChanged();
    void branchChanged();
    void mergeStateChanged();

private:
    bool m_mergeInProgress{ false };
    QString m_mergedRef;
    int m_conflictedCount{ 0 };
    bool m_hasSubmoduleConflicts{ false };
};

// ---------------------------------------------------------------------------

class TestQmlMergeBanner : public QObject
{
    Q_OBJECT
private:
    // Helper: build an engine with theme + stub wired as context, load Main.qml.
    // Returns the root object; stub is parent-owned so lifetime is the caller's.
    QObject* loadMain(QQmlApplicationEngine& engine, QmlTheme& theme, RepoListModel& repoModel, MergeStub& stub)
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
    // -----------------------------------------------------------------
    // banner is visible when mergeInProgress is true
    // -----------------------------------------------------------------
    void banner_visible_when_merge_in_progress()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        MergeStub stub;
        stub.setMergeInProgress(true);
        stub.setMergedRef(QStringLiteral("feat/foo"));
        stub.setConflictedCount(2);

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* banner = root->findChild<QObject*>(QStringLiteral("mergeBanner"));
        QVERIFY(banner != nullptr);
        QCOMPARE(banner->property("visible").toBool(), true);
    }

    // -----------------------------------------------------------------
    // banner is hidden when mergeInProgress is false
    // -----------------------------------------------------------------
    void banner_hidden_when_no_merge_in_progress()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        MergeStub stub; // mergeInProgress defaults false

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* banner = root->findChild<QObject*>(QStringLiteral("mergeBanner"));
        QVERIFY(banner != nullptr);
        QCOMPARE(banner->property("visible").toBool(), false);
    }

    // -----------------------------------------------------------------
    // retry button visible only when hasSubmoduleConflicts
    // -----------------------------------------------------------------
    void retry_button_visible_only_with_submodule_conflicts()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        MergeStub stub;
        stub.setMergeInProgress(true);
        stub.setMergedRef(QStringLiteral("feat/sub"));
        stub.setConflictedCount(1);
        stub.setHasSubmoduleConflicts(false);

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* retryBtn = root->findChild<QObject*>(QStringLiteral("mergeRetryButton"));
        QVERIFY(retryBtn != nullptr);
        QCOMPARE(retryBtn->property("visible").toBool(), false);
    }

    void retry_button_visible_when_submodule_conflicts()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        MergeStub stub;
        stub.setMergeInProgress(true);
        stub.setMergedRef(QStringLiteral("feat/sub"));
        stub.setConflictedCount(1);
        stub.setHasSubmoduleConflicts(true);

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* retryBtn = root->findChild<QObject*>(QStringLiteral("mergeRetryButton"));
        QVERIFY(retryBtn != nullptr);
        QCOMPARE(retryBtn->property("visible").toBool(), true);
    }

    // -----------------------------------------------------------------
    // commit button enabled only when conflictedCount === 0
    // -----------------------------------------------------------------
    void commit_button_disabled_when_conflicts_remain()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        MergeStub stub;
        stub.setMergeInProgress(true);
        stub.setMergedRef(QStringLiteral("feat/foo"));
        stub.setConflictedCount(3);

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* commitBtn = root->findChild<QObject*>(QStringLiteral("mergeCommitButton"));
        QVERIFY(commitBtn != nullptr);
        QCOMPARE(commitBtn->property("enabled").toBool(), false);
    }

    void commit_button_enabled_when_no_conflicts()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        MergeStub stub;
        stub.setMergeInProgress(true);
        stub.setMergedRef(QStringLiteral("feat/foo"));
        stub.setConflictedCount(0);

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* commitBtn = root->findChild<QObject*>(QStringLiteral("mergeCommitButton"));
        QVERIFY(commitBtn != nullptr);
        QCOMPARE(commitBtn->property("enabled").toBool(), true);
    }

    // -----------------------------------------------------------------
    // abort button is always present (visible with banner)
    // -----------------------------------------------------------------
    void abort_button_present_when_merge_in_progress()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        MergeStub stub;
        stub.setMergeInProgress(true);
        stub.setMergedRef(QStringLiteral("feat/bar"));
        stub.setConflictedCount(1);

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* abortBtn = root->findChild<QObject*>(QStringLiteral("mergeAbortButton"));
        QVERIFY(abortBtn != nullptr);
        QCOMPARE(abortBtn->property("visible").toBool(), true);
    }
};

#include "test_qml_merge_banner.moc"

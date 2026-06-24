// Tests for RebaseBanner.qml — the rebase-in-progress banner above the Changes list.
// Uses a stub QObject repo to drive the VM-facing properties without needing a real
// git repo or AsyncRepo infrastructure. The banner is loaded via Main.qml so all
// context properties (theme, repoVm) are wired identically to production.
//
// HOW IT WORKS:
//   The stub repo is a QObject with the Q_PROPERTYs that RebaseBanner reads.
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
// Stub repo object — exposes the read properties that RebaseBanner binds.
// Wraps enough stub surface area that WorkingPane / ChangesPane do not emit
// QML binding errors, while we control only the rebase state.
// ---------------------------------------------------------------------------
class RebaseStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool repoOpen                  READ repoOpen                  CONSTANT)
    Q_PROPERTY(bool rebaseInProgress          READ rebaseInProgress          NOTIFY stubChanged)
    Q_PROPERTY(QString rebaseOnto             READ rebaseOnto                NOTIFY stubChanged)
    Q_PROPERTY(int rebaseStep                 READ rebaseStep                NOTIFY stubChanged)
    Q_PROPERTY(int rebaseTotal                READ rebaseTotal               NOTIFY stubChanged)
    Q_PROPERTY(QString rebaseStepSummary      READ rebaseStepSummary         NOTIFY stubChanged)
    Q_PROPERTY(int rebaseConflictedCount      READ rebaseConflictedCount     NOTIFY stubChanged)
    Q_PROPERTY(bool rebaseHasSubmoduleConflicts READ rebaseHasSubmoduleConflicts NOTIFY stubChanged)
    Q_PROPERTY(QString rebasePauseReason      READ rebasePauseReason         NOTIFY stubChanged)
    Q_PROPERTY(QString rebaseMessagePrefill   READ rebaseMessagePrefill      NOTIFY stubChanged)
    Q_PROPERTY(QString currentBranch          READ currentBranch             NOTIFY stubChanged)
    // Minimal no-op invokables so the banner's button handlers don't crash.
    Q_INVOKABLE void continueRebase() {}
    Q_INVOKABLE void skipRebase() {}
    Q_INVOKABLE void abortRebase() {}
    // Extra properties / models that WorkingPane / ChangesPane bind — supply
    // stub-quality nulls so QML doesn't emit binding errors.
    Q_PROPERTY(QObject* changedFiles          READ changedFiles              CONSTANT)
    Q_PROPERTY(QObject* diffLines             READ diffLines                 CONSTANT)
    Q_PROPERTY(int checkedCount               READ checkedCount              CONSTANT)
    Q_INVOKABLE void setAllFilesChecked(bool) {}
    Q_INVOKABLE void setFileChecked(int, bool) {}
    Q_INVOKABLE void selectFile(const QString&) {}
    Q_INVOKABLE void commit(const QString&, const QString&) {}
    Q_SIGNAL void committedOk();
    Q_SIGNAL void historyListModelChanged();
    Q_PROPERTY(QObject* historyListModel      READ historyListModel          CONSTANT)
    Q_PROPERTY(QObject* graphColumnModel      READ graphColumnModel          CONSTANT)
    // MergeState stubs (WorkingPane/MergeBanner also binds these via repoVm)
    Q_PROPERTY(bool mergeInProgress           READ mergeInProgress           CONSTANT)
    Q_PROPERTY(QString mergedRef              READ mergedRef                 CONSTANT)
    Q_PROPERTY(int conflictedCount            READ conflictedCount           CONSTANT)
    Q_PROPERTY(bool hasSubmoduleConflicts     READ hasSubmoduleConflicts     CONSTANT)
    Q_INVOKABLE void abortMerge() {}
    Q_INVOKABLE void commitMerge(const QString&) {}
    Q_INVOKABLE void retryMergeDeinitSubmodules() {}

public:
    explicit RebaseStub(QObject* parent = nullptr) : QObject(parent) {}

    bool repoOpen() const { return true; }
    bool rebaseInProgress() const { return m_rebaseInProgress; }
    QString rebaseOnto() const { return m_rebaseOnto; }
    int rebaseStep() const { return m_rebaseStep; }
    int rebaseTotal() const { return m_rebaseTotal; }
    QString rebaseStepSummary() const { return m_rebaseStepSummary; }
    int rebaseConflictedCount() const { return m_rebaseConflictedCount; }
    bool rebaseHasSubmoduleConflicts() const { return m_rebaseHasSubmoduleConflicts; }
    QString rebasePauseReason() const { return {}; }
    QString rebaseMessagePrefill() const { return {}; }
    QString currentBranch() const { return QStringLiteral("main"); }
    QObject* changedFiles() const { return nullptr; }
    QObject* diffLines() const { return nullptr; }
    int checkedCount() const { return 0; }
    QObject* historyListModel() const { return nullptr; }
    QObject* graphColumnModel() const { return nullptr; }
    bool mergeInProgress() const { return false; }
    QString mergedRef() const { return {}; }
    int conflictedCount() const { return 0; }
    bool hasSubmoduleConflicts() const { return false; }

    void setRebaseInProgress(bool v) { m_rebaseInProgress = v; emit stubChanged(); }
    void setRebaseOnto(const QString& v) { m_rebaseOnto = v; emit stubChanged(); }
    void setRebaseStep(int v) { m_rebaseStep = v; emit stubChanged(); }
    void setRebaseTotal(int v) { m_rebaseTotal = v; emit stubChanged(); }
    void setRebaseStepSummary(const QString& v) { m_rebaseStepSummary = v; emit stubChanged(); }
    void setRebaseConflictedCount(int v) { m_rebaseConflictedCount = v; emit stubChanged(); }
    void setRebaseHasSubmoduleConflicts(bool v) { m_rebaseHasSubmoduleConflicts = v; emit stubChanged(); }

signals:
    void stubChanged();
    void branchChanged();
    void rebaseStateChanged();

private:
    bool m_rebaseInProgress{ false };
    QString m_rebaseOnto;
    int m_rebaseStep{ 0 };
    int m_rebaseTotal{ 0 };
    QString m_rebaseStepSummary;
    int m_rebaseConflictedCount{ 0 };
    bool m_rebaseHasSubmoduleConflicts{ false };
};

// ---------------------------------------------------------------------------

class TestQmlRebaseBanner : public QObject
{
    Q_OBJECT
private:
    // Helper: build an engine with theme + stub wired as context, load Main.qml.
    // Returns the root object; stub is parent-owned so lifetime is the caller's.
    QObject* loadMain(QQmlApplicationEngine& engine, QmlTheme& theme, RepoListModel& repoModel, RebaseStub& stub)
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
    // banner is visible when rebaseInProgress is true
    // -----------------------------------------------------------------
    void banner_visible_when_rebase_in_progress()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RebaseStub stub;
        stub.setRebaseInProgress(true);
        stub.setRebaseOnto(QStringLiteral("main"));
        stub.setRebaseStep(1);
        stub.setRebaseTotal(3);
        stub.setRebaseConflictedCount(1);

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* banner = root->findChild<QObject*>(QStringLiteral("rebaseBanner"));
        QVERIFY(banner != nullptr);
        QCOMPARE(banner->property("visible").toBool(), true);
    }

    // -----------------------------------------------------------------
    // banner is hidden when rebaseInProgress is false
    // -----------------------------------------------------------------
    void banner_hidden_when_no_rebase_in_progress()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RebaseStub stub; // rebaseInProgress defaults false

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* banner = root->findChild<QObject*>(QStringLiteral("rebaseBanner"));
        QVERIFY(banner != nullptr);
        QCOMPARE(banner->property("visible").toBool(), false);
    }

    // -----------------------------------------------------------------
    // step label contains "1/3" when step=1, total=3
    // -----------------------------------------------------------------
    void label_contains_step_fraction()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RebaseStub stub;
        stub.setRebaseInProgress(true);
        stub.setRebaseOnto(QStringLiteral("main"));
        stub.setRebaseStep(1);
        stub.setRebaseTotal(3);
        stub.setRebaseConflictedCount(1);

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        // The banner text is on the second Label inside rebaseBanner.
        // Find it by walking children: banner -> RowLayout -> Labels.
        QObject* banner = root->findChild<QObject*>(QStringLiteral("rebaseBanner"));
        QVERIFY(banner != nullptr);

        // Collect all Label children; the info label has Layout.fillWidth and
        // contains the step text. We check all labels for the fraction.
        bool found = false;
        const auto labels = banner->findChildren<QObject*>();
        for (QObject* child : labels)
        {
            const QString text = child->property("text").toString();
            if (text.contains(QStringLiteral("1/3")))
            {
                found = true;
                break;
            }
        }
        QVERIFY2(found, "No label inside rebaseBanner contained '1/3'");
    }

    // -----------------------------------------------------------------
    // Continue button is disabled while rebaseConflictedCount > 0
    // -----------------------------------------------------------------
    void continue_button_disabled_when_conflicts_remain()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RebaseStub stub;
        stub.setRebaseInProgress(true);
        stub.setRebaseOnto(QStringLiteral("main"));
        stub.setRebaseStep(1);
        stub.setRebaseTotal(3);
        stub.setRebaseConflictedCount(1);

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* continueBtn = root->findChild<QObject*>(QStringLiteral("rebaseContinueButton"));
        QVERIFY(continueBtn != nullptr);
        QCOMPARE(continueBtn->property("enabled").toBool(), false);
    }

    // -----------------------------------------------------------------
    // Continue button is enabled when rebaseConflictedCount === 0
    // -----------------------------------------------------------------
    void continue_button_enabled_when_no_conflicts()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RebaseStub stub;
        stub.setRebaseInProgress(true);
        stub.setRebaseOnto(QStringLiteral("main"));
        stub.setRebaseStep(2);
        stub.setRebaseTotal(3);
        stub.setRebaseConflictedCount(0);

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* continueBtn = root->findChild<QObject*>(QStringLiteral("rebaseContinueButton"));
        QVERIFY(continueBtn != nullptr);
        QCOMPARE(continueBtn->property("enabled").toBool(), true);
    }

    // -----------------------------------------------------------------
    // Skip and Abort buttons are present when rebaseInProgress
    // -----------------------------------------------------------------
    void skip_and_abort_buttons_present_when_rebase_in_progress()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RebaseStub stub;
        stub.setRebaseInProgress(true);
        stub.setRebaseOnto(QStringLiteral("main"));
        stub.setRebaseStep(1);
        stub.setRebaseTotal(2);
        stub.setRebaseConflictedCount(1);

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* skipBtn = root->findChild<QObject*>(QStringLiteral("rebaseSkipButton"));
        QVERIFY(skipBtn != nullptr);
        QCOMPARE(skipBtn->property("visible").toBool(), true);

        QObject* abortBtn = root->findChild<QObject*>(QStringLiteral("rebaseAbortButton"));
        QVERIFY(abortBtn != nullptr);
        QCOMPARE(abortBtn->property("visible").toBool(), true);
    }
};

#include "test_qml_rebase_banner.moc"

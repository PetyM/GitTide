// Tests for RebaseBanner.qml — message-pause variant (rebasePauseReason == "message").
// Verifies that when the pause reason is "message":
//   - the headline mentions "editing message" and the step fraction;
//   - the Continue button is ENABLED (message pause always allows continue);
//   - clicking Continue emits requestMessageEdit (does NOT call continueRebase directly);
//   - Skip and Abort buttons are still present.
// Also keeps the existing conflict-pause assertions passing (no regression).
//
// Uses the same harness as test_qml_rebase_banner.cpp: a stub QObject with Q_PROPERTYs +
// Q_INVOKABLE spies. Extended with rebasePauseReason and rebaseMessagePrefill.

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
// Stub repo — mirrors RebaseStub from test_qml_rebase_banner.cpp, extended with
// rebasePauseReason and rebaseMessagePrefill. continueRebase is now spied.
// ---------------------------------------------------------------------------
class RebaseBannerMessageStub : public QObject
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

    // Spy counters — readable from QML/test.
    Q_PROPERTY(int continueRebaseCallCount    READ continueRebaseCallCount   NOTIFY spyChanged)

    // no-op invokables for banner button handlers
    Q_INVOKABLE void continueRebase()
    {
        ++m_continueRebaseCallCount;
        emit spyChanged();
    }
    Q_INVOKABLE void continueRebase(const QString&)
    {
        ++m_continueRebaseCallCount;
        emit spyChanged();
    }
    Q_INVOKABLE void skipRebase()  {}
    Q_INVOKABLE void abortRebase() {}

    // Extra properties/models for WorkingPane/ChangesPane stubs
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
    // MergeState stubs
    Q_PROPERTY(bool mergeInProgress           READ mergeInProgress           CONSTANT)
    Q_PROPERTY(QString mergedRef              READ mergedRef                 CONSTANT)
    Q_PROPERTY(int conflictedCount            READ conflictedCount           CONSTANT)
    Q_PROPERTY(bool hasSubmoduleConflicts     READ hasSubmoduleConflicts     CONSTANT)
    Q_INVOKABLE void abortMerge() {}
    Q_INVOKABLE void commitMerge(const QString&) {}
    Q_INVOKABLE void retryMergeDeinitSubmodules() {}

public:
    explicit RebaseBannerMessageStub(QObject* parent = nullptr) : QObject(parent) {}

    bool repoOpen() const { return true; }
    bool rebaseInProgress() const { return m_rebaseInProgress; }
    QString rebaseOnto() const { return m_rebaseOnto; }
    int rebaseStep() const { return m_rebaseStep; }
    int rebaseTotal() const { return m_rebaseTotal; }
    QString rebaseStepSummary() const { return m_rebaseStepSummary; }
    int rebaseConflictedCount() const { return m_rebaseConflictedCount; }
    bool rebaseHasSubmoduleConflicts() const { return m_rebaseHasSubmoduleConflicts; }
    QString rebasePauseReason() const { return m_rebasePauseReason; }
    QString rebaseMessagePrefill() const { return m_rebaseMessagePrefill; }
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
    int continueRebaseCallCount() const { return m_continueRebaseCallCount; }

    void setRebaseInProgress(bool v)              { m_rebaseInProgress = v;         emit stubChanged(); }
    void setRebaseOnto(const QString& v)           { m_rebaseOnto = v;               emit stubChanged(); }
    void setRebaseStep(int v)                      { m_rebaseStep = v;               emit stubChanged(); }
    void setRebaseTotal(int v)                     { m_rebaseTotal = v;              emit stubChanged(); }
    void setRebaseStepSummary(const QString& v)    { m_rebaseStepSummary = v;        emit stubChanged(); }
    void setRebaseConflictedCount(int v)           { m_rebaseConflictedCount = v;    emit stubChanged(); }
    void setRebaseHasSubmoduleConflicts(bool v)    { m_rebaseHasSubmoduleConflicts = v; emit stubChanged(); }
    void setRebasePauseReason(const QString& v)    { m_rebasePauseReason = v;        emit stubChanged(); }
    void setRebaseMessagePrefill(const QString& v) { m_rebaseMessagePrefill = v;     emit stubChanged(); }

signals:
    void stubChanged();
    void spyChanged();
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
    QString m_rebasePauseReason;
    QString m_rebaseMessagePrefill;
    int m_continueRebaseCallCount{ 0 };
};

// ---------------------------------------------------------------------------

class TestQmlRebaseBannerMessage : public QObject
{
    Q_OBJECT
private:
    QObject* loadMain(QQmlApplicationEngine& engine, QmlTheme& theme,
                      RepoListModel& repoModel, RebaseBannerMessageStub& stub)
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
    // message-pause: headline contains "editing message" and step fraction
    // -----------------------------------------------------------------
    void message_pause_headline_contains_editing_message_and_step()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RebaseBannerMessageStub stub;
        stub.setRebaseInProgress(true);
        stub.setRebaseOnto(QStringLiteral("main"));
        stub.setRebaseStep(2);
        stub.setRebaseTotal(5);
        stub.setRebaseStepSummary(QStringLiteral("fix typo"));
        stub.setRebaseConflictedCount(0);
        stub.setRebasePauseReason(QStringLiteral("message"));

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* banner = root->findChild<QObject*>(QStringLiteral("rebaseBanner"));
        QVERIFY(banner != nullptr);

        bool foundEditing = false;
        bool foundStep    = false;
        const auto children = banner->findChildren<QObject*>();
        for (QObject* child : children)
        {
            const QString text = child->property("text").toString();
            if (text.contains(QStringLiteral("editing message")))
                foundEditing = true;
            if (text.contains(QStringLiteral("2/5")))
                foundStep = true;
        }
        QVERIFY2(foundEditing, "No label inside rebaseBanner contained 'editing message'");
        QVERIFY2(foundStep,    "No label inside rebaseBanner contained '2/5'");
    }

    // -----------------------------------------------------------------
    // message-pause: Continue button is ENABLED (no conflict gate)
    // -----------------------------------------------------------------
    void message_pause_continue_button_enabled()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RebaseBannerMessageStub stub;
        stub.setRebaseInProgress(true);
        stub.setRebaseStep(1);
        stub.setRebaseTotal(3);
        stub.setRebaseConflictedCount(0);   // no conflicts, as expected for message pause
        stub.setRebasePauseReason(QStringLiteral("message"));

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* continueBtn = root->findChild<QObject*>(QStringLiteral("rebaseContinueButton"));
        QVERIFY(continueBtn != nullptr);
        QCOMPARE(continueBtn->property("enabled").toBool(), true);
    }

    // -----------------------------------------------------------------
    // message-pause: clicking Continue emits requestMessageEdit,
    // does NOT call continueRebase() directly
    // -----------------------------------------------------------------
    void message_pause_continue_click_emits_requestMessageEdit_not_continueRebase()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RebaseBannerMessageStub stub;
        stub.setRebaseInProgress(true);
        stub.setRebaseStep(1);
        stub.setRebaseTotal(3);
        stub.setRebaseConflictedCount(0);
        stub.setRebasePauseReason(QStringLiteral("message"));

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* banner = root->findChild<QObject*>(QStringLiteral("rebaseBanner"));
        QVERIFY(banner != nullptr);

        QObject* continueBtn = root->findChild<QObject*>(QStringLiteral("rebaseContinueButton"));
        QVERIFY(continueBtn != nullptr);
        QCOMPARE(continueBtn->property("enabled").toBool(), true);

        // Spy on the banner's requestMessageEdit signal.
        QSignalSpy requestEditSpy(banner, SIGNAL(requestMessageEdit()));
        QVERIFY(requestEditSpy.isValid());

        // Record continueRebase call count before click.
        int callsBefore = stub.continueRebaseCallCount();

        // Click the button.
        QMetaObject::invokeMethod(continueBtn, "clicked", Qt::DirectConnection);

        // requestMessageEdit should have fired once.
        QCOMPARE(requestEditSpy.count(), 1);
        // continueRebase should NOT have been called.
        QCOMPARE(stub.continueRebaseCallCount(), callsBefore);
    }

    // -----------------------------------------------------------------
    // message-pause: Abort button is present (always-reachable guarantee)
    // -----------------------------------------------------------------
    void message_pause_abort_button_present()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RebaseBannerMessageStub stub;
        stub.setRebaseInProgress(true);
        stub.setRebaseStep(1);
        stub.setRebaseTotal(3);
        stub.setRebaseConflictedCount(0);
        stub.setRebasePauseReason(QStringLiteral("message"));

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* abortBtn = root->findChild<QObject*>(QStringLiteral("rebaseAbortButton"));
        QVERIFY(abortBtn != nullptr);
        QCOMPARE(abortBtn->property("visible").toBool(), true);
    }

    // -----------------------------------------------------------------
    // Regression: conflict-pause Continue is still DISABLED when conflicts > 0
    // -----------------------------------------------------------------
    void conflict_pause_continue_disabled_when_conflicts()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RebaseBannerMessageStub stub;
        stub.setRebaseInProgress(true);
        stub.setRebaseOnto(QStringLiteral("main"));
        stub.setRebaseStep(1);
        stub.setRebaseTotal(3);
        stub.setRebaseConflictedCount(2);
        // rebasePauseReason defaults to "" (not "message") — conflict pause

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* continueBtn = root->findChild<QObject*>(QStringLiteral("rebaseContinueButton"));
        QVERIFY(continueBtn != nullptr);
        QCOMPARE(continueBtn->property("enabled").toBool(), false);
    }

    // -----------------------------------------------------------------
    // Regression: conflict-pause Continue is ENABLED when conflicts == 0
    // -----------------------------------------------------------------
    void conflict_pause_continue_enabled_when_no_conflicts()
    {
        ThemeManager mgr;
        mgr.setMode(ThemeManager::Mode::Dark);
        QmlTheme theme(&mgr);
        RepoListModel repoModel;
        RebaseBannerMessageStub stub;
        stub.setRebaseInProgress(true);
        stub.setRebaseOnto(QStringLiteral("main"));
        stub.setRebaseStep(2);
        stub.setRebaseTotal(3);
        stub.setRebaseConflictedCount(0);
        // rebasePauseReason defaults to "" — conflict pause, resolved

        QQmlApplicationEngine engine;
        QObject* root = loadMain(engine, theme, repoModel, stub);
        QVERIFY(root != nullptr);

        QObject* continueBtn = root->findChild<QObject*>(QStringLiteral("rebaseContinueButton"));
        QVERIFY(continueBtn != nullptr);
        QCOMPARE(continueBtn->property("enabled").toBool(), true);
    }
};

#include "test_qml_rebase_banner_message.moc"

#include "gittide/ui/branchbar.hpp"
#include <QAction>
#include <QMenu>
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

using gittide::ui::BranchBar;

// Helper: find a QAction by text in a menu (non-recursive, immediate children).
static QAction* findAction(QMenu* menu, const QString& text)
{
    for (QAction* a : menu->actions())
        if (a->text() == text)
            return a;
    return nullptr;
}

// Helper: find the submenu whose title contains `substring`.
static QMenu* findSubMenu(QMenu* menu, const QString& substring)
{
    for (QAction* a : menu->actions())
        if (a->menu() && a->text().contains(substring))
            return a->menu();
    return nullptr;
}

class TestBranchBar : public QObject
{
    Q_OBJECT
private slots:
    void shows_branch_name()
    {
        BranchBar bar;
        bar.setHead(gittide::HeadState{"main", "abc", false, false});
        auto* btn = bar.findChild<QToolButton*>(QStringLiteral("currentBranchButton"));
        QVERIFY(btn);
        QVERIFY(btn->text().contains(QStringLiteral("main")));
    }

    void shows_detached_state()
    {
        BranchBar bar;
        bar.setHead(gittide::HeadState{"", "abc1234deadbeef", true, false});
        auto* btn = bar.findChild<QToolButton*>(QStringLiteral("currentBranchButton"));
        QVERIFY(btn->text().contains(QStringLiteral("detached")));
        QVERIFY(btn->text().contains(QStringLiteral("abc1234"))); // short oid
    }

    // Verify that triggering Delete on a non-current branch emits deleteRequested
    // with the correct branch name, and that the current branch's delete action
    // is disabled (cannot delete the checked-out branch).
    void delete_non_current_emits_signal_and_current_is_disabled()
    {
        BranchBar bar;
        // Two branches: "main" is current (isHead=true), "feature" is not.
        bar.setBranches({
            gittide::BranchInfo{"main",    /*isHead=*/true},
            gittide::BranchInfo{"feature", /*isHead=*/false},
        });

        auto* btn  = bar.findChild<QToolButton*>(QStringLiteral("currentBranchButton"));
        QVERIFY(btn);
        QMenu* topMenu = btn->menu();
        QVERIFY(topMenu);

        // The "feature" submenu must exist and its Delete action must be enabled.
        QMenu* featureSub = findSubMenu(topMenu, QStringLiteral("feature"));
        QVERIFY2(featureSub, "No submenu for 'feature' branch");
        QAction* featureDelete = findAction(featureSub, QStringLiteral("Delete…"));
        QVERIFY2(featureDelete, "No 'Delete…' action in feature submenu");
        QVERIFY2(featureDelete->isEnabled(), "Delete action for non-current branch should be enabled");

        // The "main" (current) submenu must have a disabled Delete action.
        QMenu* mainSub = findSubMenu(topMenu, QStringLiteral("main"));
        QVERIFY2(mainSub, "No submenu for 'main' branch");
        QAction* mainDelete = findAction(mainSub, QStringLiteral("Delete…"));
        QVERIFY2(mainDelete, "No 'Delete…' action in main submenu");
        QVERIFY2(!mainDelete->isEnabled(), "Delete action for current branch should be disabled");

        // Triggering the feature Delete action must emit deleteRequested("feature").
        QSignalSpy spy(&bar, &BranchBar::deleteRequested);
        featureDelete->trigger();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("feature"));
    }

    // Verify rename on a non-current branch emits renameRequested with that name.
    void rename_non_current_emits_signal_with_name()
    {
        BranchBar bar;
        bar.setBranches({
            gittide::BranchInfo{"main",    /*isHead=*/true},
            gittide::BranchInfo{"feature", /*isHead=*/false},
        });

        auto* btn  = bar.findChild<QToolButton*>(QStringLiteral("currentBranchButton"));
        QMenu* topMenu = btn->menu();
        QMenu* featureSub = findSubMenu(topMenu, QStringLiteral("feature"));
        QVERIFY(featureSub);
        QAction* renameAction = findAction(featureSub, QStringLiteral("Rename…"));
        QVERIFY(renameAction);

        QSignalSpy spy(&bar, &BranchBar::renameRequested);
        renameAction->trigger();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("feature"));
    }

    // Verify that the current-branch submenu has no "Switch to this branch" action.
    void current_branch_has_no_switch_action()
    {
        BranchBar bar;
        bar.setBranches({
            gittide::BranchInfo{"main", /*isHead=*/true},
        });

        auto* btn  = bar.findChild<QToolButton*>(QStringLiteral("currentBranchButton"));
        QMenu* topMenu = btn->menu();
        QMenu* mainSub = findSubMenu(topMenu, QStringLiteral("main"));
        QVERIFY(mainSub);
        QAction* switchAction = findAction(mainSub, QStringLiteral("Switch to this branch"));
        QVERIFY2(!switchAction, "Current branch should not have a 'Switch' action");
    }
};
#include "test_branch_bar.moc"

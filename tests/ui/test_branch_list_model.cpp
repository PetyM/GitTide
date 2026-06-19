#include <QtTest>
#include <QAbstractItemModel>

#include "gittide/ui/branchlistmodel.hpp"

using gittide::BranchInfo;
using gittide::BranchKind;
using gittide::ui::BranchListModel;

class TestBranchListModel : public QObject
{
    Q_OBJECT
private slots:
    void groups_and_orders_sections()
    {
        BranchListModel m;
        m.setBranches({
            BranchInfo{.name = "origin/main", .kind = BranchKind::RemoteTracking},
            BranchInfo{.name = "feature", .kind = BranchKind::Local},
            BranchInfo{.name = "wt", .kind = BranchKind::Local, .worktreePath = "/tmp/wt"},
            BranchInfo{.name = "main", .isHead = true, .kind = BranchKind::Local},
        });
        QCOMPARE(m.rowCount(), 4);

        auto section = [&](int r) { return m.index(r).data(BranchListModel::SectionRole).toString(); };
        // Local (feature, main) -> Worktrees (wt) -> Remote (origin/main)
        QCOMPARE(section(0), QStringLiteral("Local"));
        QCOMPARE(section(1), QStringLiteral("Local"));
        QCOMPARE(section(2), QStringLiteral("Worktrees"));
        QCOMPARE(section(3), QStringLiteral("Remote"));
        QCOMPARE(m.index(3).data(BranchListModel::RemoteRole).toBool(), true);
        QCOMPARE(m.index(2).data(BranchListModel::WorktreePathRole).toString(), QStringLiteral("/tmp/wt"));
    }

    void filter_is_case_insensitive_substring()
    {
        BranchListModel m;
        m.setBranches({BranchInfo{.name = "main", .isHead = true}, BranchInfo{.name = "feature/login"}});
        m.setFilter(QStringLiteral("LOG"));
        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(m.index(0).data(BranchListModel::NameRole).toString(), QStringLiteral("feature/login"));
        m.setFilter(QString());
        QCOMPARE(m.rowCount(), 2);
    }

    void local_branch_names_excludes_remote()
    {
        BranchListModel m;
        m.setBranches({BranchInfo{.name = "main", .isHead = true},
                       BranchInfo{.name = "origin/main", .kind = BranchKind::RemoteTracking}});
        QCOMPARE(m.localBranchNames(), QStringList{QStringLiteral("main")});
    }
};

#include "test_branch_list_model.moc"

#include <QAbstractItemModelTester>
#include <QObject>
#include <QtTest/QtTest>
#include <filesystem>
#include <vector>

#include "gittide/gitrepo.hpp"
#include "gittide/projectstore.hpp"
#include "gittide/submodule.hpp"
#include "gittide/ui/repolistmodel.hpp"
#include "support/temprepo.hpp"

using gittide::RepoRef;
using gittide::ui::RepoListModel;

class TestRepoListModel : public QObject
{
    Q_OBJECT
private slots:
    void exposes_alias_path_and_missing_flag()
    {
        const auto tmp = std::filesystem::temp_directory_path(); // exists
        std::vector<RepoRef> repos{
            RepoRef{.path = tmp.generic_string(), .alias = "present"},
            RepoRef{.path = "/no/such/path/gittide-test", .alias = "gone"},
        };

        RepoListModel model;
        QAbstractItemModelTester tester(&model);
        model.setRepos(repos);

        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("present"));
        QCOMPARE(model.data(model.index(0, 0), RepoListModel::MissingRole).toBool(), false);
        QCOMPARE(model.data(model.index(1, 0), RepoListModel::MissingRole).toBool(), true);
        QCOMPARE(model.data(model.index(1, 0), RepoListModel::PathRole).toString(), QStringLiteral("/no/such/path/gittide-test"));
    }

    void empty_alias_falls_back_to_directory_basename()
    {
        std::vector<RepoRef> repos{RepoRef{.path = "/home/u/api-server", .alias = ""}};
        RepoListModel model;
        model.setRepos(repos);
        QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("api-server"));
    }

    void trailing_slash_path_still_yields_basename()
    {
        // Persisted paths can carry a trailing separator (libgit2 workdir, some
        // folder pickers). The display name must remain the directory's name,
        // never a blank string.
        std::vector<RepoRef> repos{RepoRef{.path = "/home/u/api-server/", .alias = ""}};
        RepoListModel model;
        model.setRepos(repos);
        QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("api-server"));
    }

    void tree_model_parent_of_top_level_item_is_invalid()
    {
        std::vector<RepoRef> repos{RepoRef{.path = "/home/u/api", .alias = "api"}};
        RepoListModel model;
        QAbstractItemModelTester tester(&model);
        model.setRepos(repos);

        QVERIFY(!model.parent(model.index(0, 0)).isValid());
    }

    void tree_model_top_level_items_have_no_children()
    {
        std::vector<RepoRef> repos{RepoRef{.path = "/home/u/api", .alias = "api"}};
        RepoListModel model;
        model.setRepos(repos);

        const QModelIndex item = model.index(0, 0);
        QCOMPARE(model.rowCount(item), 0);
    }

    void tree_model_index_with_valid_parent_is_invalid()
    {
        std::vector<RepoRef> repos{RepoRef{.path = "/home/u/api", .alias = "api"}};
        RepoListModel model;
        model.setRepos(repos);

        const QModelIndex parent = model.index(0, 0);
        QVERIFY(!model.index(0, 0, parent).isValid());
    }

    void submodule_rows_expose_recursive_children_and_new_roles()
    {
        gittide::test::TempRepo child;
        child.writeFile("a.txt", "x\n");
        child.commitAll("child");

        gittide::test::TempRepo parent;
        parent.writeFile("top.txt", "p\n");
        parent.commitAll("parent");
        parent.addSubmodule("libchild", child.path());
        parent.commitAll("add submodule");

        std::vector<RepoRef> repos{
            RepoRef{.path = parent.path().generic_string(), .alias = "parent"},
        };

        RepoListModel model;
        QAbstractItemModelTester tester(&model);
        model.setRepos(repos);
        // setRepos does no git I/O; the subtree arrives through applySubmodules,
        // which is what ProjectController's (off-thread) poll pass calls.
        {
            auto opened = gittide::GitRepo::open(parent.path());
            QVERIFY(opened.has_value());
            auto tree = opened->submoduleTree();
            QVERIFY(tree.has_value());
            model.applySubmodules(QString::fromStdString(parent.path().generic_string()), *tree);
        }

        QCOMPARE(model.rowCount(), 1);
        const QModelIndex top = model.index(0, 0);
        QCOMPARE(model.data(top, RepoListModel::IsSubmoduleRole).toBool(), false);
        QCOMPARE(model.rowCount(top), 1); // one submodule child

        const QModelIndex sub = model.index(0, 0, top);
        QVERIFY(sub.isValid());
        QCOMPARE(model.parent(sub), top);
        QCOMPARE(model.data(sub, RepoListModel::IsSubmoduleRole).toBool(), true);
        QCOMPARE(model.data(sub, Qt::DisplayRole).toString(), QStringLiteral("libchild"));
        QCOMPARE(model.data(sub, RepoListModel::ShortOidRole).toString().size(), 7);
        QCOMPARE(model.data(sub, RepoListModel::StatusRole).toInt(),
                 static_cast<int>(gittide::SubmoduleStatus::Clean));

        // indexForRepoPath resolves both the top-level repo and a nested submodule
        // to their index (so the sidebar can expandToIndex a restored subrepo), and
        // returns an invalid index for an unknown path.
        const QString subPath = model.data(sub, RepoListModel::PathRole).toString();
        QCOMPARE(model.indexForRepoPath(QString::fromStdString(parent.path().generic_string())), top);
        QCOMPARE(model.indexForRepoPath(subPath), sub);
        QVERIFY(!model.indexForRepoPath(QStringLiteral("/no/such/subrepo")).isValid());
        QVERIFY(!model.indexForRepoPath(QString()).isValid());
    }

    void fetchState_roundtrips_and_resets()
    {
        using gittide::ui::RepoListModel;
        RepoListModel m;
        m.setRepos({gittide::RepoRef{.path = "/home/u/api"}, gittide::RepoRef{.path = "/home/u/web"}});
        QCOMPARE(m.topLevelCount(), 2);

        const QModelIndex i0 = m.index(0, 0);
        // Defaults.
        QCOMPARE(m.data(i0, RepoListModel::FetchStateRole).toInt(), int(RepoListModel::FetchState::Idle));

        QSignalSpy spy(&m, &QAbstractItemModel::dataChanged);
        QVERIFY(m.setFetchStateByPath(QStringLiteral("/home/u/api"), RepoListModel::FetchState::Running));
        QCOMPARE(m.data(i0, RepoListModel::FetchStateRole).toInt(), int(RepoListModel::FetchState::Running));
        QCOMPARE(spy.count(), 1);

        QVERIFY(m.setSyncCountsByPath(QStringLiteral("/home/u/api"), 1, 3, false));
        QCOMPARE(m.data(i0, RepoListModel::AheadRole).toInt(), 1);
        QCOMPARE(m.data(i0, RepoListModel::BehindRole).toInt(), 3);

        QVERIFY(m.setFetchStateByPath(QStringLiteral("/home/u/api"), RepoListModel::FetchState::Failed, QStringLiteral("boom")));
        QCOMPARE(m.data(i0, RepoListModel::FetchErrorRole).toString(), QStringLiteral("boom"));

        m.resetFetchStates();
        QCOMPARE(m.data(i0, RepoListModel::FetchStateRole).toInt(), int(RepoListModel::FetchState::Idle));
        QCOMPARE(m.data(i0, RepoListModel::BehindRole).toInt(), 0);
    }

    void setFetchState_out_of_range_is_noop()
    {
        using gittide::ui::RepoListModel;
        RepoListModel m;
        m.setRepos({gittide::RepoRef{.path = "/home/u/api"}});
        QVERIFY(!m.setFetchStateByPath(QStringLiteral("/no/such/repo"), RepoListModel::FetchState::Running));
        QCOMPARE(m.topLevelCount(), 1);
    }

    void depth2_submodule_owner_path_and_nontop_apply()
    {
        // Locks Fix 1 (OwnerRepoPathRole = immediate parent) and
        // Fix 2 (applySubmodules works on a non-root node path).
        using gittide::SubmoduleNode;
        using gittide::SubmoduleStatus;

        RepoListModel model;
        QAbstractItemModelTester tester(&model);
        model.setRepos({RepoRef{.path = "/tmp/gittide-root", .alias = "root"}});

        // Build depth-2 tree: root → sub → nested.
        SubmoduleNode nested;
        nested.name     = "nested";
        nested.path     = "/tmp/gittide-root/sub/nested";
        nested.status   = SubmoduleStatus::Clean;
        nested.shortOid = "aaa1111";

        SubmoduleNode sub;
        sub.name     = "sub";
        sub.path     = "/tmp/gittide-root/sub";
        sub.status   = SubmoduleStatus::Clean;
        sub.shortOid = "bbb2222";
        sub.children = {nested};

        model.applySubmodules(QStringLiteral("/tmp/gittide-root"), {sub});

        const QModelIndex topIdx    = model.index(0, 0);
        QCOMPARE(model.rowCount(topIdx), 1);
        const QModelIndex subIdx    = model.index(0, 0, topIdx);
        QVERIFY(subIdx.isValid());
        QCOMPARE(model.rowCount(subIdx), 1);
        const QModelIndex nestedIdx = model.index(0, 0, subIdx);
        QVERIFY(nestedIdx.isValid());

        // Fix 1: grandchild OwnerRepoPathRole == immediate parent path (sub), not root.
        QCOMPARE(model.data(nestedIdx, RepoListModel::OwnerRepoPathRole).toString(),
                 QStringLiteral("/tmp/gittide-root/sub"));
        // depth-1 sub still resolves to root (unchanged behaviour).
        QCOMPARE(model.data(subIdx, RepoListModel::OwnerRepoPathRole).toString(),
                 QStringLiteral("/tmp/gittide-root"));

        // Fix 2: applySubmodules targeting the sub's own path refreshes its children.
        SubmoduleNode nestedUpdated;
        nestedUpdated.name     = "nested";
        nestedUpdated.path     = "/tmp/gittide-root/sub/nested";
        nestedUpdated.status   = SubmoduleStatus::Dirty;
        nestedUpdated.shortOid = "ccc3333";

        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        model.applySubmodules(QStringLiteral("/tmp/gittide-root/sub"), {nestedUpdated});
        // Same path → in-place update, NOT a destructive rebuild: node identity is
        // preserved so an expanded subtree keeps its TreeView expansion.
        QCOMPARE(removed.count(), 0);
        QCOMPARE(inserted.count(), 0);
        QVERIFY(changed.count() > 0);

        const QModelIndex nestedIdx2 = model.index(0, 0, subIdx);
        QVERIFY(nestedIdx2.isValid());
        QCOMPARE(nestedIdx2, nestedIdx); // same node (same internalPointer) — identity kept
        QCOMPARE(model.data(nestedIdx2, RepoListModel::StatusRole).toInt(),
                 static_cast<int>(SubmoduleStatus::Dirty));
        QCOMPARE(model.data(nestedIdx2, RepoListModel::ShortOidRole).toString(),
                 QStringLiteral("ccc3333"));
    }

    void applySubmodules_inPlaceUpdate_preservesSiblingIdentity()
    {
        // Regression: switching branches changes submodule pins/status; the refresh
        // must update fields in place, never remove+reinsert the whole subtree
        // (which collapsed expanded submodules in the sidebar TreeView).
        using gittide::SubmoduleNode;
        using gittide::SubmoduleStatus;

        RepoListModel            model;
        QAbstractItemModelTester tester(&model);
        model.setRepos({RepoRef{.path = "/tmp/gittide-root", .alias = "root"}});

        auto makeSub = [](const char* name, const char* path, SubmoduleStatus st, const char* oid)
        {
            SubmoduleNode s;
            s.name     = name;
            s.path     = path;
            s.status   = st;
            s.shortOid = oid;
            return s;
        };

        const SubmoduleNode a  = makeSub("a", "/tmp/gittide-root/a", SubmoduleStatus::Clean, "aaa1111");
        const SubmoduleNode b  = makeSub("b", "/tmp/gittide-root/b", SubmoduleStatus::Clean, "bbb1111");
        model.applySubmodules(QStringLiteral("/tmp/gittide-root"), {a, b});

        const QModelIndex topIdx = model.index(0, 0);
        QCOMPARE(model.rowCount(topIdx), 2);
        const QModelIndex aIdx = model.index(0, 0, topIdx);
        const QModelIndex bIdx = model.index(1, 0, topIdx);

        // Branch switch: same submodule set, a's pin moved (now Dirty), b unchanged.
        const SubmoduleNode a2 = makeSub("a", "/tmp/gittide-root/a", SubmoduleStatus::Dirty, "ddd2222");
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        model.applySubmodules(QStringLiteral("/tmp/gittide-root"), {a2, b});

        QCOMPARE(removed.count(), 0);   // no destructive rebuild
        QCOMPARE(inserted.count(), 0);
        // Indices (and node identity) preserved for both rows.
        QCOMPARE(model.index(0, 0, topIdx), aIdx);
        QCOMPARE(model.index(1, 0, topIdx), bIdx);
        QCOMPARE(model.data(aIdx, RepoListModel::StatusRole).toInt(),
                 static_cast<int>(SubmoduleStatus::Dirty));
        QCOMPARE(model.data(aIdx, RepoListModel::ShortOidRole).toString(), QStringLiteral("ddd2222"));
    }

    void submodule_row_exposes_branch_dirty_and_ahead()
    {
        using gittide::SubmoduleNode;
        using gittide::SubmoduleStatus;

        RepoListModel model;
        QAbstractItemModelTester tester(&model);
        model.setRepos({RepoRef{.path = "/tmp/gittide-root", .alias = "root"}});

        SubmoduleNode sub;
        sub.name         = "libc";
        sub.path         = "/tmp/gittide-root/libc";
        sub.status       = SubmoduleStatus::Dirty;
        sub.shortOid     = "pin1234";       // pinned
        sub.branch       = "";              // detached
        sub.detached     = true;
        sub.headShortOid = "cur5678";       // current checkout
        sub.dirtyCount   = 3;
        sub.ahead        = 2;
        sub.behind       = 0;
        model.applySubmodules(QStringLiteral("/tmp/gittide-root"), {sub});

        const QModelIndex top = model.index(0, 0);
        const QModelIndex idx = model.index(0, 0, top);
        QVERIFY(idx.isValid());
        QCOMPARE(model.data(idx, RepoListModel::DetachedRole).toBool(), true);
        QCOMPARE(model.data(idx, RepoListModel::DirtyCountRole).toInt(), 3);
        QCOMPARE(model.data(idx, RepoListModel::AheadRole).toInt(), 2);
        QCOMPARE(model.data(idx, RepoListModel::BehindRole).toInt(), 0);
        // The row shows the CURRENT checkout, not the pinned commit.
        QCOMPARE(model.data(idx, RepoListModel::ShortOidRole).toString(), QStringLiteral("cur5678"));
    }

    void applySubmodules_updates_detail_in_place()
    {
        using gittide::SubmoduleNode;
        using gittide::SubmoduleStatus;

        auto makeSub = [](int ahead, int dirty, const char* head)
        {
            SubmoduleNode s;
            s.name         = "libc";
            s.path         = "/tmp/gittide-root/libc";
            s.status       = SubmoduleStatus::Dirty;
            s.shortOid     = "pin1234";
            s.detached     = true;
            s.headShortOid = head;
            s.dirtyCount   = dirty;
            s.ahead        = ahead;
            return s;
        };

        RepoListModel model;
        QAbstractItemModelTester tester(&model);
        model.setRepos({RepoRef{.path = "/tmp/gittide-root", .alias = "root"}});
        model.applySubmodules(QStringLiteral("/tmp/gittide-root"), {makeSub(1, 1, "aaa0001")});

        const QModelIndex top = model.index(0, 0);
        const QModelIndex idx = model.index(0, 0, top);

        // A branch/pin move: same submodule, new ahead/dirty/current-oid.
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        model.applySubmodules(QStringLiteral("/tmp/gittide-root"), {makeSub(3, 5, "bbb0002")});

        QCOMPARE(removed.count(), 0);   // in-place, not a destructive rebuild
        QCOMPARE(inserted.count(), 0);
        QCOMPARE(model.index(0, 0, top), idx); // same node identity
        QCOMPARE(model.data(idx, RepoListModel::AheadRole).toInt(), 3);
        QCOMPARE(model.data(idx, RepoListModel::DirtyCountRole).toInt(), 5);
        QCOMPARE(model.data(idx, RepoListModel::ShortOidRole).toString(), QStringLiteral("bbb0002"));
    }

    void applySubmodules_insertsThenNoOpsWhenUnchanged()
    {
        using gittide::SubmoduleNode;
        using gittide::SubmoduleStatus;

        RepoListModel model;
        QAbstractItemModelTester tester(&model);
        // Path need not exist: setRepos builds no children for a missing path.
        model.setRepos({RepoRef{.path = "/tmp/gittide-parent", .alias = "parent"}});
        const QModelIndex top = model.index(0, 0);
        QCOMPARE(model.rowCount(top), 0);

        SubmoduleNode sub;
        sub.name     = "sub";
        sub.path     = "/tmp/gittide-parent/sub";
        sub.status   = SubmoduleStatus::Clean;
        sub.shortOid = "abc1234";

        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        model.applySubmodules(QStringLiteral("/tmp/gittide-parent"), {sub});
        QCOMPARE(model.rowCount(model.index(0, 0)), 1);
        QCOMPARE(inserted.count(), 1);

        const QModelIndex subIdx = model.index(0, 0, model.index(0, 0));
        QCOMPARE(model.data(subIdx, RepoListModel::OwnerRepoPathRole).toString(),
                 QStringLiteral("/tmp/gittide-parent"));

        // Identical apply → no-op: no insert/remove/dataChanged.
        QSignalSpy inserted2(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed2(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy changed2(&model, &QAbstractItemModel::dataChanged);
        model.applySubmodules(QStringLiteral("/tmp/gittide-parent"), {sub});
        QCOMPARE(inserted2.count(), 0);
        QCOMPARE(removed2.count(), 0);
        QCOMPARE(changed2.count(), 0);

        // Busy flag toggles and emits a dataChanged on the row.
        QSignalSpy busySpy(&model, &QAbstractItemModel::dataChanged);
        model.setSubmoduleBusy(QStringLiteral("/tmp/gittide-parent/sub"), true);
        QCOMPARE(model.data(model.index(0, 0, model.index(0, 0)),
                            RepoListModel::BusyRole).toBool(), true);
        QCOMPARE(busySpy.count(), 1);
    }

    void repo_head_and_dirty_roles_roundtrip()
    {
        RepoListModel m;
        m.setRepos({gittide::RepoRef{.path = "/home/u/api"}});
        const QModelIndex i0 = m.index(0, 0);

        // Defaults.
        QCOMPARE(m.data(i0, RepoListModel::BranchRole).toString(), QString());
        QCOMPARE(m.data(i0, RepoListModel::DetachedRole).toBool(), false);
        QCOMPARE(m.data(i0, RepoListModel::DirtyCountRole).toInt(), 0);
        QCOMPARE(m.data(i0, RepoListModel::HasUpstreamRole).toBool(), false);

        QSignalSpy spy(&m, &QAbstractItemModel::dataChanged);
        QVERIFY(m.setRepoHeadByPath(QStringLiteral("/home/u/api"), QStringLiteral("main"), false, QStringLiteral("abc1234"), 3));
        QCOMPARE(m.data(i0, RepoListModel::BranchRole).toString(), QStringLiteral("main"));
        QCOMPARE(m.data(i0, RepoListModel::DirtyCountRole).toInt(), 3);
        QVERIFY(spy.count() >= 1);

        // Detached: branch empty, detached true, short oid carried in ShortOidRole.
        QVERIFY(m.setRepoHeadByPath(QStringLiteral("/home/u/api"), QString(), true, QStringLiteral("deadbee"), 0));
        QCOMPARE(m.data(i0, RepoListModel::DetachedRole).toBool(), true);
        QCOMPARE(m.data(i0, RepoListModel::ShortOidRole).toString(), QStringLiteral("deadbee"));

        // hasUpstream flows through setSyncCountsByPath.
        QVERIFY(m.setSyncCountsByPath(QStringLiteral("/home/u/api"), 2, 1, true));
        QCOMPARE(m.data(i0, RepoListModel::AheadRole).toInt(), 2);
        QCOMPARE(m.data(i0, RepoListModel::HasUpstreamRole).toBool(), true);
    }

    // The active repository pushes its own head/status into its tree row so the
    // sidebar tracks in-app mutations without waiting for the fleet poll. That
    // repo may be a submodule opened as a first-class repo, at any depth, so the
    // row must be addressable by path — a top-level row index cannot reach it.
    void setRepoHeadByPath_reaches_a_nested_submodule()
    {
        using gittide::SubmoduleNode;
        using gittide::SubmoduleStatus;

        RepoListModel m;
        QAbstractItemModelTester tester(&m);
        m.setRepos({gittide::RepoRef{.path = "/tmp/gittide-root", .alias = "root"}});

        SubmoduleNode nested;
        nested.name     = "nested";
        nested.path     = "/tmp/gittide-root/sub/nested";
        nested.status   = SubmoduleStatus::Clean;
        nested.shortOid = "aaa1111";

        SubmoduleNode sub;
        sub.name     = "sub";
        sub.path     = "/tmp/gittide-root/sub";
        sub.status   = SubmoduleStatus::Clean;
        sub.shortOid = "bbb2222";
        sub.children = {nested};
        m.applySubmodules(QStringLiteral("/tmp/gittide-root"), {sub});

        const QModelIndex subIdx    = m.index(0, 0, m.index(0, 0));
        const QModelIndex nestedIdx = m.index(0, 0, subIdx);
        QVERIFY(nestedIdx.isValid());

        QSignalSpy spy(&m, &QAbstractItemModel::dataChanged);
        QVERIFY(m.setRepoHeadByPath(QStringLiteral("/tmp/gittide-root/sub/nested"),
                                    QStringLiteral("feature"), false, QStringLiteral("ccc3333"), 4));
        QCOMPARE(m.data(nestedIdx, RepoListModel::BranchRole).toString(), QStringLiteral("feature"));
        QCOMPARE(m.data(nestedIdx, RepoListModel::DirtyCountRole).toInt(), 4);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).value<QModelIndex>(), nestedIdx);

        QVERIFY(m.setSyncCountsByPath(QStringLiteral("/tmp/gittide-root/sub/nested"), 2, 1, true));
        QCOMPARE(m.data(nestedIdx, RepoListModel::AheadRole).toInt(), 2);
        QCOMPARE(m.data(nestedIdx, RepoListModel::HasUpstreamRole).toBool(), true);

        // An unknown path is a no-op, not a crash — the repo may have been removed
        // from the project between the refresh starting and its result arriving.
        QVERIFY(!m.setRepoHeadByPath(QStringLiteral("/tmp/gone"), QStringLiteral("x"), false, QString(), 0));
        QVERIFY(!m.setSyncCountsByPath(QStringLiteral("/tmp/gone"), 0, 0, false));
    }

    void setRepoHead_out_of_range_is_noop()
    {
        RepoListModel m;
        m.setRepos({gittide::RepoRef{.path = "/home/u/api"}});
        QVERIFY(!m.setRepoHeadByPath(QStringLiteral("/no/such/repo"), QStringLiteral("x"), false, QString(), 0));
        QCOMPARE(m.topLevelCount(), 1);
    }

    // Root rows must be addressable by path, not by position: a follow-up adds
    // source-group nodes as extra root rows, which would make row index and
    // repo index diverge if any setter still assumed m_roots[row] == repos[row].
    void fetch_state_is_addressable_by_path()
    {
        const auto tmp = std::filesystem::temp_directory_path();
        std::vector<RepoRef> repos{
            RepoRef{.path = tmp.generic_string(), .alias = "one"},
            RepoRef{.path = (tmp / "gittide-two").generic_string(), .alias = "two"},
        };

        RepoListModel m;
        QAbstractItemModelTester tester(&m);
        m.setRepos(repos);

        QVERIFY(m.setFetchStateByPath(QString::fromStdString((tmp / "gittide-two").generic_string()),
                                      RepoListModel::FetchState::Running));

        // The addressed row changed; its sibling did not.
        QCOMPARE(m.data(m.index(1, 0), RepoListModel::FetchStateRole).toInt(),
                 static_cast<int>(RepoListModel::FetchState::Running));
        QCOMPARE(m.data(m.index(0, 0), RepoListModel::FetchStateRole).toInt(),
                 static_cast<int>(RepoListModel::FetchState::Idle));
    }

    void fetch_state_by_unknown_path_is_a_no_op()
    {
        RepoListModel m;
        QAbstractItemModelTester tester(&m);
        m.setRepos({RepoRef{.path = "/tmp/gittide-only", .alias = "only"}});

        QVERIFY(!m.setFetchStateByPath(QStringLiteral("/no/such/repo"), RepoListModel::FetchState::Running));
        QCOMPARE(m.data(m.index(0, 0), RepoListModel::FetchStateRole).toInt(),
                 static_cast<int>(RepoListModel::FetchState::Idle));
    }

    // setRepos runs on the UI thread on every project switch, so it must not do
    // git I/O: opening each repo and reading head/status/sync/submodules there is
    // what stalled the window when switching projects. It now builds the rows from
    // the RepoRefs alone and leaves hydration to the (async) poll.
    void setRepos_does_no_git_io()
    {
        using namespace gittide::test;
        TempRepo repo;
        repo.setIdentity("Test", "test@example.com");
        repo.writeFile("a.txt", "one\n");
        repo.commitAll("c1");
        repo.writeFile("a.txt", "two\n"); // uncommitted change → dirty on disk

        RepoListModel m;
        m.setRepos({gittide::RepoRef{.path = repo.path().generic_string(), .alias = "r"}});
        const QModelIndex i0 = m.index(0, 0);

        // The row exists and renders straight away…
        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(m.data(i0, Qt::DisplayRole).toString(), QStringLiteral("r"));
        QCOMPARE(m.data(i0, RepoListModel::MissingRole).toBool(), false);
        // …but nothing was read from git: those fields stay at their defaults
        // until ProjectController's poll fills them in off-thread.
        QVERIFY(m.data(i0, RepoListModel::BranchRole).toString().isEmpty());
        QCOMPARE(m.data(i0, RepoListModel::DirtyCountRole).toInt(), 0);
        QCOMPARE(m.data(i0, RepoListModel::HasUpstreamRole).toBool(), false);
        QCOMPARE(m.rowCount(i0), 0); // submodule children arrive with the poll too
    }
};

#include "test_repo_list_model.moc"

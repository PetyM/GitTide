#include <QAbstractItemModelTester>
#include <QObject>
#include <QtTest/QtTest>
#include <filesystem>
#include <vector>

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
    }
};

#include "test_repo_list_model.moc"

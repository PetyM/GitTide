#include <QObject>
#include <QtTest/QtTest>
#include <QAbstractItemModelTester>
#include <filesystem>
#include <vector>

#include "gittide/projectstore.hpp"
#include "gittide/ui/repolistmodel.hpp"

using gittide::RepoRef;
using gittide::ui::RepoListModel;

class TestRepoListModel : public QObject {
    Q_OBJECT
private slots:
    void exposes_alias_path_and_missing_flag() {
        const auto tmp = std::filesystem::temp_directory_path();   // exists
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
        QCOMPARE(model.data(model.index(1, 0), RepoListModel::PathRole).toString(),
                 QStringLiteral("/no/such/path/gittide-test"));
    }

    void empty_alias_falls_back_to_path() {
        std::vector<RepoRef> repos{ RepoRef{.path = "/home/u/api-server", .alias = ""} };
        RepoListModel model;
        model.setRepos(repos);
        QCOMPARE(model.data(model.index(0, 0), Qt::DisplayRole).toString(),
                 QStringLiteral("/home/u/api-server"));
    }

    void tree_model_parent_of_top_level_item_is_invalid() {
        std::vector<RepoRef> repos{ RepoRef{.path = "/home/u/api", .alias = "api"} };
        RepoListModel model;
        QAbstractItemModelTester tester(&model);
        model.setRepos(repos);

        QVERIFY(!model.parent(model.index(0, 0)).isValid());
    }

    void tree_model_top_level_items_have_no_children() {
        std::vector<RepoRef> repos{ RepoRef{.path = "/home/u/api", .alias = "api"} };
        RepoListModel model;
        model.setRepos(repos);

        const QModelIndex item = model.index(0, 0);
        QCOMPARE(model.rowCount(item), 0);
    }

    void tree_model_index_with_valid_parent_is_invalid() {
        std::vector<RepoRef> repos{ RepoRef{.path = "/home/u/api", .alias = "api"} };
        RepoListModel model;
        model.setRepos(repos);

        const QModelIndex parent = model.index(0, 0);
        QVERIFY(!model.index(0, 0, parent).isValid());
    }
};

#include "test_repo_list_model.moc"

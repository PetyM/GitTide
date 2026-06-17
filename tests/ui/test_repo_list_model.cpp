#include <QObject>
#include <QtTest/QtTest>
#include <QAbstractItemModelTester>
#include <filesystem>
#include <vector>

#include "gitgui/ProjectStore.hpp"
#include "gitgui/ui/RepoListModel.hpp"

using gitgui::RepoRef;
using gitgui::ui::RepoListModel;

class TestRepoListModel : public QObject {
    Q_OBJECT
private slots:
    void exposes_alias_path_and_missing_flag() {
        const auto tmp = std::filesystem::temp_directory_path();   // exists
        std::vector<RepoRef> repos{
            RepoRef{.path = tmp.generic_string(), .alias = "present"},
            RepoRef{.path = "/no/such/path/gitgui-test", .alias = "gone"},
        };

        RepoListModel model;
        QAbstractItemModelTester tester(&model);
        model.setRepos(repos);

        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(), QStringLiteral("present"));
        QCOMPARE(model.data(model.index(0), RepoListModel::MissingRole).toBool(), false);
        QCOMPARE(model.data(model.index(1), RepoListModel::MissingRole).toBool(), true);
        QCOMPARE(model.data(model.index(1), RepoListModel::PathRole).toString(),
                 QStringLiteral("/no/such/path/gitgui-test"));
    }

    void empty_alias_falls_back_to_path() {
        std::vector<RepoRef> repos{ RepoRef{.path = "/home/u/api-server", .alias = ""} };
        RepoListModel model;
        model.setRepos(repos);
        QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(),
                 QStringLiteral("/home/u/api-server"));
    }
};

#include "test_repo_list_model.moc"

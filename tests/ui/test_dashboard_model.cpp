#include <QObject>
#include <QtTest/QtTest>
#include <QAbstractItemModelTester>
#include <filesystem>
#include <fstream>

#include <git2.h>

#include "gittide/projectstore.hpp"
#include "gittide/ui/dashboardmodel.hpp"

using gittide::RepoRef;
using gittide::ui::DashboardModel;

namespace {
std::filesystem::path make_repo_with_untracked() {
    auto dir = std::filesystem::temp_directory_path() /
               ("gittide-dash-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_libgit2_init();
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_repository_free(raw);
    git_libgit2_shutdown();
    std::ofstream(dir / "untracked.txt") << "hello";
    return dir;
}
}  // namespace

class TestDashboardModel : public QObject {
    Q_OBJECT
private slots:
    void aggregates_change_counts_and_missing() {
        const auto repo = make_repo_with_untracked();
        std::vector<RepoRef> repos{
            RepoRef{.path = repo.generic_string(), .alias = "present"},
            RepoRef{.path = "/no/such/gittide-dash", .alias = "gone"},
        };

        DashboardModel model;
        QAbstractItemModelTester tester(&model);
        model.refresh(repos);

        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(), QStringLiteral("present"));
        QCOMPARE(model.data(model.index(0), DashboardModel::ChangeCountRole).toInt(), 1);
        QCOMPARE(model.data(model.index(0), DashboardModel::MissingRole).toBool(), false);
        QCOMPARE(model.data(model.index(1), DashboardModel::MissingRole).toBool(), true);

        std::filesystem::remove_all(repo);
    }
};

#include "test_dashboard_model.moc"

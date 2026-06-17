#include <QObject>
#include <QSignalSpy>
#include <QtTest/QtTest>
#include <filesystem>
#include <fstream>
#include <git2.h>
#include <qcorotask.h>

#include "gittide/projectstore.hpp"
#include "gittide/ui/dashboardmodel.hpp"

using gittide::ui::DashboardModel;

namespace async_test_helpers {
std::filesystem::path make_repo_with_untracked()
{
    git_libgit2_init();
    auto dir =
        std::filesystem::temp_directory_path() / ("gittide-da-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_repository_free(raw);
    {
        std::ofstream(dir / "new.txt") << "x\n";
    } // 1 untracked change
    git_libgit2_shutdown();
    return dir;
}
} // namespace async_test_helpers

class TestDashboardAsync : public QObject
{
    Q_OBJECT
private slots:
    void refresh_async_fans_out_and_reports()
    {
        const auto good                     = async_test_helpers::make_repo_with_untracked();
        std::vector<gittide::RepoRef> repos = {
            gittide::RepoRef{.path = good.generic_string(), .alias = "good"},
            gittide::RepoRef{.path = "/no/such/gittide-dash", .alias = "gone"},
        };

        DashboardModel model;
        QSignalSpy spy(&model, &DashboardModel::refreshed);
        QCoro::waitFor(model.refreshAsync(repos));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(model.rowCount(), 2);

        const auto idx0 = model.index(0);
        QCOMPARE(model.data(idx0, DashboardModel::ChangeCountRole).toInt(), 1);
        QCOMPARE(model.data(idx0, DashboardModel::MissingRole).toBool(), false);

        const auto idx1 = model.index(1);
        QCOMPARE(model.data(idx1, DashboardModel::MissingRole).toBool(), true);

        std::filesystem::remove_all(good);
    }
};

#include "test_dashboard_async.moc"

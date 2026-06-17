#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <filesystem>
#include <fstream>

#include <git2.h>
#include <qcorotask.h>

#include "gitgui/ui/RepoController.hpp"
#include "gitgui/ui/Metatypes.hpp"

using gitgui::ui::RepoController;

namespace repo_controller_test {
std::filesystem::path make_empty_repo() {
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() /
               ("gitgui-rc-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_repository_free(raw);
    git_libgit2_shutdown();
    return dir;
}

// Repo with a committed a.txt then a local modification (1 unstaged change).
std::filesystem::path make_dirty_repo() {
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() /
               ("gitgui-rcd-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_config* cfg = nullptr;
    git_repository_config(&cfg, raw);
    git_config_set_string(cfg, "user.name", "T");
    git_config_set_string(cfg, "user.email", "t@e.x");
    git_config_free(cfg);
    { std::ofstream(dir / "a.txt") << "one\n"; }
    git_index* idx = nullptr; git_repository_index(&idx, raw);
    git_index_add_bypath(idx, "a.txt"); git_index_write(idx);
    git_oid tree_oid; git_index_write_tree(&tree_oid, idx);
    git_tree* tree = nullptr; git_tree_lookup(&tree, raw, &tree_oid);
    git_signature* sig = nullptr; git_signature_now(&sig, "T", "t@e.x");
    git_oid commit_oid;
    git_commit_create_v(&commit_oid, raw, "HEAD", sig, sig, nullptr, "init", tree, 0);
    git_signature_free(sig); git_tree_free(tree); git_index_free(idx);
    git_repository_free(raw);
    { std::ofstream(dir / "a.txt") << "one\ntwo\n"; }
    git_libgit2_shutdown();
    return dir;
}
}  // namespace repo_controller_test

class TestRepoController : public QObject {
    Q_OBJECT
private slots:
    void open_existing_repo_succeeds() {
        const auto dir = repo_controller_test::make_empty_repo();
        RepoController controller;
        QSignalSpy ok(&controller, &RepoController::repoOpened);
        QSignalSpy bad(&controller, &RepoController::repoFailed);
        controller.open(QString::fromStdString(dir.generic_string()));
        QCOMPARE(ok.count(), 1);
        QCOMPARE(bad.count(), 0);
        QVERIFY(controller.isOpen());
        std::filesystem::remove_all(dir);
    }

    void open_missing_repo_fails() {
        RepoController controller;
        QSignalSpy ok(&controller, &RepoController::repoOpened);
        QSignalSpy bad(&controller, &RepoController::repoFailed);
        controller.open(QStringLiteral("/no/such/gitgui-repo"));
        QCOMPARE(ok.count(), 0);
        QCOMPARE(bad.count(), 1);
        QVERIFY(!controller.isOpen());
    }

    void refresh_status_emits_changes() {
        const auto dir = repo_controller_test::make_dirty_repo();
        RepoController controller;
        controller.open(QString::fromStdString(dir.generic_string()));
        QSignalSpy spy(&controller, &RepoController::statusChanged);
        QCoro::waitFor(controller.refreshStatus());
        QCOMPARE(spy.count(), 1);
        const auto files = spy.at(0).at(0).value<std::vector<gitgui::FileStatus>>();
        QCOMPARE(static_cast<int>(files.size()), 1);
        std::filesystem::remove_all(dir);
    }

    void stage_then_status_shows_staged() {
        const auto dir = repo_controller_test::make_dirty_repo();
        RepoController controller;
        controller.open(QString::fromStdString(dir.generic_string()));
        QSignalSpy spy(&controller, &RepoController::statusChanged);
        QCoro::waitFor(controller.stage(gitgui::StageSelection{.path = "a.txt"}));
        QVERIFY(spy.count() >= 1);  // stage chains a refreshStatus
        const auto files = spy.at(spy.count() - 1).at(0).value<std::vector<gitgui::FileStatus>>();
        QCOMPARE(static_cast<int>(files.size()), 1);
        QVERIFY(gitgui::has_flag(files[0].flags, gitgui::StatusFlag::IndexModified));
        std::filesystem::remove_all(dir);
    }

    void refresh_diff_emits_diff_ready() {
        const auto dir = repo_controller_test::make_dirty_repo();
        RepoController controller;
        controller.open(QString::fromStdString(dir.generic_string()));
        QSignalSpy spy(&controller, &RepoController::diffReady);
        QCoro::waitFor(controller.refreshDiff(QStringLiteral("a.txt"),
                                              gitgui::DiffTarget::WorktreeVsIndex));
        QCOMPARE(spy.count(), 1);
        const auto result = spy.at(0).at(1).value<gitgui::DiffResult>();
        QVERIFY(!result.hunks.empty());
        std::filesystem::remove_all(dir);
    }
};

#include "test_repo_controller.moc"

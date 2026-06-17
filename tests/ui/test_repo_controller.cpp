#include <QObject>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <filesystem>

#include <git2.h>

#include "gitgui/ui/RepoController.hpp"

using gitgui::ui::RepoController;

namespace {
// Create a throwaway initialized repo; returns its path.
std::filesystem::path make_repo() {
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
}  // namespace

class TestRepoController : public QObject {
    Q_OBJECT
private slots:
    void open_existing_repo_succeeds() {
        const auto dir = make_repo();
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
};

#include "test_repo_controller.moc"

#include <QtTest>
#include <QSignalSpy>
#include <QRandomGenerator>
#include <filesystem>
#include <fstream>
#include <git2.h>

#include "gittide/ui/repocontroller.hpp"

using gittide::ui::RepoController;

namespace reword_ctrl_test {
inline std::filesystem::path make_repo_with_commit(const char* message)
{
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() / ("gittide-rwc-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_config* cfg = nullptr; git_repository_config(&cfg, raw);
    git_config_set_string(cfg, "user.name", "T");
    git_config_set_string(cfg, "user.email", "t@e.x");
    git_config_free(cfg);
    { std::ofstream(dir / "a.txt") << "one\n"; }
    git_index* idx = nullptr; git_repository_index(&idx, raw);
    git_index_add_bypath(idx, "a.txt"); git_index_write(idx);
    git_oid tree_oid; git_index_write_tree(&tree_oid, idx);
    git_tree* tree = nullptr; git_tree_lookup(&tree, raw, &tree_oid);
    git_signature* sig = nullptr; git_signature_now(&sig, "T", "t@e.x");
    git_oid commit_oid; git_commit_create_v(&commit_oid, raw, "HEAD", sig, sig, nullptr, message, tree, 0);
    git_signature_free(sig); git_tree_free(tree); git_index_free(idx);
    git_repository_free(raw); git_libgit2_shutdown();
    return dir;
}
}

class TestRepoControllerReword : public QObject
{
    Q_OBJECT
private slots:
    void reword_changes_head_message_and_emits_committed()
    {
        const auto dir = reword_ctrl_test::make_repo_with_commit("old subject\n");
        RepoController c;
        c.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(c.isOpen());

        QSignalSpy committed(&c, &RepoController::committed);
        c.rewordHead(QStringLiteral("new subject\n"));
        QVERIFY(committed.wait(3000));

        QSignalSpy msgReady(&c, &RepoController::commitMessageReady);
        const QString head = committed.takeFirst().at(0).toString();
        c.requestCommitMessage(head);
        QVERIFY(msgReady.wait(3000));
        QCOMPARE(msgReady.takeFirst().at(1).toString(), QStringLiteral("new subject\n"));

        std::filesystem::remove_all(dir);
    }
};

#include "test_repocontroller_reword.moc"


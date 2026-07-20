#include <QtTest>
#include <QSignalSpy>
#include <QRandomGenerator>
#include <filesystem>
#include <fstream>
#include <git2.h>

#include "gittide/ui/repocontroller.hpp"

using gittide::ui::RepoController;

namespace undo_ctrl_test {
// Build a repo with two commits ("first" adds a.txt, "second" adds b.txt) and
// return its path plus the oid of the first (parent) commit.
inline std::filesystem::path make_repo_with_two_commits(std::string& firstOidOut)
{
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() / ("gittide-undoc-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_config* cfg = nullptr; git_repository_config(&cfg, raw);
    git_config_set_string(cfg, "user.name", "T");
    git_config_set_string(cfg, "user.email", "t@e.x");
    git_config_free(cfg);

    auto commitFile = [&](const char* name, const char* msg, git_oid* out)
    {
        { std::ofstream(dir / name) << "x\n"; }
        git_index* idx = nullptr; git_repository_index(&idx, raw);
        git_index_add_bypath(idx, name); git_index_write(idx);
        git_oid tree_oid; git_index_write_tree(&tree_oid, idx);
        git_tree* tree = nullptr; git_tree_lookup(&tree, raw, &tree_oid);
        git_signature* sig = nullptr; git_signature_now(&sig, "T", "t@e.x");
        git_commit* parent = nullptr;
        git_oid headoid;
        git_commit* parents[1] = { nullptr };
        size_t parent_count    = 0;
        if (git_reference_name_to_id(&headoid, raw, "HEAD") == 0
            && git_commit_lookup(&parent, raw, &headoid) == 0)
        {
            parents[0]   = parent;
            parent_count = 1;
        }
        git_commit_create(out, raw, "HEAD", sig, sig, nullptr, msg, tree, parent_count, parents);
        if (parent) git_commit_free(parent);
        git_signature_free(sig); git_tree_free(tree); git_index_free(idx);
    };

    git_oid first, second;
    commitFile("a.txt", "first\n", &first);
    commitFile("b.txt", "second\n", &second);

    char buf[GIT_OID_SHA1_HEXSIZE + 1] = {0};
    git_oid_tostr(buf, sizeof(buf), &first);
    firstOidOut = buf;

    git_repository_free(raw); git_libgit2_shutdown();
    return dir;
}
} // namespace undo_ctrl_test

class TestRepoControllerUndo : public QObject
{
    Q_OBJECT
private slots:
    void undo_moves_head_to_parent_and_stages_the_change()
    {
        std::string firstOid;
        const auto dir = undo_ctrl_test::make_repo_with_two_commits(firstOid);
        RepoController c;
        c.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(c.isOpen());

        // branchesChanged is the last refresh the undo flow performs.
        QSignalSpy doneSpy(&c, &RepoController::branchesChanged);
        c.undoLastCommit();
        QVERIFY(doneSpy.wait(15000));

        git_libgit2_init();
        git_repository* raw = nullptr;
        QCOMPARE(git_repository_open(&raw, dir.generic_string().c_str()), 0);

        // HEAD now points at the parent ("first") commit.
        git_oid head;
        QCOMPARE(git_reference_name_to_id(&head, raw, "HEAD"), 0);
        char buf[GIT_OID_SHA1_HEXSIZE + 1] = {0};
        git_oid_tostr(buf, sizeof(buf), &head);
        QCOMPARE(QString::fromUtf8(buf), QString::fromStdString(firstOid));

        // The undone commit's file (b.txt) is now staged (soft reset keeps the index).
        unsigned int flags = 0;
        QCOMPARE(git_status_file(&flags, raw, "b.txt"), 0);
        QVERIFY(flags & GIT_STATUS_INDEX_NEW);

        git_repository_free(raw);
        git_libgit2_shutdown();

        std::filesystem::remove_all(dir);
    }
};

#include "test_repocontroller_undo.moc"

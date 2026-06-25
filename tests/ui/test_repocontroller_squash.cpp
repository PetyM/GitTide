#include <QtTest>
#include <QSignalSpy>
#include <QRandomGenerator>
#include <filesystem>
#include <fstream>
#include <vector>
#include <git2.h>

#include "gittide/ui/repocontroller.hpp"

using gittide::ui::RepoController;

namespace squash_ctrl_test {
// Build a repo with `n` commits (c0..c(n-1), each adds f<i>.txt). Returns the
// repo path; fills oids newest-first (oids[0] == HEAD == c(n-1)).
inline std::filesystem::path make_repo_with_commits(int n, std::vector<std::string>& oidsNewestFirst)
{
    git_libgit2_init();
    auto dir = std::filesystem::temp_directory_path() / ("gittide-sqc-" + std::to_string(::QRandomGenerator::global()->generate()));
    std::filesystem::create_directories(dir);
    git_repository* raw = nullptr;
    git_repository_init(&raw, dir.generic_string().c_str(), 0);
    git_config* cfg = nullptr; git_repository_config(&cfg, raw);
    git_config_set_string(cfg, "user.name", "T");
    git_config_set_string(cfg, "user.email", "t@e.x");
    git_config_free(cfg);

    std::vector<std::string> oldestFirst;
    for (int i = 0; i < n; ++i)
    {
        const std::string name = "f" + std::to_string(i) + ".txt";
        { std::ofstream(dir / name) << "x\n"; }
        git_index* idx = nullptr; git_repository_index(&idx, raw);
        git_index_add_bypath(idx, name.c_str()); git_index_write(idx);
        git_oid tree_oid; git_index_write_tree(&tree_oid, idx);
        git_tree* tree = nullptr; git_tree_lookup(&tree, raw, &tree_oid);
        git_signature* sig = nullptr; git_signature_now(&sig, "T", "t@e.x");
        git_commit* parent = nullptr; git_oid parent_oid;
        git_commit* parents[1] = { nullptr }; size_t parent_count = 0;
        if (git_reference_name_to_id(&parent_oid, raw, "HEAD") == 0
            && git_commit_lookup(&parent, raw, &parent_oid) == 0)
        {
            parents[0] = parent; parent_count = 1;
        }
        git_oid commit_oid;
        const std::string msg = "c" + std::to_string(i) + "\n";
        git_commit_create(&commit_oid, raw, "HEAD", sig, sig, nullptr, msg.c_str(), tree, parent_count, parents);
        char buf[GIT_OID_SHA1_HEXSIZE + 1] = {0};
        git_oid_tostr(buf, sizeof(buf), &commit_oid);
        oldestFirst.push_back(buf);
        if (parent) git_commit_free(parent);
        git_signature_free(sig); git_tree_free(tree); git_index_free(idx);
    }
    git_repository_free(raw); git_libgit2_shutdown();

    oidsNewestFirst.assign(oldestFirst.rbegin(), oldestFirst.rend());
    return dir;
}
} // namespace squash_ctrl_test

class TestRepoControllerSquash : public QObject
{
    Q_OBJECT
private slots:
    // Selecting a contiguous range emits a todo with the oldest as pick and the
    // rest as squash, oldest-first, with base = parent of the oldest selected.
    void contiguous_selection_seeds_pick_then_squash()
    {
        std::vector<std::string> oids; // newest-first: [c2, c1, c0]
        const auto dir = squash_ctrl_test::make_repo_with_commits(3, oids);
        RepoController c;
        c.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(c.isOpen());

        QSignalSpy ready(&c, &RepoController::rebaseTodoReady);
        // Squash the two newest (c2 = HEAD, c1). Oldest selected is c1; base = c0.
        c.buildSquashTodo(QStringList{ QString::fromStdString(oids[0]),
                                       QString::fromStdString(oids[1]) });
        QVERIFY(ready.wait(3000));

        const auto args    = ready.takeFirst();
        const QString base = args.at(0).toString();
        const auto entries = args.at(1).toList();
        QCOMPARE(base, QString::fromStdString(oids[2]));        // c0
        QCOMPARE(entries.size(), 2);
        // Oldest-first: c1 (pick), then c2 (squash).
        QCOMPARE(entries.at(0).toMap().value("oid").toString(), QString::fromStdString(oids[1]));
        QCOMPARE(entries.at(0).toMap().value("action").toString(), QStringLiteral("pick"));
        QCOMPARE(entries.at(1).toMap().value("oid").toString(), QString::fromStdString(oids[0]));
        QCOMPARE(entries.at(1).toMap().value("action").toString(), QStringLiteral("squash"));

        std::filesystem::remove_all(dir);
    }

    // A non-contiguous selection (c2 + c0, skipping c1) is rejected.
    void non_contiguous_selection_is_rejected()
    {
        std::vector<std::string> oids; // [c2, c1, c0]
        const auto dir = squash_ctrl_test::make_repo_with_commits(3, oids);
        RepoController c;
        c.open(QString::fromStdString(dir.generic_string()));
        QVERIFY(c.isOpen());

        QSignalSpy ready(&c, &RepoController::rebaseTodoReady);
        QSignalSpy failed(&c, &RepoController::operationFailed);
        c.buildSquashTodo(QStringList{ QString::fromStdString(oids[0]),
                                       QString::fromStdString(oids[2]) }); // c2 + c0
        QVERIFY(failed.wait(3000));
        QCOMPARE(ready.count(), 0);

        std::filesystem::remove_all(dir);
    }
};

#include "test_repocontroller_squash.moc"

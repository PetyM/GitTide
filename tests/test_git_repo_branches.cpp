#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <filesystem>

#include <git2.h>

TEST_CASE("BranchInfo defaults to a local branch with no upstream/worktree", "[branches]")
{
    gittide::BranchInfo b{};
    REQUIRE(b.kind == gittide::BranchKind::Local);
    REQUIRE(b.upstream.empty());
    REQUIRE(b.worktreePath.empty());
    REQUIRE_FALSE(b.isHead);
}

using gittide::GitRepo;

namespace {
bool has(const std::vector<gittide::BranchInfo>& v, const std::string& n)
{
    return std::any_of(v.begin(), v.end(), [&](const auto& b) { return b.name == n; });
}
}

TEST_CASE("branches lists remote-tracking refs and local upstream", "[branches]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");

    // Fabricate refs/remotes/origin/main at HEAD and set the local branch's
    // upstream to it, using raw libgit2 on the same repo.
    git_repository* raw = nullptr;
    REQUIRE(git_repository_open(&raw, tmp.path().generic_string().c_str()) == 0);
    // A configured remote gives git_branch_upstream() a fetch refspec to map
    // refs/heads/* onto refs/remotes/origin/*.
    git_remote* remote = nullptr;
    REQUIRE(git_remote_create(&remote, raw, "origin", "https://example.invalid/r.git") == 0);
    git_remote_free(remote);
    git_oid head_oid;
    REQUIRE(git_reference_name_to_id(&head_oid, raw, "HEAD") == 0);
    git_reference* remote_ref = nullptr;
    REQUIRE(git_reference_create(&remote_ref, raw, "refs/remotes/origin/main", &head_oid, 0, "test") == 0);
    git_reference_free(remote_ref);

    git_reference* head_ref = nullptr;
    REQUIRE(git_repository_head(&head_ref, raw) == 0);
    const std::string local = git_reference_shorthand(head_ref);
    git_reference* local_ref = nullptr;
    REQUIRE(git_branch_lookup(&local_ref, raw, local.c_str(), GIT_BRANCH_LOCAL) == 0);
    git_branch_set_upstream(local_ref, "origin/main"); // best-effort
    git_reference_free(local_ref);
    git_reference_free(head_ref);
    git_repository_free(raw);

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    auto list = repo->branches();
    REQUIRE(list.has_value());

    const auto local_it = std::find_if(list->begin(), list->end(), [&](const auto& b) {
        return b.kind == gittide::BranchKind::Local && b.name == local;
    });
    REQUIRE(local_it != list->end());
    REQUIRE(local_it->upstream == "origin/main");

    const auto remote_it = std::find_if(list->begin(), list->end(),
                                        [](const auto& b) { return b.kind == gittide::BranchKind::RemoteTracking; });
    REQUIRE(remote_it != list->end());
    REQUIRE(remote_it->name == "origin/main");
    REQUIRE_FALSE(remote_it->isHead);
}

TEST_CASE("createBranch from HEAD makes a listable branch", "[branches]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    REQUIRE(repo->createBranch("feature", "").has_value());
    auto list = repo->branches();
    REQUIRE(list.has_value());
    REQUIRE(has(*list, "feature"));
    // not switched: HEAD unchanged
    REQUIRE_FALSE(std::any_of(list->begin(), list->end(),
                              [](const auto& b) { return b.isHead && b.name == "feature"; }));
}

TEST_CASE("createBranch rejects an invalid name", "[branches]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto r = repo->createBranch("bad name~^:", "");
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("branches lists the default branch and marks HEAD", "[branches]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto list = repo->branches();
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 1);
    REQUIRE((*list)[0].isHead);

    auto h = repo->head();
    REQUIRE(h.has_value());
    REQUIRE_FALSE(h->detached);
    REQUIRE(h->branch == (*list)[0].name);
    REQUIRE(h->oid.size() == 40);
}

TEST_CASE("deleteBranch removes a merged branch but blocks the current one", "[branches]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    const std::string cur = repo->head()->branch;

    REQUIRE(repo->createBranch("merged", "").has_value()); // same tip => merged
    REQUIRE(repo->deleteBranch("merged", /*force=*/false).has_value());
    REQUIRE_FALSE(has(*repo->branches(), "merged"));

    REQUIRE_FALSE(repo->deleteBranch(cur, false).has_value()); // current is blocked
}

TEST_CASE("renameBranch renames and rejects invalid names", "[branches]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->createBranch("old", "").has_value());

    REQUIRE(repo->renameBranch("old", "new", false).has_value());
    REQUIRE(has(*repo->branches(), "new"));
    REQUIRE_FALSE(has(*repo->branches(), "old"));

    REQUIRE_FALSE(repo->renameBranch("new", "bad~name", false).has_value());
}

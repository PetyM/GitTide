#include <catch2/catch_test_macros.hpp>
#include <git2.h>

#include "gittide/gitrepo.hpp"
#include "gittide/pathutil.hpp"
#include "support/temprepo.hpp"

using gittide::GitRepo;

TEST_CASE("rewordHead rewrites the HEAD message, keeping tree and parent", "[reword]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Ada", "ada@example.com");
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("first");
    tmp.writeFile("b.txt", "two\n");
    tmp.commitAll("second original");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    // Capture the pre-reword tree + parent of HEAD via libgit2.
    git_repository* r = nullptr;
    REQUIRE(git_repository_open(&r, gittide::toGitPath(tmp.path()).c_str()) == 0);
    git_oid head;
    REQUIRE(git_reference_name_to_id(&head, r, "HEAD") == 0);
    git_commit* before = nullptr;
    REQUIRE(git_commit_lookup(&before, r, &head) == 0);
    const git_oid treeBefore = *git_commit_tree_id(before);
    git_commit_free(before);

    auto oid = repo->rewordHead("second reworded\n\nbody line\n");
    REQUIRE(oid.has_value());
    REQUIRE(oid->size() == 40);

    git_oid head2;
    REQUIRE(git_reference_name_to_id(&head2, r, "HEAD") == 0);
    git_commit* after = nullptr;
    REQUIRE(git_commit_lookup(&after, r, &head2) == 0);
    REQUIRE(std::string(git_commit_message(after)) == "second reworded\n\nbody line\n");
    REQUIRE(git_oid_equal(git_commit_tree_id(after), &treeBefore) == 1); // tree unchanged
    REQUIRE(git_commit_parentcount(after) == 1);                          // parent kept
    git_commit_free(after);
    git_repository_free(r);
}

TEST_CASE("commitMessage returns the full summary + body", "[reword]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Ada", "ada@example.com");
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("subject line\n\nlong body\nsecond body line\n");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    auto hist = repo->log();
    REQUIRE(hist.has_value());
    REQUIRE(hist->size() == 1);

    auto msg = repo->commitMessage(hist->front().oid);
    REQUIRE(msg.has_value());
    REQUIRE(*msg == "subject line\n\nlong body\nsecond body line\n");
}

TEST_CASE("rewordHead errors on an unborn branch", "[reword]")
{
    gittide::test::TempRepo tmp; // fresh init, no commit => unborn HEAD
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    auto oid = repo->rewordHead("nope");
    REQUIRE_FALSE(oid.has_value());
}

TEST_CASE("rewordHead on a detached HEAD returns an error", "[reword]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Ada", "ada@example.com");
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("first");
    tmp.writeFile("b.txt", "two\n");
    tmp.commitAll("second");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    // Get the oid of the first commit (oldest = last in log()).
    auto hist = repo->log();
    REQUIRE(hist.has_value());
    REQUIRE(hist->size() == 2);
    const std::string firstOid = hist->back().oid;

    // Detach HEAD at the first commit.
    auto co = repo->checkoutCommit(firstOid);
    REQUIRE(co.has_value());

    // Reword on a detached HEAD must fail.
    auto result = repo->rewordHead("should fail");
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("rewordHead does not fold a dirty worktree into the amended commit", "[reword]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Ada", "ada@example.com");
    tmp.writeFile("a.txt", "original\n");
    tmp.commitAll("original commit");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    // Capture the pre-reword tree oid.
    git_repository* r = nullptr;
    REQUIRE(git_repository_open(&r, gittide::toGitPath(tmp.path()).c_str()) == 0);
    git_oid headOid;
    REQUIRE(git_reference_name_to_id(&headOid, r, "HEAD") == 0);
    git_commit* before = nullptr;
    REQUIRE(git_commit_lookup(&before, r, &headOid) == 0);
    const git_oid treeBefore = *git_commit_tree_id(before);
    git_commit_free(before);
    git_repository_free(r);

    // Dirty the worktree (don't stage or commit the change).
    tmp.writeFile("a.txt", "original\ndirty change\n");

    // Reword must succeed but must NOT include the worktree change.
    auto result = repo->rewordHead("new message");
    REQUIRE(result.has_value());

    // Verify that the amended commit's tree is identical to the pre-reword tree.
    git_repository* r2 = nullptr;
    REQUIRE(git_repository_open(&r2, gittide::toGitPath(tmp.path()).c_str()) == 0);
    git_oid headOid2;
    REQUIRE(git_reference_name_to_id(&headOid2, r2, "HEAD") == 0);
    git_commit* after = nullptr;
    REQUIRE(git_commit_lookup(&after, r2, &headOid2) == 0);
    REQUIRE(git_oid_equal(git_commit_tree_id(after), &treeBefore) == 1);
    git_commit_free(after);
    git_repository_free(r2);
}

TEST_CASE("rewordHead preserves a submodule pointer", "[reword]")
{
    gittide::test::TempRepo child;
    child.writeFile("c.txt", "child\n");
    child.commitAll("child commit");

    gittide::test::TempRepo parent;
    parent.setIdentity("Ada", "ada@example.com");
    parent.writeFile("top.txt", "parent\n");
    parent.commitAll("parent init");
    parent.addSubmodule("libchild", child.path());
    parent.commitAll("add submodule");

    auto repo = GitRepo::open(parent.path());
    REQUIRE(repo.has_value());

    // Capture the pre-reword tree oid.
    git_repository* r = nullptr;
    REQUIRE(git_repository_open(&r, gittide::toGitPath(parent.path()).c_str()) == 0);
    git_oid headOid;
    REQUIRE(git_reference_name_to_id(&headOid, r, "HEAD") == 0);
    git_commit* before = nullptr;
    REQUIRE(git_commit_lookup(&before, r, &headOid) == 0);
    const git_oid treeBefore = *git_commit_tree_id(before);
    git_commit_free(before);
    git_repository_free(r);

    // Reword must preserve the submodule gitlink (tree unchanged).
    auto result = repo->rewordHead("rewarded with submodule");
    REQUIRE(result.has_value());

    git_repository* r2 = nullptr;
    REQUIRE(git_repository_open(&r2, gittide::toGitPath(parent.path()).c_str()) == 0);
    git_oid headOid2;
    REQUIRE(git_reference_name_to_id(&headOid2, r2, "HEAD") == 0);
    git_commit* after = nullptr;
    REQUIRE(git_commit_lookup(&after, r2, &headOid2) == 0);
    REQUIRE(git_oid_equal(git_commit_tree_id(after), &treeBefore) == 1);
    git_commit_free(after);
    git_repository_free(r2);
}

TEST_CASE("commitMessage on a bad oid returns an error", "[reword]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("root");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    // All-zeros oid — no such commit.
    const std::string badOid(40, '0');
    auto result = repo->commitMessage(badOid);
    REQUIRE_FALSE(result.has_value());

    // Malformed (too short) oid.
    auto result2 = repo->commitMessage("deadbeef");
    REQUIRE_FALSE(result2.has_value());
}

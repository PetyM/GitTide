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

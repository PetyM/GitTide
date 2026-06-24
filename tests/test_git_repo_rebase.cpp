#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <git2.h> // for oid parent inspection in assertions

using gittide::GitRepo;

TEST_CASE("rebaseState reports not-in-progress for a clean repo", "[rebase]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("c1");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto st = repo->rebaseState();
    REQUIRE_FALSE(st.inProgress);
    REQUIRE(st.current == 0);
    REQUIRE(st.total == 0);
    REQUIRE(st.conflictedPaths.empty());
}

TEST_CASE("startRebase replays a linear branch onto the target", "[rebase]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("base.txt", "base\n");
    tmp.commitAll("c0");                       // master @ c0

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->createBranch("feature", "").has_value());
    REQUIRE(repo->checkoutBranch("feature").has_value());
    tmp.writeFile("f.txt", "feature\n");
    tmp.commitAll("c1 on feature");            // feature @ c1

    // master advances on a different file (no conflict).
    REQUIRE(repo->checkoutBranch("master").has_value());
    tmp.writeFile("m.txt", "main\n");
    tmp.commitAll("c2 on master");             // master @ c2
    REQUIRE(repo->checkoutBranch("feature").has_value());

    auto out = repo->startRebase("master");
    REQUIRE(out.has_value());
    REQUIRE_FALSE(out->conflicted);
    REQUIRE_FALSE(repo->rebaseState().inProgress);

    // feature's tip now sits on top of master: m.txt and f.txt both present.
    auto st = repo->status();
    REQUIRE(st.has_value());
    auto hist = repo->log(10);
    REQUIRE(hist.has_value());
    // c1's content replayed: f.txt exists in the working tree.
    REQUIRE(std::filesystem::exists(tmp.path() / "f.txt"));
    REQUIRE(std::filesystem::exists(tmp.path() / "m.txt"));
}

TEST_CASE("startRebase errors when the onto branch is missing", "[rebase]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("c1");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto out = repo->startRebase("does-not-exist");
    REQUIRE_FALSE(out.has_value());
}

TEST_CASE("startRebase errors mid-merge", "[rebase]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("base");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->createBranch("feature", "").has_value());
    REQUIRE(repo->checkoutBranch("feature").has_value());
    tmp.writeFile("a.txt", "feature\n");
    tmp.commitAll("feature edit");
    REQUIRE(repo->checkoutBranch("master").has_value());
    tmp.writeFile("a.txt", "main\n");
    tmp.commitAll("main edit");
    auto m = repo->mergeBranch("feature");     // conflicts → mid-merge
    REQUIRE(m.has_value());

    auto out = repo->startRebase("feature");
    REQUIRE_FALSE(out.has_value());            // refuse to rebase mid-merge
}

TEST_CASE("startRebase pauses on conflict, continueRebase finishes after resolve", "[rebase]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("c0");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->createBranch("feature", "").has_value());
    REQUIRE(repo->checkoutBranch("feature").has_value());
    tmp.writeFile("a.txt", "feature\n");       // same line → will conflict
    tmp.commitAll("c1 on feature");

    REQUIRE(repo->checkoutBranch("master").has_value());
    tmp.writeFile("a.txt", "main\n");
    tmp.commitAll("c2 on master");
    REQUIRE(repo->checkoutBranch("feature").has_value());

    auto out = repo->startRebase("master");
    REQUIRE(out.has_value());
    REQUIRE(out->conflicted);
    auto st = repo->rebaseState();
    REQUIRE(st.inProgress);
    REQUIRE(st.conflictedPaths.size() == 1);

    // Resolve: write a merged file and stage it.
    tmp.writeFile("a.txt", "resolved\n");
    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());

    auto cont = repo->continueRebase();
    REQUIRE(cont.has_value());
    REQUIRE_FALSE(cont->conflicted);
    REQUIRE_FALSE(repo->rebaseState().inProgress);
}

TEST_CASE("continueRebase errors when no rebase is in progress", "[rebase]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("c1");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto cont = repo->continueRebase();
    REQUIRE_FALSE(cont.has_value());
}

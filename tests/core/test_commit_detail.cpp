#include <catch2/catch_test_macros.hpp>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

using gittide::GitRepo;
using gittide::test::TempRepo;

TEST_CASE("commitDetail reports message split, author and line stats", "[commitdetail]")
{
    // TempRepo::commitAll commits with a fixed "Test <test@example.com>" author
    // (see test_commit_email.cpp); TempRepo::setIdentity only affects paths that
    // read the repo config identity (e.g. GitRepo::commit), not this raw helper.
    TempRepo repo;
    repo.writeFile("a.txt", "one\ntwo\n");
    repo.commitAll("base");

    // Second commit: +2 lines added to a new file, 1 line changed in a.txt.
    repo.writeFile("a.txt", "one\nCHANGED\n");
    repo.writeFile("b.txt", "x\ny\n");
    repo.commitAll("Add b and edit a\n\nSecond line of body.\nThird line.");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto log = gr->log(0);
    REQUIRE(log);
    const std::string tip = log->at(0).oid; // newest first

    auto d = gr->commitDetail(tip);
    REQUIRE(d);
    CHECK(d->summary == "Add b and edit a");
    CHECK(d->body == "Second line of body.\nThird line.");
    CHECK(d->authorName == "Test");
    CHECK(d->authorEmail == "test@example.com");
    CHECK(d->authorTime > 0);
    CHECK(d->filesChanged == 2);     // a.txt modified, b.txt added
    CHECK(d->additions == 3);        // b.txt: 2, a.txt: 1
    CHECK(d->deletions == 1);        // a.txt: "two" removed
}

TEST_CASE("commitDetail on the root commit diffs against the empty tree", "[commitdetail]")
{
    TempRepo repo;
    repo.writeFile("a.txt", "one\ntwo\n");
    repo.commitAll("root");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto log = gr->log(0);
    REQUIRE(log);

    auto d = gr->commitDetail(log->at(0).oid);
    REQUIRE(d);
    CHECK(d->summary == "root");
    CHECK(d->body.empty());
    CHECK(d->filesChanged == 1);
    CHECK(d->additions == 2);
    CHECK(d->deletions == 0);
}

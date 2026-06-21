#include <catch2/catch_test_macros.hpp>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

using gittide::GitRepo;
using gittide::test::TempRepo;

TEST_CASE("syncStatus reports no upstream when none is set", "[sync][status]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto st = gr->syncStatus();
    REQUIRE(st);
    REQUIRE_FALSE(st->hasUpstream);
}

TEST_CASE("syncStatus reports ahead after a local-only commit", "[sync][status]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    repo.addBareRemote("origin");
    repo.pushBranch("origin", "master");

    repo.writeFile("a.txt", "two");
    repo.commitAll("c2"); // local-only

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto st = gr->syncStatus();
    REQUIRE(st);
    REQUIRE(st->hasUpstream);
    REQUIRE(st->ahead == 1);
    REQUIRE(st->behind == 0);
    REQUIRE(st->remoteName == "origin");
    REQUIRE(st->upstreamName == "origin/master");
}

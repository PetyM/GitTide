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

TEST_CASE("fetch updates remote-tracking and reports behind", "[sync][fetch]")
{
    // origin starts at c1; a *second* clone pushes c2; our repo fetches and is behind 1.
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    auto bare = repo.addBareRemote("origin");
    repo.pushBranch("origin", "master");

    // Second working clone of the bare, adds c2, pushes it.
    TempRepo other;
    other.cloneFrom(bare);                 // helper added below
    other.setIdentity("Other", "o@example.com");
    other.writeFile("a.txt", "two");
    other.commitAll("c2");
    other.pushBranch("origin", "master");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto fr = gr->fetch("origin", gittide::Credentials{}, [](unsigned, unsigned) { return true; });
    REQUIRE(fr);

    auto st = gr->syncStatus();
    REQUIRE(st);
    REQUIRE(st->hasUpstream);
    REQUIRE(st->behind == 1);
    REQUIRE(st->ahead == 0);
}

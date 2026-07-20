#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

using gittide::GitRepo;
using gittide::test::TempRepo;

namespace {
bool contains(const std::vector<std::string>& v, const std::string& oid)
{
    return std::find(v.begin(), v.end(), oid) != v.end();
}
} // namespace

TEST_CASE("localOnlyOids returns commits not reachable from any remote-tracking ref", "[localonly]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    repo.addBareRemote("origin");
    repo.pushBranch("origin", "master"); // origin/master now at c1

    repo.writeFile("a.txt", "two");
    repo.commitAll("c2"); // local-only

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);

    auto head = gr->log(0);
    REQUIRE(head);
    REQUIRE(head->size() == 2);
    const std::string c2 = head->at(0).oid; // newest first
    const std::string c1 = head->at(1).oid;

    auto localOnly = gr->localOnlyOids();
    REQUIRE(localOnly);
    CHECK(contains(*localOnly, c2));
    CHECK_FALSE(contains(*localOnly, c1)); // c1 is on origin/master
    CHECK(localOnly->size() == 1);
}

TEST_CASE("localOnlyOids returns all HEAD commits when there is no remote", "[localonly]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    repo.writeFile("a.txt", "two");
    repo.commitAll("c2");

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto localOnly = gr->localOnlyOids();
    REQUIRE(localOnly);
    CHECK(localOnly->size() == 2); // nothing is pushed anywhere
}

TEST_CASE("localOnlyOids is empty when HEAD is fully pushed", "[localonly]")
{
    TempRepo repo;
    repo.setIdentity("Test", "test@example.com");
    repo.writeFile("a.txt", "one");
    repo.commitAll("c1");
    repo.addBareRemote("origin");
    repo.pushBranch("origin", "master"); // origin/master at c1 == HEAD

    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto localOnly = gr->localOnlyOids();
    REQUIRE(localOnly);
    CHECK(localOnly->empty());
}

TEST_CASE("localOnlyOids is empty for an unborn branch", "[localonly]")
{
    TempRepo repo; // no commits
    auto gr = GitRepo::open(repo.path());
    REQUIRE(gr);
    auto localOnly = gr->localOnlyOids();
    REQUIRE(localOnly);
    CHECK(localOnly->empty());
}

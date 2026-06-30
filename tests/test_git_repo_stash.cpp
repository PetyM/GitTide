#include <catch2/catch_test_macros.hpp>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

TEST_CASE("stashCount reflects the stash stack", "[stash]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "orig\n");
    tmp.commitAll("init");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    REQUIRE(repo->stashCount().value() == 0);

    tmp.writeFile("a.txt", "dirty\n");
    REQUIRE(repo->stashSave("wip").value() == true); // a stash was created
    REQUIRE(repo->stashCount().value() == 1);

    REQUIRE(repo->stashPop().has_value());
    REQUIRE(repo->stashCount().value() == 0);
}

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

TEST_CASE("stashList returns entries newest-first with message and oid", "[stash]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "orig\n");
    tmp.commitAll("init");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->stashList().value().empty());

    tmp.writeFile("a.txt", "first\n");
    REQUIRE(repo->stashSave("one").value() == true);
    tmp.writeFile("a.txt", "second\n");
    REQUIRE(repo->stashSave("two").value() == true);

    auto list = repo->stashList();
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 2);
    // Newest is stash@{0}.
    REQUIRE((*list)[0].index == 0);
    REQUIRE((*list)[1].index == 1);
    REQUIRE((*list)[0].message.find("two") != std::string::npos);
    REQUIRE((*list)[1].message.find("one") != std::string::npos);
    REQUIRE((*list)[0].oid.size() == 40);
}

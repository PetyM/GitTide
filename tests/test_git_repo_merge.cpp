#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

using gittide::GitRepo;
using gittide::StatusFlag;
using gittide::hasFlag;

TEST_CASE("Conflicted flag composes and is distinct", "[merge]")
{
    StatusFlag f = StatusFlag::WtModified | StatusFlag::Conflicted;
    REQUIRE(hasFlag(f, StatusFlag::Conflicted));
    REQUIRE(hasFlag(f, StatusFlag::WtModified));
    REQUIRE_FALSE(hasFlag(StatusFlag::WtModified, StatusFlag::Conflicted));
}

TEST_CASE("mergeState reports not-in-progress for a clean repo", "[merge]")
{
    gittide::test::TempRepo tmp;
    tmp.setIdentity("Test", "test@example.com");
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("c1");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto ms = repo->mergeState();
    REQUIRE(ms.has_value());
    REQUIRE_FALSE(ms->inProgress);
    REQUIRE(ms->conflictedPaths.empty());
}

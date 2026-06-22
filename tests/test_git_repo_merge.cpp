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

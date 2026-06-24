#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>

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

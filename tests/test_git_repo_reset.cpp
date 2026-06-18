#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

using gittide::GitRepo;
using gittide::StatusFlag;
using gittide::hasFlag;

namespace {
bool anyStaged(const std::vector<gittide::FileStatus>& v)
{
    return std::any_of(v.begin(), v.end(), [](const auto& f) {
        return hasFlag(f.flags, StatusFlag::IndexNew)
            || hasFlag(f.flags, StatusFlag::IndexModified)
            || hasFlag(f.flags, StatusFlag::IndexDeleted);
    });
}
}

TEST_CASE("resetIndexToHead unstages a staged file but keeps the worktree edit", "[reset]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "base\n");
    tmp.commitAll("init");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    tmp.writeFile("a.txt", "edited\n");
    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());
    REQUIRE(anyStaged(*repo->status())); // precondition: a.txt is staged

    REQUIRE(repo->resetIndexToHead().has_value());

    auto st = repo->status();
    REQUIRE(st.has_value());
    REQUIRE_FALSE(anyStaged(*st));                 // nothing staged now
    REQUIRE_FALSE(st->empty());                    // but the change still shows (unstaged)
    REQUIRE(hasFlag((*st)[0].flags, StatusFlag::WtModified));
}

TEST_CASE("resetIndexToHead clears the index on an unborn branch", "[reset]")
{
    gittide::test::TempRepo tmp;            // fresh repo, no commits
    tmp.writeFile("a.txt", "x\n");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());

    REQUIRE(repo->resetIndexToHead().has_value());

    auto st = repo->status();
    REQUIRE(st.has_value());
    REQUIRE_FALSE(anyStaged(*st));          // a.txt is back to untracked
    REQUIRE(hasFlag((*st)[0].flags, StatusFlag::WtNew));
}

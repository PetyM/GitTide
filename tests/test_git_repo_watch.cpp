#include <algorithm>
#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include "gittide/gitrepo.hpp"
#include "gittide/watch.hpp"
#include "support/temprepo.hpp"

using gittide::GitRepo;

namespace {
bool contains(const std::vector<std::filesystem::path>& v, const std::filesystem::path& p)
{
    return std::find(v.begin(), v.end(), p) != v.end();
}
} // namespace

TEST_CASE("watchTargets includes worktree dirs and the git dir", "[gitrepo][watch]")
{
    gittide::test::TempRepo repo;
    repo.writeFile("src/a.txt", "hello");
    repo.commitAll("init");

    auto r = GitRepo::open(repo.path());
    REQUIRE(r);
    auto t = r->watchTargets();
    REQUIRE(t);

    CHECK_FALSE(t->workdir.empty());
    CHECK_FALSE(t->gitDir.empty());
    // The working-tree root and a tracked subdirectory are watched.
    CHECK(contains(t->dirs, t->workdir));
    CHECK(contains(t->dirs, t->workdir / "src"));
    // The git dir itself is watched (so external commits/checkouts are seen).
    CHECK(contains(t->dirs, t->gitDir));
}

TEST_CASE("watchTargets prunes gitignored directories", "[gitrepo][watch]")
{
    gittide::test::TempRepo repo;
    repo.writeFile(".gitignore", "build/\n");
    repo.writeFile("src/a.txt", "hello");
    repo.writeFile("build/out.o", "junk");
    repo.commitAll("init");

    auto r = GitRepo::open(repo.path());
    REQUIRE(r);
    auto t = r->watchTargets();
    REQUIRE(t);

    CHECK(contains(t->dirs, t->workdir / "src"));
    CHECK_FALSE(contains(t->dirs, t->workdir / "build"));
}

TEST_CASE("watchTargets does not re-list the worktree's .git as a worktree dir", "[gitrepo][watch]")
{
    gittide::test::TempRepo repo;
    repo.writeFile("a.txt", "hello");
    repo.commitAll("init");

    auto r = GitRepo::open(repo.path());
    REQUIRE(r);
    auto t = r->watchTargets();
    REQUIRE(t);

    // The worktree walk must not descend into .git; the git dir is enumerated
    // separately under t->gitDir, so there is exactly one watched ".git" root.
    const auto count = std::count(t->dirs.begin(), t->dirs.end(), t->gitDir);
    CHECK(count == 1);
}

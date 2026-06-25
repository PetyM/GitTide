#include <catch2/catch_test_macros.hpp>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

using gittide::DiffLineOrigin;
using gittide::DiffTarget;

TEST_CASE("GitRepo::diff WorktreeVsIndex shows unstaged edit", "[diff]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\ny\nz\n");
    tmp.commitAll("init");
    tmp.writeFile("a.txt", "x\nY2\nz\n");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto d = repo->diff(DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.size() == 1);

    bool has_added = false, has_removed = false;
    for (const auto& ln : d->hunks[0].lines)
    {
        has_added |= ln.origin == DiffLineOrigin::Added;
        has_removed |= ln.origin == DiffLineOrigin::Removed;
    }
    REQUIRE(has_added);
    REQUIRE(has_removed);
}

TEST_CASE("GitRepo::diff IndexVsHead is empty with nothing staged", "[diff]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");

    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto d = repo->diff(DiffTarget::IndexVsHead, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.empty());
}

TEST_CASE("WorktreeVsHead shows an untracked file as all-added", "[diff]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("init");
    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    // Brand-new, never-staged file.
    tmp.writeFile("new.txt", "alpha\nbeta\ngamma\n");

    auto d = repo->diff(gittide::DiffTarget::WorktreeVsHead, "new.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.size() == 1);

    bool has_added = false, has_removed = false;
    for (const auto& ln : d->hunks[0].lines)
    {
        has_added |= ln.origin == DiffLineOrigin::Added;
        has_removed |= ln.origin == DiffLineOrigin::Removed;
    }
    REQUIRE(has_added);
    REQUIRE_FALSE(has_removed);
}

TEST_CASE("WorktreeVsHead shows an untracked file inside a new directory", "[diff]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("init");
    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    // New file in a brand-new, untracked directory — libgit2 reports the dir as a
    // single untracked entry unless the diff recurses into it.
    tmp.writeFile("sub/new.txt", "alpha\nbeta\n");

    auto d = repo->diff(gittide::DiffTarget::WorktreeVsHead, "sub/new.txt");
    REQUIRE(d.has_value());
    REQUIRE(d->hunks.size() == 1);

    bool has_added = false;
    for (const auto& ln : d->hunks[0].lines)
        has_added |= ln.origin == DiffLineOrigin::Added;
    REQUIRE(has_added);
}

TEST_CASE("WorktreeVsHead shows changes even when the index is staged", "[diff]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "one\n");
    tmp.commitAll("init");
    auto repo = gittide::GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    tmp.writeFile("a.txt", "one\ntwo\n");
    REQUIRE(repo->stage(gittide::StageSelection{"a.txt", std::nullopt, {}}).has_value());

    // index now matches the worktree, so WorktreeVsIndex is empty...
    auto wi = repo->diff(gittide::DiffTarget::WorktreeVsIndex, "a.txt");
    REQUIRE(wi.has_value());
    REQUIRE(wi->hunks.empty());

    // ...but WorktreeVsHead still reports the added line.
    auto wh = repo->diff(gittide::DiffTarget::WorktreeVsHead, "a.txt");
    REQUIRE(wh.has_value());
    REQUIRE_FALSE(wh->hunks.empty());
}

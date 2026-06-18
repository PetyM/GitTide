#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

using gittide::GitRepo;

namespace {
bool has(const std::vector<gittide::BranchInfo>& v, const std::string& n)
{
    return std::any_of(v.begin(), v.end(), [&](const auto& b) { return b.name == n; });
}
}

TEST_CASE("createBranch from HEAD makes a listable branch", "[branches]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    REQUIRE(repo->createBranch("feature", "").has_value());
    auto list = repo->branches();
    REQUIRE(list.has_value());
    REQUIRE(has(*list, "feature"));
    // not switched: HEAD unchanged
    REQUIRE_FALSE(std::any_of(list->begin(), list->end(),
                              [](const auto& b) { return b.isHead && b.name == "feature"; }));
}

TEST_CASE("createBranch rejects an invalid name", "[branches]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto r = repo->createBranch("bad name~^:", "");
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("branches lists the default branch and marks HEAD", "[branches]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());

    auto list = repo->branches();
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 1);
    REQUIRE((*list)[0].isHead);

    auto h = repo->head();
    REQUIRE(h.has_value());
    REQUIRE_FALSE(h->detached);
    REQUIRE(h->branch == (*list)[0].name);
    REQUIRE(h->oid.size() == 40);
}

TEST_CASE("deleteBranch removes a merged branch but blocks the current one", "[branches]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    const std::string cur = repo->head()->branch;

    REQUIRE(repo->createBranch("merged", "").has_value()); // same tip => merged
    REQUIRE(repo->deleteBranch("merged", /*force=*/false).has_value());
    REQUIRE_FALSE(has(*repo->branches(), "merged"));

    REQUIRE_FALSE(repo->deleteBranch(cur, false).has_value()); // current is blocked
}

TEST_CASE("renameBranch renames and rejects invalid names", "[branches]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "x\n");
    tmp.commitAll("init");
    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    REQUIRE(repo->createBranch("old", "").has_value());

    REQUIRE(repo->renameBranch("old", "new", false).has_value());
    REQUIRE(has(*repo->branches(), "new"));
    REQUIRE_FALSE(has(*repo->branches(), "old"));

    REQUIRE_FALSE(repo->renameBranch("new", "bad~name", false).has_value());
}

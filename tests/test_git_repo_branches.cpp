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

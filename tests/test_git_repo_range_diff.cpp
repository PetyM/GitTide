#include <catch2/catch_test_macros.hpp>
#include <algorithm>

#include "gittide/gitrepo.hpp"
#include "support/temprepo.hpp"

using gittide::GitRepo;
using gittide::StatusFlag;
using gittide::hasFlag;

namespace {
bool has(const std::vector<gittide::FileStatus>& v, const std::string& p)
{
    return std::any_of(v.begin(), v.end(),
                       [&](const auto& f) { return f.path.generic_string() == p; });
}
}

TEST_CASE("rangeFiles lists the net file set across a contiguous range", "[range]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "a1\n"); tmp.commitAll("c1");      // adds a.txt
    tmp.writeFile("b.txt", "b1\n"); tmp.commitAll("c2");      // adds b.txt
    tmp.writeFile("a.txt", "a1\na2\n"); tmp.commitAll("c3");  // modifies a.txt

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    auto h = repo->log();
    REQUIRE(h.has_value());
    REQUIRE(h->size() == 3);
    const std::string c3 = (*h)[0].oid; // newest
    const std::string c2 = (*h)[1].oid;
    const std::string c1 = (*h)[2].oid; // oldest

    // Range c1..c3 inclusive == parent(c1) [empty] vs tree(c3): both files present.
    auto files = repo->rangeFiles(c1, c3);
    REQUIRE(files.has_value());
    REQUIRE(has(*files, "a.txt"));
    REQUIRE(has(*files, "b.txt"));

    // Per-file diff over the range carries hunks.
    auto d = repo->rangeDiff(c1, c3, "a.txt");
    REQUIRE(d.has_value());
    REQUIRE_FALSE(d->hunks.empty());

    // Range c2..c3 (parent(c2)=c1's tree vs c3): a.txt modified, b.txt added.
    auto sub = repo->rangeFiles(c2, c3);
    REQUIRE(sub.has_value());
    REQUIRE(has(*sub, "a.txt"));
    REQUIRE(has(*sub, "b.txt"));
}

TEST_CASE("rangeFiles of a single commit equals that commit's files", "[range]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "a1\n"); tmp.commitAll("c1");
    tmp.writeFile("b.txt", "b1\n"); tmp.commitAll("c2");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    auto h = repo->log();
    REQUIRE(h.has_value());
    const std::string c2 = h->front().oid;

    auto single = repo->rangeFiles(c2, c2);
    auto cf     = repo->commitFiles(c2);
    REQUIRE(single.has_value());
    REQUIRE(cf.has_value());
    REQUIRE(single->size() == cf->size());
    REQUIRE(has(*single, "b.txt"));
    REQUIRE_FALSE(has(*single, "a.txt"));
}

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

TEST_CASE("rangeFiles starting from the root commit includes the root's own files", "[range]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("root.txt", "root content\n"); tmp.commitAll("root");
    tmp.writeFile("second.txt", "second content\n"); tmp.commitAll("second");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    auto h = repo->log();
    REQUIRE(h.has_value());
    REQUIRE(h->size() == 2);

    const std::string newest = (*h)[0].oid;
    const std::string root   = (*h)[1].oid;

    // Range from the root commit: root.txt (added by root) and second.txt (added by second).
    auto files = repo->rangeFiles(root, newest);
    REQUIRE(files.has_value());
    REQUIRE(has(*files, "root.txt"));   // root commit's own file is present
    REQUIRE(has(*files, "second.txt")); // second commit adds this too
}

TEST_CASE("single-commit rangeDiff equals commitDiff for the same file", "[range]")
{
    gittide::test::TempRepo tmp;
    tmp.writeFile("a.txt", "line1\n");              tmp.commitAll("c1");
    tmp.writeFile("a.txt", "line1\nline2\n");        tmp.commitAll("c2");

    auto repo = GitRepo::open(tmp.path());
    REQUIRE(repo.has_value());
    auto h = repo->log();
    REQUIRE(h.has_value());
    const std::string c2 = h->front().oid;

    auto rd = repo->rangeDiff(c2, c2, "a.txt");
    auto cd = repo->commitDiff(c2, "a.txt");
    REQUIRE(rd.has_value());
    REQUIRE(cd.has_value());

    // Both must produce the same number of hunks.
    REQUIRE(rd->hunks.size() == cd->hunks.size());
    REQUIRE_FALSE(rd->hunks.empty());

    // The first hunk's line count must also match.
    REQUIRE(rd->hunks[0].lines.size() == cd->hunks[0].lines.size());
}
